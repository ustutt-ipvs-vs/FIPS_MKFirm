#include "initial.h"
#include "dgm/critical_path.h"
#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "merge.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include <utility>

namespace tsndgm {

MergingInitialHeuristic::MergingInitialHeuristic(const StreamStorage *stream_storage,
                                                 const NetworkTopology *network) noexcept
    : stream_storage_(stream_storage),
      wireless_heuristic_(stream_storage, network, wireless_stream_filter),
      wired_heuristic_(stream_storage, network, wired_stream_filter) {}

auto MergingInitialHeuristic::generate(GlobalObjective objective_type) noexcept
    -> TransmissionGraph {
  auto g1 = wireless_heuristic_.generate(objective_type);
  auto g2 = wired_heuristic_.generate(objective_type);

  auto crit_cost_eval = [](TransmissionGraph &g, GlobalOpIndex id) {
    return (*g.critical_path())[id].cost;
  };

  return TransmissionGraphMerger(stream_storage_, std::move(g1), std::move(g2),
                                 default_stream_filter, crit_cost_eval)
      .generate(objective_type);
}

} // namespace tsndgm
