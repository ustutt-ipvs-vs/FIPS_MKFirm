#include "aggregation.h"
#include "dgm/critical_path.h"
#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include "utils/generator.h"
#include <print>
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

  for (auto link : bottleneck_links(id)) {
    TransmissionOperation *prev = nullptr;
    for (LinkOpPosition i = 0; i < g[link].size();) {
      auto *op = g[link][i];
      if (op->pcp != stream.pcp) {
        continue;
      }

      if (merge_condition(op, prev)) {
        std::println("merge");
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
  std::println("g1:");
  g1 = compress_stream<MERGE_AFTER>(std::move(g1), id);
  std::println("g:");
  g = compress_stream<MERGE_BEFORE>(std::move(g), id);

  // if there's only one feasible option, return that one
  if (g1.is_feasible() && !g.is_feasible()) {
    std::println(" -> choose g1");
    return g1;
  }
  if (!g1.is_feasible() && g.is_feasible()) {
    std::println(" -> choose g");
    return g;
  }

  // otherwise, use makespan as a secondary objective
  auto g1_makespan = g1.critical_path(MAKESPAN)->get_last().objective;
  auto g_makespan = g.critical_path(MAKESPAN)->get_last().objective;
  if (g1_makespan < g_makespan) {
    std::println(" -> choose g1");
    return g1;
  }

  std::println(" -> choose g");
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

template TransmissionGraph
NecessaryQueueingMerge::compress_stream<NecessaryQueueingMerge::MERGE_BEFORE>(TransmissionGraph &&g,
                                                                              StreamId id);
template TransmissionGraph
NecessaryQueueingMerge::compress_stream<NecessaryQueueingMerge::MERGE_AFTER>(TransmissionGraph &&g,
                                                                             StreamId id);

} // namespace tsndgm
