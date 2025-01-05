#include "critical_path.h"
#include "network/topology.h"
#include "transmission_operations.h"
#include "traversal.h"
#include "utils/generator.h"
#include <cassert>
#include <cstdio>
#include <format>
#include <limits>
#include <optional>
#include <print>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace tsndgm {

constexpr auto CriticalPath::visitor_discover_vertex(auto visitor) noexcept -> TraversalStatus {
  auto *v = std::get<Vertex *>(visitor);
  crit_cost_[v->id] = {0, 0};
  return CONTINUE;
}

constexpr auto CriticalPath::visitor_finish_edge(auto visitor) noexcept -> TraversalStatus {
  auto [u, v, type] = std::get<Edge>(visitor);
  Delay const uv_cost =
      crit_cost_[u->id].cost + u->weights[type].outgoing + v->weights[type].incoming;
  if (uv_cost > crit_cost_[v->id].cost) {
    crit_cost_[v->id].cost = uv_cost;
    crit_cost_[v->id].pred = u->id;
  }
  return CONTINUE;
}

constexpr auto CriticalPath::visitor_tree_edge(auto visitor) noexcept -> TraversalStatus {
  auto [u, v, type] = std::get<Edge>(visitor);
  cycle_pred_[v->id] = {.cost = u->weights[type].outgoing + v->weights[type].incoming,
                        .pred = u->id};
  return CONTINUE;
}

constexpr auto CriticalPath::visitor_back_edge(auto visitor) noexcept -> TraversalStatus {
  auto [u, v, type] = std::get<Edge>(visitor);
  auto w = u->id;
  auto cost = 0;
  while (w != v->id) {
    auto &op = (*processing_order_)[w];
    auto f = *op.frames.begin();
    std::println("{} [{},{}], {}#{}", op.id, op.source->name, op.target->name, f.stream->name,
                 f.id);
    cost += cycle_pred_[w].cost;
    w = cycle_pred_[w].pred;
  }
  auto &op = (*processing_order_)[w];
  auto f = *op.frames.begin();
  std::println("{} [{},{}], {}#{}", op.id, op.source->name, op.target->name, f.stream->name, f.id);
  return ABORT;
}

auto CriticalPath::compute(GlobalObjective objective_type) -> std::optional<CriticalPath::Result> {
  auto status = dfs_->traverse<BACKWARD>(
      sink_,
      DFSEventHandler(
          std::make_pair(DFSVisitor::DISCOVER_VERTEX,
                         [&](auto v) { return visitor_discover_vertex(v); }),
          std::make_pair(DFSVisitor::TREE_EDGE, [&](auto e) { return visitor_tree_edge(e); }),
          std::make_pair(DFSVisitor::FINISH_EDGE, [&](auto e) { return visitor_finish_edge(e); }),
          std::make_pair(DFSVisitor::BACK_EDGE, [&](auto e) { return visitor_back_edge(e); })));

  if (status == COMPLETED) {
    valid = true;
    return objective(objective_type);
  }

  valid = false;
  last_result_ = {std::numeric_limits<Delay>::max(), SINK_ID};
  return {};
}

auto CriticalPath::objective(GlobalObjective objective_type) -> CriticalPath::Result {
  switch (objective_type) {
  case MAKESPAN:
    last_result_ = {crit_cost_[SINK_ID].cost, SINK_ID};
    return last_result_;
  case PER_FRAME:
    last_result_ = {0, SINK_ID};
    for (auto *op : sink_->route_pred) {
      auto d = op->weights[JOB].outgoing;
      for (auto frame : op->frames) {
        auto objective = frame.stream->objective(crit_cost_[op->id].cost + d, frame.id);
        if (objective > last_result_.objective) {
          last_result_ = {objective, op->id};
        }
      }
    }
    return last_result_;
  }
  std::unreachable();
}

auto CriticalPath::traverse_operations() const
    -> Generator<std::pair<const TransmissionOperation *, Delay>> {
  if (!valid) {
    co_return;
  }

  auto op_id = last_result_.critical_vertex;
  std::pair<const TransmissionOperation *, Delay> val;
  while (op_id != SOURCE_ID) {
    val = {&(*processing_order_)[op_id], crit_cost_[op_id].cost};
    co_yield val;
    op_id = crit_cost_[op_id].pred;
  }
  val = {&(*processing_order_)[SOURCE_ID], crit_cost_[SOURCE_ID].cost};
  co_yield val;
}

auto CriticalPath::print(const std::vector<VertexInfo> &info, std::string indent, GlobalOpIndex id,
                         GlobalOpIndex parent, bool is_last_child) const
    -> Generator<std::tuple<std::string, GlobalOpIndex, std::string>> {
  if (id == src_->id || crit_cost_[id].pred == parent) {
    std::tuple<std::string, GlobalOpIndex, std::string> e = {
        std::format("{}{}", indent, is_last_child ? "└──" : "├──"), id,
        std::format(" ({})", crit_cost_[id].cost)};
    co_yield e;

    for (auto [child_id, type] : info[id].succ) {
      co_yield print(info, std::format("{}{}", indent, is_last_child ? "   " : "│  "), child_id, id,
                     child_id == std::get<0>(info[id].succ.back()));
    }
  } else {
    std::tuple<std::string, GlobalOpIndex, std::string> e = {
        std::format("{}{}", indent, is_last_child ? "└──" : "├──"), id, " X"};
    co_yield e;
  }
}

auto CriticalPath::print() const -> Generator<std::tuple<std::string, GlobalOpIndex, std::string>> {
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
