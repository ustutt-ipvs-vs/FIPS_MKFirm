#include "aggregation.h"
#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include "utils/generator.h"
#include <utility>

namespace tsndgm {

NecessaryQueueingMerge::NecessaryQueueingMerge(const StreamStorage *stream_storage,
                                               const NetworkTopology *network) noexcept
    : stream_storage_(stream_storage), network_(network) {}

template <NecessaryQueueingMerge::MergePolicy P>
auto NecessaryQueueingMerge::compress_stream(TransmissionGraph &&g,
                                             StreamId id) noexcept -> TransmissionGraph {
  const auto &stream = stream_storage_->streams[id];

  auto merge_condition = [&stream](auto op, auto prev) {
    if constexpr (P == MERGE_BEFORE) {
      return op->contains(&stream) && prev != nullptr;
    } else {
      return !op->contains(&stream) && prev != nullptr;
    }
  };
  auto update_condition = [&stream](auto op) {
    if constexpr (P == MERGE_BEFORE) {
      return !op->contains(&stream);
    } else {
      return op->contains(&stream);
    }
  };

  for (auto link : wireless_links(id)) {
    TransmissionOperation *prev = nullptr;
    for (LinkOpPosition i = 0; i < g[link].size();) {
      auto *op = g[link][i];
      if (op->pcp != stream.pcp) {
        continue;
      }

      if (merge_condition(op, prev)) {
        g.merge({prev->id, op->id});
        prev = nullptr;
        if (g.is_feasible()) {
          return g;
        }
      } else if (update_condition(op)) {
        prev = op;
        i++;
      } else {
        i++;
      }
    }
  }

  return g;
}

auto NecessaryQueueingMerge::compress_stream(TransmissionGraph &&g,
                                             StreamId id) noexcept -> TransmissionGraph {
  TransmissionGraph g1 = g;
  g1 = compress_stream<MERGE_AFTER>(std::move(g1), id);
  if (g1.is_feasible()) {
    return g1;
  }
  g = compress_stream<MERGE_BEFORE>(std::move(g), id);
  return g;
}

auto NecessaryQueueingMerge::wireless_links(StreamId id) const noexcept -> Generator<Link> {
  const auto &stream = stream_storage_->streams[id];
  for (auto [hop1, hop2] : stream.route.traverse_wireless_hops()) {
    Link link(hop1->device->id, hop2->device->id);
    co_yield link;
  }
}

template TransmissionGraph
NecessaryQueueingMerge::compress_stream<NecessaryQueueingMerge::MERGE_BEFORE>(TransmissionGraph &&g,
                                                                              StreamId id);
template TransmissionGraph
NecessaryQueueingMerge::compress_stream<NecessaryQueueingMerge::MERGE_AFTER>(TransmissionGraph &&g,
                                                                             StreamId id);

} // namespace tsndgm
