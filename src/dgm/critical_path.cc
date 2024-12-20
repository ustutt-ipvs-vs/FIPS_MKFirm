#include "critical_path.h"
#include "../network/topology.h"
#include "../utils/generator.h"
#include "transmission_operations.h"
#include "traversal.h"
#include <cassert>
#include <cstdio>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace tsndgm {

constexpr auto CriticalPath::visitor_discover_vertex(auto visitor) noexcept
    -> TraversalStatus {
  auto *v = std::get<Vertex *>(visitor);
  crit_cost_[v->id] = {0, 0};
  return CONTINUE;
}

constexpr auto
CriticalPath::visitor_finish_edge(auto visitor) noexcept -> TraversalStatus {
  auto [u, v, type] = std::get<Edge>(visitor);
  Delay const uv_cost = crit_cost_[u->id].cost + u->weights[type].outgoing +
                        v->weights[type].incoming;
  if (uv_cost > crit_cost_[v->id].cost) {
    crit_cost_[v->id].cost = uv_cost;
    crit_cost_[v->id].pred = u->id;
  }
  return CONTINUE;
}

auto CriticalPath::compute() -> std::optional<CriticalPath::Result> {
  auto status = dfs_->traverse<BACKWARD>(
      sink_,
      DFSEventHandler(
          std::make_pair(DFSVisitor::DISCOVER_VERTEX,
                         [&](auto v) { return visitor_discover_vertex(v); }),
          std::make_pair(DFSVisitor::FINISH_EDGE,
                         [&](auto e) { return visitor_finish_edge(e); }),
          std::make_pair(DFSVisitor::BACK_EDGE,
                         [&](auto /*e*/) { return ABORT; })));

  if (status == COMPLETED) {
    valid = true;
    last_result_ = {crit_cost_[sink_->id].cost, sink_};
    return last_result_;
  }
  valid = false;
  last_result_ = {std::numeric_limits<Delay>::max(), sink_};
  return {};
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
      co_yield print(
          info, std::format("{}{}", indent, is_last_child ? "   " : "│  "),
          child_id, id, child_id == std::get<0>(info[id].succ.back()));
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

  co_yield print(info, "", src_->id, 0, true);
}

} // namespace tsndgm
