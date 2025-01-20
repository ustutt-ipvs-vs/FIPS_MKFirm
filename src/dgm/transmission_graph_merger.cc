#include "transmission_graph_merger.h"
#include "dgm/critical_path.h"
#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "network/topology.h"
#include <cassert>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace tsndgm {

auto TransmissionGraphMerger::generate(GlobalObjective objective_type) noexcept
    -> TransmissionGraph {
  auto order = compute_ordering();
  auto initial = compute_initial(order);
  auto old_to_new = compute_old_to_new_mapping(&first, order);

  return consistent_merge(objective_type, std::move(initial), old_to_new);
}

auto TransmissionGraphMerger::consistent_merge(GlobalObjective objective_type,
                                               std::vector<TransmissionOperation> &&initial,
                                               const std::vector<GlobalOpIndex> &selection)
    -> TransmissionGraph {
  TransmissionGraph g(stream_storage_, topology_, std::move(initial), objective_type,
                      merged_stream_filter_);

  for (auto op_id : selection) {
    if (op_id <= SINK_ID) {
      continue;
    }

    auto [op1, pos1] = g[op_id];
    auto transmissions = g[op1->link()];

    std::optional<FlipInstruction> req_flip;
    for (LinkOpPosition pos2 = pos1 + 1; pos2 < transmissions.size(); pos2++) {
      auto *op2 = transmissions[pos2];
      if (op1->pcp != op2->pcp) {
        continue;
      }

      for (auto pair : g.related_neighbor_pairs({op1->id, op2->id})) {
        if (g[pair.first].second > g[pair.second].second) {
          req_flip = {.link = op1->link(), .op_id = op1->id, .new_pos = pos2};
        }
      }
    }
    if (req_flip.has_value()) {
      g.flip(*req_flip);
    }
  }

  return g;
}

auto TransmissionGraphMerger::compute_ordering() noexcept -> Ordering {
  Ordering order;
  for (auto *g : {&first, &second}) {
    for (GlobalOpIndex op_id = SINK_ID + 1; op_id < g->size(); op_id++) {
      auto [op, _] = (*g)[op_id];
      if (!op->valid()) {
        continue;
      }
      Delay const d = eval_(*g, op_id);
      order.insert({{d, *op->frames.begin(), op->id}, {*op, g}});
    }
  }
  return order;
}

auto TransmissionGraphMerger::compute_initial(const Ordering &order) noexcept
    -> std::vector<TransmissionOperation> {
  std::vector<TransmissionOperation> initial(order.size() + 2);
  initial[0] = {.id = SOURCE_ID};
  initial[1] = {.id = SINK_ID};
  for (auto [i, entry] : std::views::enumerate(order)) {
    auto [op, g] = entry.second;
    auto new_id = SINK_ID + i + 1;
    op.id = new_id;
    initial[new_id] = std::move(op);
  }
  return initial;
}

auto TransmissionGraphMerger::compute_old_to_new_mapping(const TransmissionGraph *g,
                                                         const Ordering &order) noexcept
    -> std::vector<GlobalOpIndex> {
  std::vector<GlobalOpIndex> old_to_new(g->size());
  for (auto [i, entry] : std::views::enumerate(order)) {
    auto [op, g_ptr] = entry.second;
    if (g_ptr == g) {
      old_to_new[op.id] = SINK_ID + i + 1;
    }
  }
  return old_to_new;
}

} // namespace tsndgm
