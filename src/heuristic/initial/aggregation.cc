#include "aggregation.h"
#include "dgm/critical_path.h"
#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "network/stream.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include "transmission_ordering.h"
#include "utils/generator.h"
#include <algorithm>
#include <cstddef>
#include <limits>
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

NecessaryQueueingMerge::NecessaryQueueingMerge(const StreamStorage *stream_storage,
                                               const NetworkTopology *network,
                                               GlobalObjective objective_type) noexcept
    : stream_storage_(stream_storage), network_(network), objective_type_(objective_type) {}

auto NecessaryQueueingMerge::compress_stream(TransmissionGraph &&g, PrecedenceGraphs *graphs,
                                             StreamId id) noexcept -> TransmissionGraph {
  const auto &stream = stream_storage_->streams[id];

  for (auto link : bottleneck_links(id)) {
    CriticalOperationInfo prev;

    for (size_t pos = 0; pos < g.number_of_transmissions(link); pos++) {
      auto &op = *g[link][pos];
      auto pdb = wireless_packet_delay_budget(op);
      if (!pdb.has_value() || op.pcp != stream.pcp) {
        continue;
      }

      Delay effective_release = 0;
      Delay effective_deadline = std::numeric_limits<Delay>::max();
      for (auto frame : op.frames) {
        StreamId const stream_id = stream_storage_->get_stream_id(frame.stream);
        effective_release = std::max(effective_release, graphs->effective_release(stream_id, link) +
                                                            frame.id * frame.stream->period);
        effective_deadline =
            std::min(effective_deadline,
                     graphs->effective_deadline(stream_id, link) + frame.id * frame.stream->period);
      }
      CriticalOperationInfo const next = {.op = op.id,
                                          .effective_release = effective_release,
                                          .effective_deadline = effective_deadline,
                                          .dejittering = pdb->d_total.max - pdb->d_total.min};

      if (prev.op == SOURCE_ID) {
        prev = next;
        continue;
      }

      if (next.requires_merge(prev) && !next.conflicts(prev)) {
        g.merge({prev.op, next.op});
        prev = next.merge(prev);
        pos--;
      } else {
        prev = next.raise_ceiling(prev);
      }
    }
  }

  return g;
}

auto NecessaryQueueingMerge::bottleneck_links(StreamId id) const noexcept -> Generator<Link> {
  const auto &stream = stream_storage_->streams[id];
  for (auto pair : stream.route.traverse_wireless_hops()) {
    const auto *hop = pair.second;
    for (auto *child : hop->childs) {
      Link link(hop->device->id, child->device->id);
      co_yield link;
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
