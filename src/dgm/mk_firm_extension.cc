#include "mk_firm_extension.h"
#include "dgm/transmission_operations.h"
#include "dgm/traversal.h"
#include "network/histogram.h"
#include "network/stream.h"
#include "network/topology.h"
#include "nlohmann/json_fwd.hpp"
#include "utils/generator.h"
#include <algorithm>
#include <cstddef>
#include <map>
#include <numeric>
#include <print>
#include <ranges>
#include <utility>

namespace tsndgm {

void TokenStepFunction::extend(Delay period) {
  if (hyper_cycle == 1) {
    hyper_cycle = period;
    func.insert({hyper_cycle, 0});
    return;
  }

  func.erase(hyper_cycle);
  Delay const old_hyper_cycle = hyper_cycle;
  size_t const old_size = func.size();
  hyper_cycle = std::lcm(period, hyper_cycle);
  func.insert({hyper_cycle, 0});
  for (Count c = 1; c < hyper_cycle / old_hyper_cycle; c++) {
    for (auto [t, b] : func | std::views::take(old_size)) {
      func.insert({(c * old_hyper_cycle) + t, b});
    }
  }
}

MKFirmConfiguration::MKFirmConfiguration(DFSTraversal &dfs, const ProcessingOrder &processing_order,
                                         Delay hyper_cycle) noexcept
    : hyper_cycle_(hyper_cycle), critical_path_(dfs, processing_order),
      processing_order_(&processing_order), crit_cost_(processing_order.total_operations),
      mu_(processing_order.total_operations) {}

auto MKFirmConfiguration::dump_to_json(const NetworkTopology *topology,
                                       nlohmann::json &&j) const noexcept -> nlohmann::json {
  j["MK_FIRM_PSFP"] = nlohmann::json::object();
  for (const auto &[device_id, psfp_gates] : mkfirm_psfp_config) {
    auto device_name = topology->at(device_id).name;
    j["MK_FIRM_PSFP"][device_name] = nlohmann::json::array();
    for (const auto &gate : psfp_gates) {
      nlohmann::json j_gate{
          {"frames", nlohmann::json::array()}, {"open", gate.open}, {"close", gate.close}};
      for (const auto &frame : gate.frames) {
        j_gate["frames"].push_back(frame.name());
      }
      j["MK_FIRM_PSFP"][device_name].push_back(std::move(j_gate));
    }
  }

  return j;
}

void MKFirmConfiguration::prolongation(const Vertex &v) noexcept {
  const auto &tb = token_bucket(v);
  crit_cost_[v.id] = std::max(crit_cost_[v.id], critical_path_[v.id].cost);
  // Conservative bound that allows refilling TB during frame transmissions (in case there
  // are multiple v.frames).
  Bits const elevated_traffic =
      (TicksPerSec * BitsPerByte * tb.bucket_size) + (v.weights.pdb.d_trans.max * tb.token_rate);
  mu_[v.id] = std::max(mu_[v.id],
                       crit_cost_[v.id] + (elevated_traffic / (tb.link_data_rate - tb.token_rate)));
}

void MKFirmConfiguration::update_prolongation(const Edge &e) noexcept {
  auto [u, v, _] = e;
  const auto &tb = token_bucket(*v);
  Bits const elevated_traffic = v->weights.pdb.d_trans.max * tb.token_rate;
  mu_[v->id] =
      std::max(mu_[v->id], mu_[u->id] + (elevated_traffic / (tb.link_data_rate - tb.token_rate)));
}

void MKFirmConfiguration::deferment(const Edge &e) noexcept {
  auto [u, v, _] = e;
  crit_cost_[v->id] = std::max(crit_cost_[v->id], mu_[u->id] + u->source->clock_resolution);
}

void MKFirmConfiguration::fault_isolation(const Edge &e) noexcept {
  auto [u, v, _] = e;
  crit_cost_[v->id] = std::max(crit_cost_[v->id],
                               mu_[u->id] + u->weights[FIFO].outgoing + v->weights[FIFO].incoming);
}

void MKFirmConfiguration::sequential_transmission(const Edge &e) noexcept {
  auto [u, v, _] = e;
  const auto &tb = token_bucket_diff(e);
  Bits const branched_off_traffic = (TicksPerSec * BitsPerByte * tb.bucket_size) +
                                    ((mu_[u->id] - crit_cost_[u->id]) * tb.token_rate);
  crit_cost_[v->id] = std::max(crit_cost_[v->id], crit_cost_[u->id] + u->weights[JOB].outgoing +
                                                      v->weights[JOB].incoming +
                                                      (branched_off_traffic / tb.link_data_rate));
}

auto MKFirmConfiguration::check_stable_qos(const Vertex &v) const noexcept -> TraversalStatus {
  auto dmax = v.weights[JOB].outgoing;
  for (auto frame : v.frames) {
    auto dmin = frame.stream->pdb_map.at(v.link()).d_total.min;
    DelayInterval const arrival_interval = DelayInterval(dmin + crit_cost_[v.id], dmax + mu_[v.id]);
    auto objective = frame.stream->objective(arrival_interval, frame.id);
    if (objective > 0) {
      std::println("Stable QoS violation of {}: [{}, {}] -> {}", frame.stream->name,
                   arrival_interval.min, arrival_interval.max, objective);
      return ABORT;
    }
  }
  return CONTINUE;
}

void MKFirmConfiguration::add_mk_firm_psfp(const Vertex &v) noexcept {
  Delay psfp_closing_time = mu_[v.id] + v.weights.pdb.d_total.max;

  for (const auto &frame : v.frames) {
    const auto *stream = frame.stream;
    FrameIndex n = hyper_cycle_ / stream->period;
    for (FrameIndex c = frame.id; c < std::lcm(n, stream->mk_firm.k()); c += n) {
      if (!stream->mk_firm.mask[c % stream->mk_firm.k()]) {
        continue;
      }

      mkfirm_psfp_config[v.link().target].push_back(MKFirmPSFPGate{
          .frames = {Frame{.stream = stream, .id = c}},
          .open = (c * stream->period) + psfp_closing_time,
          .close = (c * stream->period) + stream->mk_firm.e2e_latency,
      });
    }
  }
}

auto MKFirmConfiguration::mk_firm_streams_at(Link link) const noexcept
    -> Generator<const Stream *> {
  for (const auto *op : (*processing_order_)[link]) {
    for (auto frame : op->frames) {
      if (frame.id == 0 && frame.stream->mk_firm.required()) {
        co_yield frame.stream;
      }
    }
  }
}

auto MKFirmConfiguration::mk_firm_stream_diff_at(Link link1, Link link2) const noexcept
    -> Generator<const Stream *> {
  for (const auto *op : (*processing_order_)[link1]) {
    for (auto frame : op->frames) {
      if (frame.id == 0 && frame.stream->mk_firm.required()) {
        auto it = std::ranges::find_if(
            op->route_succ, [frame](auto *op_succ) { return op_succ->frames.contains(frame); });
        if ((*it)->link() != link2) {
          co_yield frame.stream;
        }
      }
    }
  }
}

auto MKFirmConfiguration::token_bucket(const Vertex &v) noexcept -> const TokenBucket & {
  Link const link = v.link();
  if (!token_bucket_.contains(link)) {
    const auto &link_prop = (*v.source)[v.target->id];
    auto it =
        token_bucket_.insert({link, compute_token_bucket(link_prop, mk_firm_streams_at(link))})
            .first;
    return it->second;
  }
  return token_bucket_.at(link);
}

auto MKFirmConfiguration::token_bucket_diff(const Edge &e) noexcept -> const TokenBucket & {
  auto [u, v, _] = e;
  auto links = std::make_pair(u->link(), v->link());
  if (!token_bucket_diff_.contains(links)) {
    const auto &link_prop = (*u->source)[u->target->id];
    auto it = token_bucket_diff_
                  .insert({links, compute_token_bucket(link_prop, mk_firm_stream_diff_at(
                                                                      links.first, links.second))})
                  .first;
    return it->second;
  }
  return token_bucket_diff_.at(links);
}

auto MKFirmConfiguration::compute_token_bucket(const DataLinkProperty &link,
                                               Generator<const Stream *> &&streams) noexcept
    -> TokenBucket {
  auto token_step_function = compute_token_step_function(link, std::move(streams));
  token_step_function.extend(2 * token_step_function.hyper_cycle);
  Bytes const b = compute_bucket_size(token_step_function.func);
  DataRate const r = compute_token_rate(token_step_function.func, b);
  return {.bucket_size = b, .token_rate = r, .link_data_rate = link.data_rate};
}

auto MKFirmConfiguration::compute_token_step_function(const DataLinkProperty &link,
                                                      Generator<const Stream *> &&streams) noexcept
    -> TokenStepFunction {
  TokenStepFunction token_step_function;
  for (const auto &stream : streams) {
    /* For each frame f_i flagged by stream.mk_firm.mask, there can be an elevated frame during
     * the time window [i * stream.period, i * stream.period + stream.mk_firm.e2e_latency].
     * Increase the step function in that interval by F.frame_size.max. */
    token_step_function.extend(stream->period * stream->mk_firm.k());
    token_step_function.link_data_rate = link.data_rate;
    for (FrameIndex c = 0; c < token_step_function.hyper_cycle / stream->period; c++) {
      if (!stream->mk_firm.mask[c % stream->mk_firm.k()]) {
        continue;
      }

      auto &func = token_step_function.func;
      auto lower = --func.upper_bound(c * stream->period);
      lower = func.insert({c * stream->period, lower->second}).first;
      auto upper = --func.upper_bound((c * stream->period) + stream->mk_firm.e2e_latency);
      for (auto it = lower; it != upper; ++it) {
        it->second += stream->frame_size.max + IFGBytes;
      }
      func.insert({(c * stream->period) + stream->mk_firm.e2e_latency, upper->second});
    }
  }
  return token_step_function;
}

auto MKFirmConfiguration::compute_bucket_size(const std::map<Delay, Bytes> &func) noexcept
    -> Bytes {
  Bytes prev = 0;
  Bytes bucket_size = 0;
  // Conservative bound that all elevated frames arrive at joint of two consecutive steps
  for (auto b : func | std::views::values) {
    bucket_size = std::max(bucket_size, prev + b);
    prev = b;
  }
  return bucket_size;
}

auto MKFirmConfiguration::compute_token_rate(const std::map<Delay, Bytes> &func,
                                             Bytes bucket_size) noexcept -> DataRate {
  DataRate token_rate = 0;
  for (auto lower = func.begin(); lower != func.end();) {
    Bytes sum = lower->second;
    // Conservative bound that elevated frames arrive at last possible instant of "lower"
    for (auto upper = ++lower; upper != func.end(); ++upper) {
      sum += upper->second;
      if (sum > bucket_size) {
        token_rate = std::max(token_rate, (BitsPerByte * TicksPerSec * (sum - bucket_size)) /
                                              (upper->first - lower->first));
      }
    }
  }
  return token_rate;
}

} // namespace tsndgm
