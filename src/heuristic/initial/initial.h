#pragma once

#include "aggregation.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include "transmission_ordering.h"

namespace tsndgm {

[[maybe_unused]] constexpr auto release_time_eval = [](TransmissionGraph &g, GlobalOpIndex id) {
  return (*g.critical_path())[id].cost;
};

struct IncrementalHeuristic {
  TransmissionGraph g;

  IncrementalHeuristic(const StreamStorage *stream_storage,
                       const NetworkTopology *network) noexcept;

  [[nodiscard]] auto
  add_stream(StreamId id, const std::function<Delay(TransmissionGraph &g, GlobalOpIndex id)> &eval =
                              release_time_eval) noexcept -> bool;

private:
  const StreamStorage *stream_storage_;
  const NetworkTopology *network_;
  PrecedenceGraphs graphs_;
  NecessaryQueueingMerge aggregation_;

  std::vector<const Stream *> feasible_streams_;

  [[nodiscard]] static auto check_feasibility(TransmissionGraph &g_new) noexcept -> bool;
};

} // namespace tsndgm
