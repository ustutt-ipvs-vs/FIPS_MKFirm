#include "transmission_graph.h"
#include "critical_path.h"
#include "network/histogram.h"
#include "network/stream.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include "transmission_operations.h"
#include "traversal.h"
#include "utils/generator.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <deque>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace tsndgm {

TransmissionGraph::TransmissionGraph(
    const StreamStorage *stream_storage, GlobalObjective objective_type,
    const InitialTransmissionOrder &initial,
    const std::function<bool(const Stream &)> &stream_filter) noexcept
    : objective_type(objective_type), stream_storage_(stream_storage),
      stream_filter_(stream_filter) {
  processing_order_.operations.reserve(stream_storage->number_of_transmissions(stream_filter_));

  if (initial.empty()) {
    for (auto frame : stream_storage->frames(stream_filter_)) {
      add_frame(frame);
    }
  } else {
    for (auto [frame, port] : initial) {
      add_operation(frame, port);
    }
  }

  for (auto &op : processing_order_.operations | std::views::drop(2)) {
    auto link = op.link();
    processing_order_[link].push_back(&op);
  }
  for (const auto &stream : stream_storage_->filtered_streams(stream_filter_)) {
    connect_precedence_constraints(stream);
  }
  rebuild();
}

TransmissionGraph::TransmissionGraph(
    const StreamStorage *stream_storage, std::vector<TransmissionOperation> &&initial,
    GlobalObjective objective_type,
    const std::function<bool(const Stream &)> &stream_filter) noexcept
    : objective_type(objective_type), stream_storage_(stream_storage),
      stream_filter_(stream_filter) {
  std::swap(processing_order_.operations, initial);
  processing_order_.total_operations = processing_order_.operations.size();

  for (auto [i, op] : std::views::enumerate(processing_order_.operations)) {
    op.id = i;
    op.route_pred.clear();
    op.route_succ.clear();
    if (SINK_ID < op.id) {
      auto link = op.link();
      processing_order_[link].push_back(&op);
    }
  }
  for (const auto &stream : stream_storage_->filtered_streams(stream_filter_)) {
    connect_precedence_constraints(stream);
  }
  rebuild();
}

// auto TransmissionGraph::consistent_build(
//     const StreamStorage *stream_storage, std::vector<TransmissionOperation> &&initial,
//     GlobalObjective objective_type, const std::function<bool(const Stream &)> &stream_filter,
//     const std::function<int(const TransmissionOperation &)> &priority_filter) noexcept
//     -> TransmissionGraph {
//   auto g = TransmissionGraph(stream_storage, std::move(initial), objective_type, stream_filter);
//
//   PartialOrder consistency(g.size());
//   for (GlobalOpIndex op_id = SINK_ID + 1; op_id < g.size(); op_id++) {
//     auto [op, pos] = g[op_id];
//     for (auto *pred : op->route_pred) {
//       consistency[op->id].insert(pred->id);
//     }
//
//     Link const link = op->link();
//     for (LinkOpPosition i = pos; i > 0; i--) {
//       auto *prev_op = g[link][i - 1];
//       auto related_edges = g.equivalence_class({prev_op->id, op->id});
//       if (std::ranges::any_of(related_edges, [](auto &pair) { return pair.first > pair.second; })
//       &&
//           priority_filter(*op) > priority_filter(*prev_op)) {
//         // inconsistent selection, and op should take precendence
//         for (auto &pair : related_edges) {
//           consistency[pair.first].insert(pair.second);
//         }
//       }
//     }
//   }
//
//   auto sorted = topological_sort(g.processing_order_.operations, consistency).collect(g.size());
//   auto g_new = TransmissionGraph(stream_storage, std::move(sorted), objective_type,
//   stream_filter); assert(g_new.check_consistency()); return g_new;
// }

TransmissionGraph::TransmissionGraph(const TransmissionGraph &other) noexcept
    : objective_type(other.objective_type), processing_order_(other.processing_order_),
      stream_storage_(other.stream_storage_), flip_log_(other.flip_log_),
      stream_filter_(other.stream_filter_) {
  rebuild();
}

TransmissionGraph::TransmissionGraph(TransmissionGraph &&other) noexcept
    : objective_type(other.objective_type), processing_order_(std::move(other.processing_order_)),
      stream_storage_(other.stream_storage_), flip_log_(std::move(other.flip_log_)),
      stream_filter_(std::move(other.stream_filter_)) {
  rebuild();
}

