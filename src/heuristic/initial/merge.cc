#include "merge.h"
#include "dgm/critical_path.h"
#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "network/stream.h"
#include "network/topology.h"
#include <cassert>
#include <functional>
#include <map>
#include <ranges>
#include <utility>
#include <vector>

namespace tsndgm {

auto TransmissionGraphMerger::generate(GlobalObjective objective_type) noexcept
    -> TransmissionGraph {
  std::map<std::pair<Delay, Frame>, TransmissionOperation> order;

  for (auto *g : {&first, &second}) {
    for (GlobalOpIndex op_id = SINK_ID + 1; op_id < g->size(); op_id++) {
      auto [op, _] = (*g)[op_id];
      if (!op->valid()) {
        continue;
      }
      Delay const d = eval_(*g, op_id);
      order.insert({{d, *op->frames.begin()}, *op});
    }
  }

  std::vector<TransmissionOperation> initial(order.size() + 2);
  initial[0] = {.id = SOURCE_ID};
  initial[1] = {.id = SINK_ID};
  for (auto [i, entry] : std::views::enumerate(order)) {
    entry.second.id = SINK_ID + i + 1;
    initial[SINK_ID + i + 1] = std::move(entry.second);
  }

  return TransmissionGraph(stream_storage_, std::move(initial), objective_type,
                           merged_stream_filter_);
}

} // namespace tsndgm
