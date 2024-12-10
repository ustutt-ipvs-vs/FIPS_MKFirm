#include "transmission_graph.h"
#include "../network/histogram.h"
#include "../network/stream.h"
#include "../network/stream_storage.h"
#include "../network/topology.h"
#include "critical_path.h"
#include "transmission_operations.h"
#include "traversal.h"
#include <algorithm>
#include <bits/ranges_algo.h>
#include <cassert>
#include <cstddef>
#include <format>
#include <map>
#include <numeric>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tsndgm {

TransmissionGraph::TransmissionGraph(const StreamStorage *stream_storage)
    : stream_storage_(stream_storage) {
  processing_order_.operations.resize(
      std::ranges::fold_left(stream_storage->sorted_frames(), SINK_ID + 1,
                             [](GlobalOpIndex c, auto & /*f*/) { return c + 1; }));
  for (auto [stream_ptr, f] : stream_storage->sorted_frames()) {
    add_frame(*stream_ptr, f);
  }
  rebuild();
}

auto TransmissionGraph::critical_path() -> const CriticalPath::Result & {
  return critical_path_.compute();
}

template <TraversalDirection D>
auto TransmissionGraph::traverse() -> Generator<DFSVisitor> {
  if constexpr (D == FORWARD) {
    for (auto visitor : dfs_.traverse<FORWARD>(&processing_order_.src)) {
      co_yield visitor;
    }
  } else {
    for (auto visitor : dfs_.traverse<BACKWARD>(&processing_order_.sink)) {
      co_yield visitor;
    }
  }
}

void TransmissionGraph::flip(const FlipInstruction &inst) noexcept {
  consistent_flip(inst);
}

void TransmissionGraph::rebuild() {
  for (const auto &stream : *stream_storage_) {
    connect_precedence_constraints(stream);
  }
  position_.resize(processing_order_.total_operations);
  for (auto link : std::views::keys(processing_order_.map)) {
    recompute_positions(link);
  }

  dfs_ = DFSTraversal(&processing_order_, &position_);
  critical_path_ = CriticalPath(&dfs_, processing_order_);
}

void TransmissionGraph::add_frame(const Stream &stream, FrameIndex f) {
  if (stream_storage_->hyper_cycle % stream.period != 0) {
    throw std::invalid_argument(
        "Later added streams must fit within the same hypercycle");
  }

  // 1. Add all transmission operations without precedence constraints
  for (auto [source, target] : stream.route.traverse_hops()) {
    Link const link(source->device->id, target->device->id);
    const PDB &pdb = stream.pdb_map.at(link);

    TransmissionWeights weights = {.fifo = {pdb.d_total.min, pdb.d_trans.max},
                                   .machine = {0, pdb.d_trans.max},
                                   .job = {0, pdb.d_total.max}};
    if (source->is_talker()) {
      weights.job.incoming = f * stream.period + stream.phase;
    }

    GlobalOpIndex const id = processing_order_.total_operations++;
    processing_order_[id] = {.id = id,
                             .source = source->device,
                             .target = target->device,
                             .streams = {Frame(&stream, f)},
                             .pcp = stream.pcp,
                             .weights = weights};
    processing_order_[link].push_back(&processing_order_[id]);
  }
}