auto TransmissionGraph::operator=(const TransmissionGraph &other) noexcept -> TransmissionGraph & {
  if (this == &other) {
    return *this;
  }

  objective_type = other.objective_type;
  processing_order_ = other.processing_order_;
  stream_storage_ = other.stream_storage_;
  flip_log_ = other.flip_log_;
  stream_filter_ = other.stream_filter_;
  rebuild();
  return *this;
}

auto TransmissionGraph::operator=(TransmissionGraph &&other) noexcept -> TransmissionGraph & {
  if (this == &other) {
    return *this;
  }

  objective_type = other.objective_type;
  std::swap(processing_order_, other.processing_order_);
  stream_storage_ = other.stream_storage_;
  std::swap(flip_log_, other.flip_log_);
  std::swap(stream_filter_, other.stream_filter_);
  rebuild();
  return *this;
}

auto TransmissionGraph::is_feasible() -> bool {
  const auto *crit_path = critical_path();
  switch (objective_type) {
  case MAKESPAN:
    return crit_path != nullptr;
  case PER_FRAME:
    return crit_path != nullptr && crit_path->get_last().objective <= 0;
  }
  std::unreachable();
}

auto TransmissionGraph::critical_path() -> const CriticalPath * {
  if (critical_path_.valid || critical_path_.compute(objective_type).has_value()) {
    return &critical_path_;
  }
  return nullptr;
}

template <TraversalDirection D> auto TransmissionGraph::traverse() -> Generator<DFSVisitor> {
  if constexpr (D == FORWARD) {
    co_yield dfs_.traverse<FORWARD>(&processing_order_.src());
  } else {
    co_yield dfs_.traverse<BACKWARD>(&processing_order_.sink());
  }
}

void TransmissionGraph::flip(const FlipInstruction &inst) noexcept {
  critical_path_.valid = false;
  flip_log_.clear();
  consistent_flip(inst);
}

void TransmissionGraph::flip(GlobalOpIndex op_id, LinkOpPosition new_pos) noexcept {
  auto &op = processing_order_[op_id];
  Link const link = {op.source->id, op.target->id};
  flip({link, op_id, new_pos});
}

void TransmissionGraph::undo_last_flip() noexcept {
  for (auto [id, pos] : flip_log_) {
    auto link = processing_order_[id].link();
    processing_order_[link][pos] = &processing_order_[id];
    std::get<1>(position_[id]) = pos;
  }
  flip_log_.clear();
}

void TransmissionGraph::merge(MergeInstruction inst) noexcept {
  assert(position_[inst.first].first == position_[inst.second].first);
  assert(processing_order_[inst.first].pcp == processing_order_[inst.second].pcp);

  // w.l.o.g., inst.first should come before inst.second
  if (position_[inst.first].second > position_[inst.second].second) {
    inst = {inst.second, inst.first};
  }

  // move inst.second right after inst.first
  auto related_edges = equivalence_class(inst);
  std::map<std::pair<Link, GlobalOpIndex>, LinkOpPosition> req_flips;
  for (auto &pair : related_edges) {
    req_flips.insert(
        {{processing_order_[pair.first].link(), pair.second}, position_[pair.first].second + 1});
  }
  consistent_flip<MOVE_BEFORE>(std::move(req_flips));
  assert(check_consistency());
  assert(std::ranges::all_of(
      req_flips, [&](auto &e) { return position_[e.first.second].second == e.second; }));

  // merge transmission operations (inst.first is kept)
  for (auto &pair : related_edges) {
    auto &op1 = processing_order_[pair.first];
    auto &op2 = processing_order_[pair.second];

    op1.weights.merge(op2.weights);
    op1.frames.merge(op2.frames);

    delete_merged_neighbors(related_edges, op2.route_pred);
    relink_job_predecessors(&op2, &op1);
    delete_merged_neighbors(related_edges, op2.route_succ);
    relink_job_successors(&op2, &op1);
  }

  // delete inst.second (not from processing_order_.operations container
  // to avoid invalidating all pointers; we don't want to rebuild
  // the entire DGM...)
  for (auto &pair : related_edges) {
    auto [transmission, pos] = position_[pair.second];
    for (auto *op : *transmission | std::views::drop(pos)) {
      position_[op->id].second--;
    }
    transmission->erase(transmission->begin() + static_cast<std::ptrdiff_t>(pos));
    position_[pair.second].second = transmission->size();
  }
}

