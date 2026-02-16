#include "tsn_configuration.h"
#include "dgm/transmission_operations.h"
#include "dgm/traversal.h"
#include "mk_firm_extension.h"
#include "network/topology.h"
#include "nlohmann/json_fwd.hpp"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <limits>
#include <ranges>
#include <utility>

namespace tsndgm {

auto PeriodicGate::operator+=(const PeriodicGateInterval &interval) -> PeriodicGate & {
  Delay const duration = interval.closing_time - interval.opening_time;
  if (interval.opening_time == offset && !durations.empty()) {
    durations.back() += duration;
  } else {
    durations.push_back(interval.opening_time - offset);
    durations.push_back(duration);
  }
  offset = interval.opening_time + duration;
  return *this;
}

void PeriodicGate::extend_to(Delay time) {
  switch (initial) {
  [[likely]] case CLOSED:
    offset = time - offset;
    durations[0] += offset;
    return;
  [[unlikely]] case OPEN:
    durations.push_back(time - offset);
    offset = 0;
    return;
  }
}

template <typename ConfigurationType>
TSNConfiguration<ConfigurationType>::TSNConfiguration(DFSTraversal &dfs, ConfigurationType &&config,
                                                      const ProcessingOrder &processing_order,
                                                      const NetworkTopology *topology,
                                                      Delay hyper_cycle)
    : configuration(std::move(config)), dfs_(&dfs), processing_order_(&processing_order),
      topology_(topology), hyper_cycle_(hyper_cycle) {
  status = dfs_->traverse<BACKWARD>(&processing_order.sink(), traversal_events());
  if (status != COMPLETED) {
    *this = TSNConfiguration<ConfigurationType>();
    return;
  }
  for (auto &[_, gate] : gcl_config) {
    gate.extend_to(hyper_cycle_);
  }
  add_meta_data("makespan", configuration[SINK_ID].max);
}

template <typename ConfigurationType>
void TSNConfiguration<ConfigurationType>::add_talker_entry(
    const TransmissionOperation &op) noexcept {
  DeviceId const device = op.link().source;
  for (auto frame : op.frames) {
    if (frame.stream->route[device].is_talker()) {
      talker_config[frame] = configuration[op.id].min;
    }
  }
}

template <typename ConfigurationType>
void TSNConfiguration<ConfigurationType>::add_listener_entry(
    const TransmissionOperation &op) noexcept {
  DeviceId const device = op.link().target;
  for (auto frame : op.frames) {
    if (frame.stream->route[device].is_listener()) {
      Delay const d_min = frame.stream->pdb_map.at(op.link()).d_total.min;
      Delay const d_max = op.weights.pdb.d_total.max;
      listener_config[frame] = DelayInterval(d_min, d_max) + configuration[op.id];
    }
  }
}

template <typename ConfigurationType>
void TSNConfiguration<ConfigurationType>::add_gcl_entry(const TransmissionOperation &op) noexcept {
  Delay const opening_time = configuration[op.id].min;
  Delay const closing_time = configuration[op.id].max + op.weights.pdb.d_trans.max;

  gcl_config[{op.link(), op.pcp}] +=
      PeriodicGateInterval{.opening_time = opening_time, .closing_time = closing_time};
}

template <typename ConfigurationType>
void TSNConfiguration<ConfigurationType>::add_psfp_entries(
    const TransmissionOperation &op) noexcept {
  DeviceId const device = op.link().target;
  auto d_min = std::ranges::fold_left(
      op.frames, std::numeric_limits<Delay>::max(), [&op](Delay d, auto &frame) -> auto {
        return std::min(d, frame.stream->pdb_map.at(op.link()).d_total.min);
      });

  psfp_config[device].push_back(PSFPGate{
      .frames = op.frames,
      .open = configuration[op.id].min + d_min,
      .close = configuration[op.id].max + op.weights.pdb.d_total.max,
  });
}

template <typename ConfigurationType>
[[nodiscard]] auto TSNConfiguration<ConfigurationType>::dump_to_json() const
    -> nlohmann::ordered_json {
  nlohmann::ordered_json j = {{"EXACT", nlohmann::ordered_json::object()},
                              {"TALKERS", nlohmann::ordered_json::object()},
                              {"LISTENERS", nlohmann::ordered_json::object()},
                              {"GCL", nlohmann::ordered_json::object()},
                              {"PSFP", nlohmann::ordered_json::object()},
                              {"META", meta_data_}};

  // Exact transmission offsets at each hop
  for (auto frame : std::views::keys(talker_config)) {
    j["EXACT"][frame.name()] = nlohmann::ordered_json::object();
    for (const auto *op : processing_order_->traverse_operations(frame)) {
      j["EXACT"][frame.name()][topology_->link_to_string(op->link())] = {configuration[op->id].min,
                                                                         configuration[op->id].max};
    }
  }

  // Exact transmission offset at talkers
  for (const auto &[frame, tx_time] : talker_config) {
    j["TALKERS"][frame.name()] = tx_time;
  }

  // Arrival interval at listeners
  for (const auto &[frame, arrival_interval] : listener_config) {
    j["LISTENERS"][frame.name()] = {arrival_interval.min, arrival_interval.max};
  }

  // Gate Control Lists
  for (const auto &[port, gate] : gcl_config) {
    auto [link, pcp] = port;
    auto egress_port = topology_->link_to_string(link);
    j["GCL"][egress_port][std::format("Q{}", pcp)]["initial"] = gate.initial;
    j["GCL"][egress_port][std::format("Q{}", pcp)]["offset"] = gate.offset;
    j["GCL"][egress_port][std::format("Q{}", pcp)]["durations"] = gate.durations;
  }

  // Per-Stream Filtering and Policing
  for (const auto &[device_id, psfp_gates] : psfp_config) {
    auto device_name = topology_->at(device_id).name;
    j["PSFP"][device_name] = nlohmann::ordered_json::array();
    for (const auto &gate : psfp_gates) {
      nlohmann::ordered_json j_gate{
          {"frames", nlohmann::ordered_json::array()}, {"open", gate.open}, {"close", gate.close}};
      for (const auto &frame : gate.frames) {
        j_gate["frames"].push_back(frame.name());
      }
      j["PSFP"][device_name].push_back(std::move(j_gate));
    }
  }

  j = configuration.dump_to_json(topology_, std::move(j));

  return j;
}

template <typename ConfigurationType>
void TSNConfiguration<ConfigurationType>::dump_to_file(const std::filesystem::path &out) const {
  auto json = dump_to_json();
  std::ofstream ofstream(out);
  ofstream << std::setw(4) << json;
}

template struct TSNConfiguration<CriticalPathConfiguration>;
template struct TSNConfiguration<MKFirmConfiguration>;

} // namespace tsndgm