void TransmissionGraph::connect_precedence_constraints(const Stream &stream) {
  auto get_next_occurence = [&](auto it, auto link, auto f) {
    return std::find_if(it, processing_order_[link].end(),
                        [&](TransmissionOperation *op) {
                          return op->streams.contains(Frame(&stream, f));
                        });
  };

  // Connect stream operations via precedence constraints
  for (auto [link12, link23] : stream.route.traverse_consecutive_links()) {
    auto op1_it = processing_order_[link12].begin();
    auto op2_it = processing_order_[link23].begin();
    for (auto f : stream.frames(stream_storage_->hyper_cycle)) {
      op1_it = get_next_occurence(op1_it, link12, f);
      op2_it = get_next_occurence(op2_it, link23, f);
      (*op1_it)->route_succ.push_back(*op2_it);
      (*op2_it)->route_pred.push_back(*op1_it);
      ++op1_it;
      ++op2_it;
    }
  }

  // Connect source to talkers
  for (auto talker_link : stream.route.traverse_talker_links()) {
    auto op_it = processing_order_[talker_link].begin();
    for (auto f : stream.frames(stream_storage_->hyper_cycle)) {
      op_it = get_next_occurence(op_it, talker_link, f);
      processing_order_.src.route_succ.push_back(*op_it);
      (*op_it)->route_pred.push_back(&processing_order_.src);
      ++op_it;
    }
  }

  // Connect source to talkers
  for (auto listener_link : stream.route.traverse_listener_links()) {
    auto op_it = processing_order_[listener_link].begin();
    for (auto f : stream.frames(stream_storage_->hyper_cycle)) {
      op_it = get_next_occurence(op_it, listener_link, f);
      (*op_it)->route_succ.push_back(&processing_order_.sink);
      processing_order_.sink.route_pred.push_back(*op_it);
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

void TransmissionGraph::consistent_flip(const FlipInstruction &inst) noexcept {
  std::map<std::pair<Link, GlobalOpIndex>, LinkOpPosition> req_flips = {
      {{inst.link, inst.op_id}, inst.new_pos}};
  while (!req_flips.empty()) {
    auto [key, new_pos] = *req_flips.begin();
    auto [link, op_id] = key;
    req_flips.erase(key);

    for (LinkOpPosition cur_pos = std::get<1>(position_[op_id]);
         cur_pos > new_pos; cur_pos--) {
      auto &op = *processing_order_[link][cur_pos];
      auto &prev_op = *processing_order_[link][cur_pos - 1];
      std::iter_swap(processing_order_[link].begin() + cur_pos,
                     processing_order_[link].begin() + cur_pos - 1);
      std::get<1>(position_[op.id])--;
      std::get<1>(position_[prev_op.id])++;

      if (op.pcp != prev_op.pcp) {
        continue;
      }

      for (auto &req_flip : adjacent_flips(op, prev_op)) {
        auto it = req_flips.find({req_flip.link, req_flip.op_id});
        if (it != req_flips.end()) {
          it->second = std::min(it->second, req_flip.new_pos);
        } else {
          req_flips.insert({{req_flip.link, req_flip.op_id}, req_flip.new_pos});
        }
      }
    }
  }

  assert(check_consistency());
}

auto TransmissionGraph::adjacent_flips(const TransmissionOperation &first,
                                       const TransmissionOperation &second)
    const noexcept -> Generator<FlipInstruction> {
  for (auto &req_flip : adjacent_flips(first.route_pred, second.route_pred)) {
    co_yield req_flip;
  }
  for (auto &req_flip : adjacent_flips(first.route_succ, second.route_succ)) {
    co_yield req_flip;
  }
}

auto TransmissionGraph::adjacent_flips(
    const std::vector<TransmissionOperation *> &first,
    const std::vector<TransmissionOperation *> &second) const noexcept
    -> Generator<FlipInstruction> {
  for (auto *op1 : first) {
    if (op1->id == SOURCE_ID || op1->id == SINK_ID) {
      continue;
    }

    auto it = std::ranges::find_if(second, [&](auto *op2) {
      return op1->source->id == op2->source->id &&
             op1->target->id == op2->target->id;
    });
    if (it != second.end()) {
      LinkOpPosition cur_pos = std::get<1>(position_[op1->id]);
      LinkOpPosition req_pos = std::get<1>(position_[(*it)->id]);
      if (req_pos < cur_pos) {
        FlipInstruction inst = {.link = {op1->source->id, op1->target->id},
                                .op_id = op1->id,
                                .new_pos = req_pos};
        co_yield inst;
      }
    }
  }
}

auto TransmissionGraph::check_consistency() const noexcept -> bool {
  for (auto link : std::views::keys(processing_order_.map)) {
    const auto &transmissions = processing_order_.map.at(link);
    size_t const n = transmissions.size();
    for (size_t pos1 = 0; pos1 < n; pos1++) {
      for (size_t pos2 = pos1 + 1; pos2 < n; pos2++) {
        for (auto _ :
             adjacent_flips(*transmissions[pos1], *transmissions[pos2])) {
          return false;
        }
      }
    }
  }
  return true;
}

void TransmissionGraph::print_critical_path(std::ostream &out) const {
  for (auto e : critical_path_.print()) {
    GlobalOpIndex const id = std::get<1>(e);
    std::string op_str;
    if (id == SOURCE_ID) {
      op_str = "source";
    } else if (id == SINK_ID) {
      op_str = "sink";
    } else {
      auto const &op = processing_order_.operations[id];
      op_str =
          std::format("{}: ([{},{}], {{{}}})", id, op.source->id, op.target->id,
                      std::accumulate(op.streams.begin(), op.streams.end(),
                                      std::string(""), [](auto s, auto frame) {
                                        return s == ""
                                                   ? frame.name()
                                                   : s + ", " + frame.name();
                                      }));
    }
    std::println(out, "{}{}{}", std::get<0>(e), op_str, std::get<2>(e));
  }
}

template Generator<DFSVisitor> TransmissionGraph::traverse<FORWARD>();
template Generator<DFSVisitor> TransmissionGraph::traverse<BACKWARD>();

} // namespace tsndgm