void TransmissionGraph::rebuild() {
  position_.resize(processing_order_.total_operations);
  for (auto link : std::views::keys(processing_order_.map)) {
    recompute_positions(link);
  }
  dfs_ = DFSTraversal(&processing_order_, &position_);
  critical_path_ = CriticalPath(&dfs_, processing_order_);
}

void TransmissionGraph::add_frame(Frame frame) noexcept {
  for (auto port : frame.stream->route.traverse_hops()) {
    add_operation(frame, port);
  }
}

void TransmissionGraph::add_operation(Frame frame, RouteHopLink port) noexcept {
  auto [stream, f] = frame;
  auto [source, target] = port;
  Link const link(source->device->id, target->device->id);

  const PDB &pdb = stream->pdb_map.at(link);

  TransmissionWeights weights = TransmissionWeights::from_pdb(pdb);
  if (source->is_talker()) {
    weights.job.incoming = f * stream->period + stream->phase;
  }

  GlobalOpIndex const id = processing_order_.total_operations++;
  processing_order_.operations.push_back({.id = id,
                                          .source = source->device,
                                          .target = target->device,
                                          .frames = {frame},
                                          .pcp = stream->pcp,
                                          .weights = weights});
}

void TransmissionGraph::connect_precedence_constraints(const Stream &stream) {
  auto get_next_occurence = [&](auto it, auto link, auto f) {
    return std::find_if(it, processing_order_[link].end(), [&](TransmissionOperation *op) {
      return op->frames.contains(Frame(&stream, f));
    });
  };

  // Connect stream operations via precedence constraints
  for (auto [link12, link23] : stream.route.traverse_consecutive_links()) {
    auto op1_it = processing_order_[link12].begin();
    auto op2_it = processing_order_[link23].begin();
    for (auto f : stream.frames(stream_storage_->hyper_cycle)) {
      op1_it = get_next_occurence(op1_it, link12, f);
      op2_it = get_next_occurence(op2_it, link23, f);
      add_neighbor((*op1_it)->route_succ, *op2_it);
      add_neighbor((*op2_it)->route_pred, *op1_it);
      ++op1_it;
      ++op2_it;
    }
  }

  // Connect source to talkers
  for (auto talker_link : stream.route.traverse_talker_links()) {
    auto op_it = processing_order_[talker_link].begin();
    for (auto f : stream.frames(stream_storage_->hyper_cycle)) {
      op_it = get_next_occurence(op_it, talker_link, f);
      add_neighbor(processing_order_.src().route_succ, *op_it);
      add_neighbor((*op_it)->route_pred, &processing_order_.src());
      ++op_it;
    }
  }

  // Connect source to talkers
  for (auto listener_link : stream.route.traverse_listener_links()) {
    auto op_it = processing_order_[listener_link].begin();
    for (auto f : stream.frames(stream_storage_->hyper_cycle)) {
      op_it = get_next_occurence(op_it, listener_link, f);
      add_neighbor((*op_it)->route_succ, &processing_order_.sink());
      add_neighbor(processing_order_.sink().route_pred, *op_it);
      ++op_it;
    }
  }
}

void TransmissionGraph::recompute_positions(Link link) {
  auto &operations = processing_order_.map.at(link);
  for (auto [i, op] : std::views::enumerate(operations)) {
    position_[op->id] = {&operations, i};
  }
}

void TransmissionGraph::add_neighbor(std::vector<TransmissionOperation *> &neighbors,
                                     TransmissionOperation *op) noexcept {
  auto it = std::ranges::find(neighbors, op);
  if (it == neighbors.end()) {
    neighbors.push_back(op);
  }
}

void TransmissionGraph::consistent_flip(const FlipInstruction &inst) noexcept {
  auto [_, cur_pos] = position_[inst.op_id];
  if (cur_pos > inst.new_pos) {
    consistent_flip<MOVE_BEFORE>(inst);
  } else {
    consistent_flip<MOVE_AFTER>(inst);
  }
}

