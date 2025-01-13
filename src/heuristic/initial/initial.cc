#include "initial.h"
#include "dgm/critical_path.h"
#include "dgm/transmission_graph.h"
#include "dgm/transmission_graph_merger.h"
#include "dgm/transmission_operations.h"
#include "network/stream.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <utility>
#include <vector>

namespace tsndgm {

IncrementalHeuristic::IncrementalHeuristic(const StreamStorage *stream_storage,
                                           const NetworkTopology *network) noexcept
    : stream_storage_(stream_storage), network_(network), graphs_(stream_storage),
      aggregation_(stream_storage, network) {}

auto IncrementalHeuristic::add_stream(
    StreamId id,
    const std::function<Delay(TransmissionGraph &g, GlobalOpIndex id)> &eval) noexcept -> bool {
  const auto &stream = stream_storage_->streams[id];
  graphs_.add_stream(id);

  TransmissionGraph g_new = stream.route.has_wireless_links() ? add_wireless_stream(id, eval)
                                                              : add_wired_stream(id, eval);
  if (g_new.is_feasible()) {
    g = std::move(g_new);
    feasible_streams_.push_back(&stream);
    return true;
  }
  g_new.print_critical_path();
  return false;
}

auto IncrementalHeuristic::add_wired_stream(
    StreamId id, const std::function<Delay(TransmissionGraph &g, GlobalOpIndex id)> &eval) noexcept
    -> TransmissionGraph {
  const auto &stream = stream_storage_->streams[id];

  auto stream_filter = [&](const Stream &s) {
    return std::ranges::find(feasible_streams_, &s) != feasible_streams_.end() || &stream == &s;
  };
  auto g_new = TransmissionGraphMerger(stream_storage_, network_, g, graphs_.stream_graphs[id],
                                       stream_filter, eval)
                   .generate(PER_FRAME);
  assert(g_new.check_consistency());

  if (g_new.is_feasible()) {
    auto wired_stream_filter = [&](const Stream &s) {
      return !s.route.has_wireless_links() &&
             (std::ranges::find(feasible_streams_, &s) != feasible_streams_.end() || &stream == &s);
    };
    g_wired_ = TransmissionGraphMerger(stream_storage_, network_, std::move(g_wired_),
                                       graphs_.stream_graphs[id], wired_stream_filter, eval)
                   .generate(PER_FRAME);
    assert(g_wired_.check_consistency());
  }
  return g_new;
}

auto IncrementalHeuristic::add_wireless_stream(
    StreamId id, const std::function<Delay(TransmissionGraph &g, GlobalOpIndex id)> &eval) noexcept
    -> TransmissionGraph {
  const auto &stream = stream_storage_->streams[id];

  // merge new stream into g_wireless
  auto wireless_stream_filter = [&](const Stream &s) {
    return s.route.has_wireless_links() &&
           (std::ranges::find(feasible_streams_, &s) != feasible_streams_.end() || &stream == &s);
  };
  auto g_wireless_new =
      TransmissionGraphMerger(stream_storage_, network_, g_wireless_, graphs_.stream_graphs[id],
                              wireless_stream_filter, eval)
          .generate(PER_FRAME);
  g_wireless_new = aggregation_.compress_stream(std::move(g_wireless_new), id);
  assert(g_wireless_new.check_consistency());

  // merge g_wired and g_wireless
  auto stream_filter = [&](const Stream &s) {
    return std::ranges::find(feasible_streams_, &s) != feasible_streams_.end() || &stream == &s;
  };
  auto g_new = TransmissionGraphMerger(stream_storage_, network_, g_wired_, g_wireless_new,
                                       stream_filter, eval)
                   .generate(PER_FRAME);
  assert(g_new.check_consistency());

  if (g_new.is_feasible()) {
    g_wireless_ = std::move(g_wireless_new);
  }
  return g_new;
}

StrictTemporalIsolationHeuristic::StrictTemporalIsolationHeuristic(
    const StreamStorage *stream_storage, const NetworkTopology *network) noexcept
    : stream_storage_(stream_storage), network_(network), graphs_(stream_storage) {}

auto StrictTemporalIsolationHeuristic::add_stream(
    StreamId id,
    const std::function<Delay(TransmissionGraph &g, GlobalOpIndex id)> &eval) noexcept -> bool {
  const auto &stream = stream_storage_->streams[id];
  graphs_.add_stream(id);

  auto stream_filter = [&](const Stream &s) {
    return std::ranges::find(feasible_streams_, &s) != feasible_streams_.end() || &stream == &s;
  };
  TransmissionGraph g_new = TransmissionGraphMerger(stream_storage_, network_, g,
                                                    graphs_.stream_graphs[id], stream_filter, eval)
                                .generate(PER_FRAME);

  if (g_new.is_feasible()) {
    g = std::move(g_new);
    feasible_streams_.push_back(&stream);
    return true;
  }
  g_new.print_critical_path();
  return false;
}

} // namespace tsndgm
