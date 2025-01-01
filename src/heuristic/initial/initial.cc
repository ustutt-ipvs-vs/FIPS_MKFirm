#include "initial.h"
#include "dgm/critical_path.h"
#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "merge.h"
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
      aggregation_(stream_storage, network, PER_FRAME) {}

auto IncrementalHeuristic::add_stream(
    StreamId id,
    const std::function<Delay(TransmissionGraph &g, GlobalOpIndex id)> &eval) noexcept -> bool {
  const auto &stream = stream_storage_->streams[id];
  graphs_.add_stream(id);

  TransmissionGraph g_new;
  if (feasible_streams_.empty()) {
    g_new = graphs_.stream_graphs[id];
  } else {
    auto merged_stream_filter = [&](const Stream &s) {
      return std::ranges::find(feasible_streams_, &s) != feasible_streams_.end() || &stream == &s;
    };

    TransmissionGraphMerger merger(stream_storage_, g, graphs_.stream_graphs[id],
                                   merged_stream_filter, eval);
    g_new = merger.generate(PER_FRAME);
    g_new.fix_stream_consistency(id);
    assert(g_new.check_consistency());
    if (check_feasibility(g_new)) {
      g = std::move(g_new);
      feasible_streams_.push_back(&stream);
      return true;
    }

    g_new = aggregation_.compress_stream(std::move(g_new), &graphs_, id);
    assert(g_new.check_consistency());
  }

  if (check_feasibility(g_new)) {
    g = std::move(g_new);
    feasible_streams_.push_back(&stream);
    return true;
  }
  return false;
}

auto IncrementalHeuristic::check_feasibility(TransmissionGraph &g_new) noexcept -> bool {
  const auto *crit_path = g_new.critical_path();
  g_new.print_critical_path();
  return crit_path != nullptr && crit_path->get_last().objective <= 0;
}

} // namespace tsndgm
