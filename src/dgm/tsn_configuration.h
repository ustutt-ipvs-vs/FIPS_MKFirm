#pragma once

#include "critical_path.h"
#include "network/stream.h"
#include "network/topology.h"
#include "transmission_operations.h"
#include "traversal.h"
#include <deque>
#include <map>
#include <set>

namespace tsndgm {

enum GateState : std::uint8_t { CLOSED, OPEN };

struct PeriodicGateInterval {
  Delay opening_time;
  Delay closing_time;
};

struct PeriodicGate {
  std::vector<Delay> durations;
  GateState initial{CLOSED};
  Delay offset{0};

  auto operator+=(const PeriodicGateInterval &interval) -> PeriodicGate &;
  void extend_to(Delay time);
};

struct PSFPGate {
  std::set<Frame> frames;
  Delay open;
  Delay close;
};

using GCLConfiguration = std::map<std::pair<Link, PCPValue>, PeriodicGate>;
using PSFPConfiguration = std::map<DeviceId, std::deque<PSFPGate>>;
using TalkerConfiguration = std::map<Frame, Delay>;

struct TSNConfiguration {
  GCLConfiguration gcl_config;
  PSFPConfiguration psfp_config;
  TalkerConfiguration talker_config;

  TSNConfiguration() = default;
  explicit TSNConfiguration(DFSTraversal &dfs, const ProcessingOrder &processing_order,
                            const NetworkTopology *topology, Delay hyper_cycle);

  constexpr auto traversal_events() {
    return critical_path_.traversal_events().add(std::make_tuple(std::make_pair(
        DFSVisitor::FINISH_VERTEX, [&](auto v) { return this->visitor_finish_vertex(v); })));
  }

  constexpr auto visitor_finish_vertex(auto visitor) noexcept -> TraversalStatus;

  template <class T> void add_meta_data(const std::string &name, const T &value) {
    meta_data_[name] = value;
  }

  [[nodiscard]] auto dump_to_json() const -> nlohmann::json;
  void dump_to_file(const std::filesystem::path &out) const;

private:
  DFSTraversal *dfs_;
  CriticalPath critical_path_;
  const ProcessingOrder *processing_order_;
  const NetworkTopology *topology_;
  Delay hyper_cycle_;
  nlohmann::json meta_data_;

  void add_talker_entry(const TransmissionOperation &op) noexcept;
  void add_gcl_entry(const TransmissionOperation &op) noexcept;
  void add_psfp_entries(const TransmissionOperation &op) noexcept;
};

} // namespace tsndgm
