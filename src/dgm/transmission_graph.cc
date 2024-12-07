#include "transmission_graph.h"
#include "../network/stream.h"
#include "../network/topology.h"
#include "/home/eggersn/projects/libtsndgm2/src/network/histogram.h"
#include "transmission_operations.h"
#include "traversal.h"
#include <bits/ranges_algo.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tsndgm {

TransmissionGraph::TransmissionGraph(StreamStorage stream_storage)
    : stream_storage_(std::move(stream_storage)) {
  hyper_cycle_ = std::ranges::fold_left(
      stream_storage_.streams, static_cast<Delay>(1),
      [](Delay h, auto &stream) { return std::lcm(h, stream.period); });
  add_streams(stream_storage_.streams);
}

void TransmissionGraph::add_streams(const std::vector<Stream> &streams) {
  for (const auto &stream : streams) {
    add_stream(stream, false);
  }
  rebuild();
}

void TransmissionGraph::add_stream(const Stream &stream, bool rebuild) {
  if (hyper_cycle_ % stream.period != 0) {
    throw std::invalid_argument(
        "Later added streams must fit within the same hypercycle");
  }

  // 1. Add all transmission operations without precedence constraints
  for (auto [source, target] : stream.route.traverse_hops()) {
    Link const link(source->device->id, target->device->id);
    const PDB &pdb = stream.pdb_map.at(link);

    // Add operations for each of the stream's frame in the hypercycle
    for (FrameIndex const f : stream.frames(hyper_cycle_)) {
      TransmissionWeights weights = {.fifo = {pdb.d_total.min, pdb.d_trans.max},
                                     .machine = {0, pdb.d_trans.max},
                                     .job = {0, pdb.d_total.max}};
      if (source->is_talker()) {
        weights.job.incoming = f * stream.period + stream.phase;
      }

      processing_order_[link].push_back(
          {.id = processing_order_.total_operations++,
           .source = source->device,
           .target = target->device,
           .streams = {&stream},
           .pcp = stream.pcp,
           .weights = weights});
    }
  }

  if (rebuild) {
    this->rebuild();
  }
}

void TransmissionGraph::rebuild() {
  for (const auto &stream : stream_storage_) {
    connect_precedence_constraints(stream);
  }

  dfs_ = DFSTraversal(&processing_order_);
  critical_path_ = CriticalPath(&dfs_, processing_order_);
}

void TransmissionGraph::connect_precedence_constraints(const Stream &stream) {
  auto get_next_occurence = [&](auto it, auto link) {
    return std::find_if(it, processing_order_[link].end(),
                        [&](TransmissionOperation &op) {
                          return op.streams.contains(&stream);
                        });
  };

  // Connect stream operations via precedence constraints
  for (auto [link12, link23] : stream.route.traverse_consecutive_links()) {
    auto op1_it = processing_order_[link12].begin();
    auto op2_it = processing_order_[link23].begin();
    for (auto _ : stream.frames(hyper_cycle_)) {
      op1_it = get_next_occurence(op1_it, link12);
      op2_it = get_next_occurence(op2_it, link23);
      op1_it->route_succ.push_back(&(*op2_it));
      op2_it->route_pred.push_back(&(*op1_it));
      ++op1_it;
      ++op2_it;
    }
  }

  // Connect source to talkers
  for (auto talker_link : stream.route.traverse_talker_links()) {
    auto op_it = processing_order_[talker_link].begin();
    for (auto _ : stream.frames(hyper_cycle_)) {
      op_it = get_next_occurence(op_it, talker_link);
      processing_order_.src.route_succ.push_back(&(*op_it));
      op_it->route_pred.push_back(&processing_order_.src);
      ++op_it;
    }
  }

  // Connect source to talkers
  for (auto listener_link : stream.route.traverse_listener_links()) {
    auto op_it = processing_order_[listener_link].begin();
    for (auto _ : stream.frames(hyper_cycle_)) {
      op_it = get_next_occurence(op_it, listener_link);
      op_it->route_succ.push_back(&processing_order_.sink);
      processing_order_.sink.route_pred.push_back(&(*op_it));
      ++op_it;
    }
  }
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

template Generator<DFSVisitor> TransmissionGraph::traverse<FORWARD>();
template Generator<DFSVisitor> TransmissionGraph::traverse<BACKWARD>();

} // namespace tsndgm
