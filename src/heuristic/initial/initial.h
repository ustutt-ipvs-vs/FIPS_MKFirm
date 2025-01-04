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
  TransmissionGraph g_wired_;
  TransmissionGraph g_wireless_;

  const StreamStorage *stream_storage_;
  const NetworkTopology *network_;
  PrecedenceGraphs graphs_;
  NecessaryQueueingMerge aggregation_;

  std::vector<const Stream *> feasible_streams_;

  [[nodiscard]] auto
  add_wired_stream(StreamId id,
                   const std::function<Delay(TransmissionGraph &g, GlobalOpIndex id)> &eval =
                       release_time_eval) noexcept -> TransmissionGraph;
  [[nodiscard]] auto
  add_wireless_stream(StreamId id,
                      const std::function<Delay(TransmissionGraph &g, GlobalOpIndex id)> &eval =
                          release_time_eval) noexcept -> TransmissionGraph;
};

struct StrictTemporalIsolationHeuristic {
  TransmissionGraph g;

  StrictTemporalIsolationHeuristic(const StreamStorage *stream_storage,
                                   const NetworkTopology *network) noexcept;

  [[nodiscard]] auto
  add_stream(StreamId id, const std::function<Delay(TransmissionGraph &g, GlobalOpIndex id)> &eval =
                              release_time_eval) noexcept -> bool;

private:
  const StreamStorage *stream_storage_;
  const NetworkTopology *network_;
  PrecedenceGraphs graphs_;

  std::vector<const Stream *> feasible_streams_;
};

} // namespace tsndgm
