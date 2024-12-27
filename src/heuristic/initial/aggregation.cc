#include "aggregation.h"
#include "dgm/critical_path.h"
#include "dgm/transmission_graph.h"
#include "network/stream.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include "transmission_ordering.h"
#include "utils/generator.h"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>

namespace tsndgm {

auto NecessaryQueueingMerge::CriticalOperationInfo::requires_merge(
    const CriticalOperationInfo &other) const noexcept -> bool {
  return other.effective_release + dejittering > static_cast<Delay>(0.8 * effective_deadline);
}

auto NecessaryQueueingMerge::CriticalOperationInfo::conflicts(
    const CriticalOperationInfo &other) const noexcept -> bool {
  return effective_release > other.effective_deadline;
}

auto NecessaryQueueingMerge::CriticalOperationInfo::merge(
    const CriticalOperationInfo &other) const noexcept -> CriticalOperationInfo {
  return {.op = other.op,
          .effective_release = std::max(effective_release, other.effective_release),
          .effective_deadline = std::min(effective_deadline, other.effective_deadline),
          .dejittering = std::max(dejittering, other.dejittering)};
}

auto NecessaryQueueingMerge::CriticalOperationInfo::raise_ceiling(
    const CriticalOperationInfo &other) const noexcept -> CriticalOperationInfo {
  return {.op = op,
          .effective_release = std::max(effective_release, other.effective_release + dejittering),
          .effective_deadline = effective_deadline,
          .dejittering = dejittering};
}

NecessaryQueueingMerge::NecessaryQueueingMerge(
    const StreamStorage *stream_storage, const NetworkTopology *network,
    const std::function<bool(const Stream &)> &stream_filter) noexcept
    : graphs(stream_storage, stream_filter), stream_storage_(stream_storage), network_(network),
      stream_filter_(stream_filter) {}

auto NecessaryQueueingMerge::generate(GlobalObjective objective_type) noexcept
    -> TransmissionGraph {
  IterativeEffectiveRelease heuristic(stream_storage_, network_, stream_filter_);
  g_ = heuristic.generate(objective_type);

  for (auto link : bottleneck_links()) {
    std::map<PCPValue, CriticalOperationInfo> previous_operation;
    for (size_t pos = 0; pos < g_.number_of_transmissions(link); pos++) {
      auto &op = *g_[link][pos];
      auto pdb = wireless_packet_delay_budget(op);
      if (!pdb.has_value()) {
        continue;
      }

      StreamId const stream_id = stream_storage_->get_stream_id(op.frames.begin()->stream);
      CriticalOperationInfo const next = {
          .op = op.id,
          .effective_release = graphs.effective_release(stream_id, link),
          .effective_deadline = graphs.effective_deadline(stream_id, link),
          .dejittering = pdb->d_total.max - pdb->d_total.min};
      if (!previous_operation.contains(op.pcp)) {
        previous_operation.insert({op.pcp, next});
        continue;
      }

      CriticalOperationInfo &prev = previous_operation[op.pcp];
      if (next.requires_merge(prev) && !next.conflicts(prev)) {
        g_.merge({prev.op, next.op});
        prev = next.merge(prev);
        pos--;
      } else {
        prev = next.raise_ceiling(prev);
      }
    }
  }

  return g_;
}

auto NecessaryQueueingMerge::bottleneck_links() const noexcept -> Generator<Link> {
  for (const auto *device : network_->devices()) {
    if (device->type == TSN_TRANSLATOR) {
      for (const auto &data_link : device->out) {
        co_yield static_cast<Link>(data_link);
      };
    }
  }
}

auto NecessaryQueueingMerge::wireless_packet_delay_budget(
    const TransmissionOperation &op) const noexcept -> std::optional<PacketDelayBudget> {
  for (auto *pred : op.route_pred) {
    if ((*network_)[pred->link()].type == WIRELESS) {
      return pred->weights.pdb;
    }
  }
  return {};
}

} // namespace tsndgm
