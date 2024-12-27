#pragma once

#include "aggregation.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include "transmission_ordering.h"

namespace tsndgm {

struct MergingInitialHeuristic {
  MergingInitialHeuristic(const StreamStorage *stream_storage,
                          const NetworkTopology *network) noexcept;

  [[nodiscard]] auto generate(GlobalObjective objective_type) noexcept -> TransmissionGraph;

private:
  const StreamStorage *stream_storage_;

  NecessaryQueueingMerge wireless_heuristic_;
  IterativeEffectiveRelease wired_heuristic_;
};

} // namespace tsndgm