template <TransmissionGraph::FlipPolicy P>
void TransmissionGraph::consistent_flip(
    std::map<std::pair<Link, GlobalOpIndex>, LinkOpPosition> &&req_flips) noexcept {
  auto log_flip = [&](auto id, auto pos) {
    if (!flip_log_.contains(id)) {
      flip_log_.insert({id, pos});
    }
  };
  constexpr auto next = [](auto pos) {
    if constexpr (P == MOVE_BEFORE) {
      return pos - 1;
    } else {
      return pos + 1;
    }
  };

  while (!req_flips.empty()) {
    auto [key, new_pos] = *req_flips.begin();
    auto [link, op_id] = key;
    log_flip(op_id, std::get<1>(position_[op_id]));
    req_flips.erase(key);

    for (LinkOpPosition cur_pos = std::get<1>(position_[op_id]); cur_pos != new_pos;
         cur_pos = next(cur_pos)) {
      // swap positions
      auto &op = *processing_order_[link][cur_pos];
      auto &prev_op = *processing_order_[link][next(cur_pos)];
      log_flip(prev_op.id, next(cur_pos));
      std::iter_swap(processing_order_[link].begin() + static_cast<std::ptrdiff_t>(cur_pos),
                     processing_order_[link].begin() + static_cast<std::ptrdiff_t>(next(cur_pos)));
      if constexpr (P == MOVE_BEFORE) {
        std::get<1>(position_[op.id])--;
        std::get<1>(position_[prev_op.id])++;
      } else { // P == MOVE_AFTER
        std::get<1>(position_[op.id])++;
        std::get<1>(position_[prev_op.id])--;
      }

      // if both frame transmissions are within the same queue,
      // we need to respect the FIFO property and have to update
      // predecessor or successor transmissions as well
      if (op.pcp != prev_op.pcp) {
        continue;
      }
      for (auto &req_flip : adjacent_flips<P>(op, prev_op)) {
        auto it = req_flips.find({req_flip.link, req_flip.op_id});
        if (it == req_flips.end()) {
          req_flips.insert({{req_flip.link, req_flip.op_id}, req_flip.new_pos});
        } else if (P == MOVE_BEFORE) {
          it->second = std::min(it->second, req_flip.new_pos);
        } else { // P == MOVE_AFTER
          it->second = std::max(it->second, req_flip.new_pos);
        }
      }
    }
  }
}

template <TransmissionGraph::FlipPolicy P>
void TransmissionGraph::consistent_flip(const FlipInstruction &inst) noexcept {
  consistent_flip<P>({{{inst.link, inst.op_id}, inst.new_pos}});
}

auto TransmissionGraph::delete_merged_neighbors(
    const std::deque<OperationPair> &related_edges,
    std::vector<TransmissionOperation *> &neighbors) noexcept
    -> std::vector<TransmissionOperation *> & {
  std::erase_if(neighbors, [&related_edges](auto *op) {
    return std::ranges::find_if(related_edges, [op](auto &pair) {
             return pair.second == op->id;
           }) != related_edges.end();
  });
  return neighbors;
}

void TransmissionGraph::relink_job_predecessors(TransmissionOperation *old_op,
                                                TransmissionOperation *new_op) noexcept {
  for (auto *op : old_op->route_pred) {
    std::ranges::replace(op->route_succ, old_op, new_op);
  }
  std::ranges::copy(old_op->route_pred, std::back_inserter(new_op->route_pred));
}

void TransmissionGraph::relink_job_successors(TransmissionOperation *old_op,
                                              TransmissionOperation *new_op) noexcept {
  for (auto *op : old_op->route_succ) {
    std::ranges::replace(op->route_pred, old_op, new_op);
  }
  std::ranges::copy(old_op->route_succ, std::back_inserter(new_op->route_succ));
}

template <TransmissionGraph::FlipPolicy P>
auto TransmissionGraph::adjacent_flips(const TransmissionOperation &first,
                                       const TransmissionOperation &second) const noexcept
    -> Generator<FlipInstruction> {
  co_yield adjacent_flips<P>(first.route_pred, second.route_pred);
  co_yield adjacent_flips<P>(first.route_succ, second.route_succ);
}

template <TransmissionGraph::FlipPolicy P>
auto TransmissionGraph::adjacent_flips(const std::vector<TransmissionOperation *> &first,
                                       const std::vector<TransmissionOperation *> &second)
    const noexcept -> Generator<FlipInstruction> {
  auto flip_required = [](auto first, auto second) {
    if constexpr (P == MOVE_BEFORE) {
      return first < second;
    } else {
      return first > second;
    }
  };

  for (auto pair : related_neighbor_pairs(first, second)) {
    const auto &op1 = processing_order_[pair.first];

    LinkOpPosition cur_pos = position_[pair.first].second;
    LinkOpPosition req_pos = position_[pair.second].second;
    if (flip_required(req_pos, cur_pos)) {
      FlipInstruction inst = {
          .link = {op1.source->id, op1.target->id}, .op_id = op1.id, .new_pos = req_pos};
      co_yield inst;
    }
  }
}

