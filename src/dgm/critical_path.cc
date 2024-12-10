#include "critical_path.h"
#include "../network/topology.h"
#include "transmission_operations.h"
#include "traversal.h"
#include <cassert>
#include <cstdio>
#include <format>
#include <string>
#include <tuple>
#include <vector>

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
      if (uv_cost > crit_cost_[v->id].cost) {
        crit_cost_[v->id].cost = uv_cost;
        crit_cost_[v->id].pred = u->id;
      }
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

auto CriticalPath::print(const std::vector<VertexInfo> &info,
                         std::string indent, GlobalOpIndex id,
                         GlobalOpIndex parent, bool is_last_child) const
    -> Generator<std::tuple<std::string, GlobalOpIndex, std::string>> {
  if (id == src_->id || crit_cost_[id].pred == parent) {
    std::tuple<std::string, GlobalOpIndex, std::string> e = {
        std::format("{}{}", indent, is_last_child ? "└──" : "├──"), id,
        std::format(" ({})", crit_cost_[id].cost)};
    co_yield e;

    for (auto [child_id, type] : info[id].succ) {
      for (auto e : print(
               info, std::format("{}{}", indent, is_last_child ? "   " : "│  "),
               child_id, id, child_id == std::get<0>(info[id].succ.back()))) {
        co_yield e;
      }
    }
  } else {
    std::tuple<std::string, GlobalOpIndex, std::string> e = {
        std::format("{}{}", indent, is_last_child ? "└──" : "├──"), id, " X"};
    co_yield e;
  }
}

auto CriticalPath::print() const
    -> Generator<std::tuple<std::string, GlobalOpIndex, std::string>> {
  std::vector<VertexInfo> info(crit_cost_.size());

  for (auto visitor : dfs_->traverse<BACKWARD>(sink_)) {
    switch (visitor.event) {
    case DFSVisitor::EXAMINE_EDGE: {
      auto [u, v, type] = std::get<Edge>(visitor.visited_element);
      info[u->id].succ.emplace_back(v->id, type);
      break;
    }
    default:
      break;
    }
  }

  for (auto e : print(info, "", src_->id, 0, true)) {
    co_yield e;
  }
}

} // namespace tsndgm
