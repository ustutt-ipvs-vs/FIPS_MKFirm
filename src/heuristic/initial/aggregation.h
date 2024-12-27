#pragma once

#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "network/stream_storage.h"
#include "transmission_ordering.h"

namespace tsndgm {

struct NecessaryQueueingMerge {
  struct CriticalOperationInfo {
    GlobalOpIndex op;
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

  PrecedenceGraphs graphs;

  NecessaryQueueingMerge(
      const StreamStorage *stream_storage, const NetworkTopology *network,
      const std::function<bool(const Stream &)> &stream_filter = default_stream_filter) noexcept;

  [[nodiscard]] auto generate(GlobalObjective objective_type) noexcept -> TransmissionGraph;

private:
  const StreamStorage *stream_storage_;
  const NetworkTopology *network_;
  std::function<bool(const Stream &)> stream_filter_;
  TransmissionGraph g_;

  [[nodiscard]] auto bottleneck_links() const noexcept -> Generator<Link>;
  [[nodiscard]] auto wireless_packet_delay_budget(const TransmissionOperation &op) const noexcept
      -> std::optional<PacketDelayBudget>;
};

} // namespace tsndgm