[[nodiscard]] auto TransmissionGraph::equivalence_class(OperationPair pair) const noexcept
    -> std::deque<OperationPair> {
  std::deque<OperationPair> finished;
  std::deque<OperationPair> visited = {pair};

  while (!visited.empty()) {
    auto cur = visited.front();

    for (auto &next : related_neighbor_pairs(cur)) {
      if (std::ranges::find(finished, next) != finished.end()) {
        continue;
      }
      if (std::ranges::find(visited, next) == visited.end()) {
        visited.push_back(next);
      }
    }

    visited.pop_front();
    finished.push_back(cur);
  }

  return finished;
}

[[nodiscard]] auto TransmissionGraph::related_neighbor_pairs(OperationPair pair) const noexcept
    -> Generator<OperationPair> {
  assert(position_[pair.first].first == position_[pair.second].first);

  const auto &op1 = processing_order_[pair.first];
  const auto &op2 = processing_order_[pair.second];

  co_yield related_neighbor_pairs(op1.route_pred, op2.route_pred);
  co_yield related_neighbor_pairs(op1.route_succ, op2.route_succ);
}

[[nodiscard]] auto TransmissionGraph::related_neighbor_pairs(
    const std::vector<TransmissionOperation *> &first,
    const std::vector<TransmissionOperation *> &second) noexcept -> Generator<OperationPair> {
  for (auto *op1 : first) {
    if (op1->id == SOURCE_ID || op1->id == SINK_ID) {
      continue;
    }

    auto it = std::ranges::find_if(second, [&](auto *op2) {
      return op1->source->id == op2->source->id && op1->target->id == op2->target->id;
    });
    if (it != second.end()) {
      OperationPair pair = {op1->id, (*it)->id};
      co_yield pair;
    }
  }
}

auto TransmissionGraph::check_consistency() const noexcept -> bool {
  for (auto link : std::views::keys(processing_order_.map)) {
    const auto &transmissions = processing_order_.map.at(link);
    size_t const n = transmissions.size();
    for (size_t pos1 = 0; pos1 < n; pos1++) {
      for (size_t pos2 = pos1 + 1; pos2 < n; pos2++) {
        if (transmissions[pos1]->pcp != transmissions[pos2]->pcp) {
          continue;
        }
        for (auto _ : adjacent_flips<MOVE_BEFORE>(*transmissions[pos1], *transmissions[pos2])) {
          return false;
        }
      }
    }
  }
  return true;
}

auto TransmissionGraph::is_valid(GlobalOpIndex id) const noexcept -> bool {
  return position_[id].second < position_[id].first->size();
}

auto TransmissionGraph::contains(const Stream *stream) const noexcept -> bool {
  return stream_filter_(*stream);
}

void TransmissionGraph::print_critical_path(std::ostream &out) const {
  if (!critical_path_.valid) {
    return;
  }

  for (auto [op, cost] : critical_path_.traverse_operations()) {
    std::string op_str;
    if (op->id == SOURCE_ID) {
      op_str = "source";
    } else if (op->id == SINK_ID) {
      op_str = "sink";
    } else {
      op_str =
          std::format("{}: ([{},{}], {{{}}})", op->id, op->source->id, op->target->id,
                      std::accumulate(op->frames.begin(), op->frames.end(), std::string(""),
                                      [](auto s, auto frame) {
                                        return s == "" ? frame.name() : s + ", " + frame.name();
                                      }));
    }
    std::println(out, "{}: {}", op_str, cost);
  }
}

void TransmissionGraph::print_critical_cost(std::ostream &out) const {
  if (!critical_path_.valid) {
    return;
  }

  for (auto e : critical_path_.print()) {
    GlobalOpIndex const id = std::get<1>(e);
    std::string op_str;
    if (id == SOURCE_ID) {
      op_str = "source";
    } else if (id == SINK_ID) {
      op_str = "sink";
    } else {
      const auto &op = processing_order_[id];
      op_str =
          std::format("{}: ([{},{}], {{{}}})", id, op.source->id, op.target->id,
                      std::accumulate(op.frames.begin(), op.frames.end(), std::string(""),
                                      [](auto s, auto frame) {
                                        return s == "" ? frame.name() : s + ", " + frame.name();
                                      }));
    }
    std::println(out, "{}{}{}", std::get<0>(e), op_str, std::get<2>(e));
  }
}

template Generator<DFSVisitor> TransmissionGraph::traverse<FORWARD>();
template Generator<DFSVisitor> TransmissionGraph::traverse<BACKWARD>();

} // namespace tsndgm
