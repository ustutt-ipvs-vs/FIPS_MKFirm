#include "critical_path.h"
#include "network/topology.h"
#include "transmission_operations.h"
#include "traversal.h"
#include "utils/generator.h"
#include <cassert>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace tsndgm {

auto CriticalPath::compute(GlobalObjective objective_type) -> std::optional<CriticalPath::Result> {
  auto status = dfs_->traverse<BACKWARD>(sink_, traversal_events());
  if (status == COMPLETED) {
    valid = true;
    return objective(objective_type);
  }

  valid = false;
  last_result_ = {.objective = std::numeric_limits<Delay>::max(), .critical_vertex = SINK_ID};
  return {};
}

auto CriticalPath::objective(GlobalObjective objective_type) -> CriticalPath::Result {
  switch (objective_type) {
  case MAKESPAN:
    last_result_ = {.objective = crit_cost_[SINK_ID].cost, .critical_vertex = SINK_ID};
    return last_result_;
  case PER_FRAME:
    last_result_ = {.objective = 0, .critical_vertex = SINK_ID};
    for (auto *op : sink_->route_pred) {
      auto dmax = op->weights[JOB].outgoing;
      for (auto frame : op->frames) {
        auto dmin = frame.stream->pdb_map.at(op->link()).d_total.min;
        auto arrival_interval =
            DelayInterval(dmin + crit_cost_[op->id].cost, dmax + crit_cost_[op->id].cost);
        auto objective = frame.stream->objective(arrival_interval, frame.id);
        if (objective > last_result_.objective) {
          last_result_ = {.objective = objective, .critical_vertex = op->id};
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
