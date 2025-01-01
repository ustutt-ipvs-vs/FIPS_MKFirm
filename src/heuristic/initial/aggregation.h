#pragma once

#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "network/stream_storage.h"
#include "transmission_ordering.h"

namespace tsndgm {

struct NecessaryQueueingMerge {
  struct CriticalOperationInfo {
    GlobalOpIndex op{SOURCE_ID};
    Delay effective_release;
    Delay effective_deadline;
    Delay dejittering;

    [[nodiscard]] auto requires_merge(const CriticalOperationInfo &other) const noexcept -> bool;
    [[nodiscard]] auto conflicts(const CriticalOperationInfo &other) const noexcept -> bool;
    [[nodiscard]] auto
    merge(const CriticalOperationInfo &other) const noexcept -> CriticalOperationInfo;
    [[nodiscard]] auto
    raise_ceiling(const CriticalOperationInfo &other) const noexcept -> CriticalOperationInfo;
  };

  NecessaryQueueingMerge(const StreamStorage *stream_storage, const NetworkTopology *network,
                         GlobalObjective objective_type) noexcept;

  [[nodiscard]] auto compress_stream(TransmissionGraph &&g,
                                     StreamId id) noexcept -> TransmissionGraph;

private:
  const StreamStorage *stream_storage_;
  const NetworkTopology *network_;
  GlobalObjective objective_type_;

  [[nodiscard]] auto wireless_packet_delay_budget(const TransmissionOperation &op) const noexcept
      -> std::optional<PacketDelayBudget>;
};

} // namespace tsndgm
