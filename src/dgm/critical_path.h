#pragma once

#include "traversal.h"
#include <print>

namespace tsndgm {

enum GlobalObjective : std::uint8_t { MAKESPAN, PER_FRAME };

struct CriticalPath {
  struct Result {
    Delay objective;
    GlobalOpIndex critical_vertex;
  };
  struct CriticalCost {
    Delay cost;
    GlobalOpIndex pred;
  };

  bool valid{false};

  CriticalPath() = default;
#ifndef NDEBUG
  CriticalPath(DFSTraversal &dfs, const ProcessingOrder &processing_order)
      : dfs_(&dfs), processing_order_(&processing_order), src_(&processing_order.src()),

        sink_(&processing_order.sink()), crit_cost_(processing_order.total_operations),
        cycle_pred_(processing_order.total_operations) {};
#else
  CriticalPath(DFSTraversal &dfs, const ProcessingOrder &processing_order)
      : dfs_(&dfs), processing_order_(&processing_order), src_(&processing_order.src()),

        sink_(&processing_order.sink()), crit_cost_(processing_order.total_operations) {};
#endif

  CriticalPath(CriticalPath &&) = default;
  CriticalPath(const CriticalPath &) = default;
  auto operator=(CriticalPath &&) -> CriticalPath & = default;
  auto operator=(const CriticalPath &) -> CriticalPath & = default;
  ~CriticalPath() = default;

  auto compute(GlobalObjective objective_type) -> std::optional<Result>;
  [[nodiscard]] auto get_last() const -> Result { return last_result_; }

  [[nodiscard]] auto
  traverse_operations() const -> Generator<std::pair<const TransmissionOperation *, Delay>>;
  [[nodiscard]] auto
  print() const -> Generator<std::tuple<std::string, GlobalOpIndex, std::string>>;

  auto operator[](GlobalOpIndex id) const noexcept -> CriticalCost { return crit_cost_[id]; }

  constexpr auto traversal_events() {
    return DFSEventHandler(
        std::make_pair(DFSVisitor::DISCOVER_VERTEX,
                       [&](auto v) { return this->visitor_discover_vertex(v); }),
        std::make_pair(DFSVisitor::FINISH_EDGE,
                       [&](auto e) { return this->visitor_finish_edge(e); }),
        std::make_pair(DFSVisitor::TREE_EDGE, [&](auto e) { return this->visitor_tree_edge(e); }),
        std::make_pair(DFSVisitor::BACK_EDGE, [&](auto e) { return this->visitor_back_edge(e); }));
  }

private:
  DFSTraversal *dfs_;
  const ProcessingOrder *processing_order_;
  const Vertex *src_, *sink_;
  Result last_result_;

  std::vector<CriticalCost> crit_cost_;
  std::vector<CriticalCost> cycle_pred_;

  struct VertexInfo {
    std::vector<std::pair<GlobalOpIndex, EdgeType>> succ;
    std::string indent;
  };

  [[nodiscard]] auto
  print(const std::vector<VertexInfo> &info, std::string indent, GlobalOpIndex id,
        GlobalOpIndex parent,
        bool is_last_child) const -> Generator<std::tuple<std::string, GlobalOpIndex, std::string>>;
  [[nodiscard]] auto objective(GlobalObjective objective_type) -> Result;

  constexpr auto visitor_discover_vertex(auto visitor) noexcept -> TraversalStatus {
    auto *v = std::get<Vertex *>(visitor);
    crit_cost_[v->id] = {0, 0};
    return CONTINUE;
  }
  constexpr auto visitor_finish_edge(auto visitor) noexcept -> TraversalStatus {
    auto [u, v, type] = std::get<Edge>(visitor);
    Delay const uv_cost =
        crit_cost_[u->id].cost + u->weights[type].outgoing + v->weights[type].incoming;
    if (uv_cost > crit_cost_[v->id].cost) {
      crit_cost_[v->id].cost = uv_cost;
      crit_cost_[v->id].pred = u->id;
    }
    return CONTINUE;
  }
  constexpr auto visitor_back_edge(auto visitor) noexcept -> TraversalStatus {
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
    std::println("{} [{},{}], {}#{}", op.id, op.source->name, op.target->name, f.stream->name,
                 f.id);
    return ABORT;
  }
  constexpr auto visitor_tree_edge(auto visitor) noexcept -> TraversalStatus {
    auto [u, v, type] = std::get<Edge>(visitor);
    cycle_pred_[v->id] = {.cost = u->weights[type].outgoing + v->weights[type].incoming,
                          .pred = u->id};
    return CONTINUE;
  }
};

} // namespace tsndgm
