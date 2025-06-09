#include "mk_firm_extension.h"
#include "dgm/transmission_operations.h"
#include "dgm/traversal.h"
#include "network/histogram.h"
#include "network/stream.h"
#include "network/topology.h"
#include "nlohmann/json_fwd.hpp"
#include "utils/generator.h"
#include <algorithm>
#include <map>
#include <numeric>
#include <print>
#include <utility>

namespace tsndgm {

auto ElevationCount::operator()(Delay t1, Delay t2) const noexcept -> Count {
  auto k = stream->mk_firm.k();
  Count l = ((t1 - stream->mk_firm.e2e_latency) / stream->period) + 1;
  Count h = t2 / stream->period;

  Count c = 0;
  for (auto i = l; i <= h; i++) {
    c += static_cast<Count>(stream->mk_firm.mask[i % k]);
  }
  return c;
}

ElevationStepFunctions::ElevationStepFunctions(Generator<const Stream *> &&streams) noexcept {
  for (const auto *stream : streams) {
    funcs.emplace_back(ElevationCount(stream));
    hyper_cycle = std::lcm(hyper_cycle, stream->period * stream->mk_firm.k());
  }

  for (auto &func : funcs) {
    auto k = func.stream->mk_firm.k();
    for (auto i = 0; i < hyper_cycle / func.stream->period; i++) {
      if (func.stream->mk_firm.mask[i % k]) {
        increments.insert(i * func.stream->period);
        decrements.insert((i * func.stream->period) + func.stream->mk_firm.e2e_latency);
      }
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
  if (e.source->pcp < e.target->pcp) {
    crit_cost_[v->id] = std::max(crit_cost_[v->id], mu_[u->id] + u->source->clock_resolution);
  } else { // e.source->pcp == e.target->pcp (special case for first hop; not covered by fifo)
    crit_cost_[v->id] = std::max(crit_cost_[v->id], mu_[u->id] + u->weights[MACHINE].outgoing +
                                                        v->weights[MACHINE].incoming);
  }
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

auto MKFirmConfiguration::mk_firm_hypercycle_at(Link link) const noexcept -> Delay {
  Delay hyper_cycle = 1;
  for (const auto *op : (*processing_order_)[link]) {
    for (auto frame : op->frames) {
      if (frame.id == 0 && frame.stream->mk_firm.required()) {
        hyper_cycle = std::lcm(hyper_cycle, frame.stream->period * frame.stream->mk_firm.k());
      }
    }
  }
  return hyper_cycle;
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
  auto stream_elevation = ElevationStepFunctions(std::move(streams));
  Bytes const b = compute_bucket_size(stream_elevation);
  DataRate const r = compute_token_rate(stream_elevation, b);
  return {.bucket_size = b, .token_rate = r, .link_data_rate = link.data_rate};
}

auto MKFirmConfiguration::compute_bucket_size(
    const ElevationStepFunctions &stream_elevation) noexcept -> Bytes {
  Bytes bucket_size = 0;
  for (auto t : stream_elevation.increments) {
    Bytes b_t = std::ranges::fold_left(
        stream_elevation.funcs, static_cast<Bytes>(0), [t](auto b, auto &func) {
          return b + (func(t) * (func.stream->frame_size.max + IFGBytes));
        });
    bucket_size = std::max(bucket_size, b_t);
  }
  return bucket_size;
}

auto MKFirmConfiguration::compute_token_rate(const ElevationStepFunctions &stream_elevation,
                                             Bytes bucket_size) noexcept -> DataRate {
  auto refill = [&stream_elevation](Delay t1, Delay t2) -> DataRate {
    return std::ranges::fold_left(
        stream_elevation.funcs, static_cast<DataRate>(0), [t1, t2](auto b, auto &func) {
          return b + (func(t1, t2) * (func.stream->frame_size.max + IFGBytes));
        });
  };

  DataRate token_rate = 0;
  for (auto t1 : stream_elevation.increments) {
    for (auto t2 : stream_elevation.decrements) {
      if (t1 < t2) {
        token_rate = std::max(token_rate, BitsPerByte * TicksPerSec *
                                              (refill(t1, t2) - bucket_size) / (t2 - t1));
      }
      t2 += stream_elevation.hyper_cycle;
      token_rate = std::max(token_rate,
                            BitsPerByte * TicksPerSec * (refill(t1, t2) - bucket_size) / (t2 - t1));
    }
  }
  return token_rate;
}

} // namespace tsndgm
