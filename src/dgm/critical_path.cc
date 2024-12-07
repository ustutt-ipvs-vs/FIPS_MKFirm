#include "critical_path.h"
#include "../network/topology.h"
#include "traversal.h"
#include <algorithm>
#include <cassert>
#include <cstdio>

namespace tsndgm {

auto CriticalPath::compute() -> const CriticalPath::Result & {
  for (auto visitor : dfs_->traverse<BACKWARD>(sink_)) {
    switch (visitor.event) {
    case DFSVisitor::DISCOVER_VERTEX: {
      Vertex *v = std::get<Vertex *>(visitor.visited_element);
      crit_cost_[v->id] = {0, 0};
      break;
    }
    case DFSVisitor::FINISH_EDGE: {
      auto [u, v, type] = std::get<Edge>(visitor.visited_element);
      Delay const uv_cost = crit_cost_[u->id].cost + u->weights[type].outgoing +
                            v->weights[type].incoming;
      crit_cost_[v->id].cost = std::max(crit_cost_[v->id].cost, uv_cost);
      break;
    }
    case DFSVisitor::BACK_EDGE:
      assert(false);
    default:
      break;
    }
  }

  last_result_ = {crit_cost_[sink_->id].cost, sink_};
  return last_result_;
}

} // namespace tsndgm
