#include "transmission_ordering.h"
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
#include <map>
#include <ranges>
#include <utility>
#include <vector>

namespace tsndgm {

PrecedenceGraphs::PrecedenceGraphs(
    const StreamStorage *stream_storage,
    const std::function<bool(const Stream &)> &stream_filter) noexcept
    : stream_storage_(stream_storage) {
  for (auto [s_id, stream] : stream_storage->filtered_streams_with_id(stream_filter)) {
    auto stream_filter = [stream](const auto &other) { return stream == &other; };
    stream_graphs.insert({s_id, TransmissionGraph(stream_storage, MAKESPAN, {}, stream_filter)});

    for (auto [dev1, dev2] : stream->route.traverse_links()) {
      Link const link(dev1->id, dev2->id);
      auto it = link_to_streams.find(link);
      if (it == link_to_streams.end()) {
        link_to_streams.insert({link, {s_id}});
      } else {
        link_to_streams[link].push_back(s_id);
      }
    }
    s_id++;
  }
}

auto PrecedenceGraphs::effective_release(StreamId id, Link link) noexcept -> Delay {
  auto &g = stream_graphs[id];
  auto op_id = g[link][0]->id;
  return (*g.critical_path())[op_id].cost;
}

auto PrecedenceGraphs::effective_deadline(StreamId id, Link link) noexcept -> Delay {
  const auto &stream = stream_storage_->streams[id];
  auto &g = stream_graphs[id];
  auto op_id = g[link][0]->id;
  const auto &critical_path = *g.critical_path();
  return stream.e2e_latency - (critical_path[SINK_ID].cost - critical_path[op_id].cost);
}

EffectiveRelease::EffectiveRelease(
    const StreamStorage *stream_storage, const NetworkTopology * /*network*/,
    const std::function<bool(const Stream &)> &stream_filter) noexcept
    : graphs(stream_storage, stream_filter), stream_storage_(stream_storage),
      stream_filter_(stream_filter) {}

auto EffectiveRelease::generate(GlobalObjective objective_type) noexcept -> TransmissionGraph {
  std::map<std::pair<Delay, Frame>, RouteHopLink> order;

  auto hyper_cycle = stream_storage_->hyper_cycle;
  for (auto [s_id, stream] : stream_storage_->filtered_streams_with_id(stream_filter_)) {
    for (auto [source, target] : stream->route.traverse_hops()) {
      Link const link(source->device->id, target->device->id);
      auto effective_release = graphs.effective_release(s_id, link);
      for (auto f : stream->frames(hyper_cycle)) {
        auto effective_release_f = f * stream->period + effective_release;
        order.insert({{effective_release_f, Frame(stream, f)}, {source, target}});
      }
    }
  }

  InitialTransmissionOrder initial(order.size());
  for (auto [i, op] : std::views::enumerate(order)) {
    initial[i] = {op.first.second, op.second};
  }
  TransmissionGraph g(stream_storage_, objective_type, initial, stream_filter_);
  assert(g.check_consistency());
  return g;
}

IterativeEffectiveRelease::IterativeEffectiveRelease(
    const StreamStorage *stream_storage, const NetworkTopology * /*network*/,
    const std::function<bool(const Stream &)> &stream_filter) noexcept
    : stream_storage_(stream_storage), stream_filter_(stream_filter) {}

auto IterativeEffectiveRelease::generate(GlobalObjective objective_type) noexcept
    -> TransmissionGraph {
  auto crit_cost_eval = [](TransmissionGraph &g, GlobalOpIndex id) {
    return (*g.critical_path())[id].cost;
  };

  std::vector<const Stream *> streams;
  TransmissionGraph g;

  PrecedenceGraphs graphs(stream_storage_, stream_filter_);
  for (auto &[s_id, g_s] : graphs.stream_graphs) {
    const auto &stream = stream_storage_->streams[s_id];
    if (streams.empty()) {
      streams = {&stream};
      g = std::move(g_s);
      continue;
    }

    g.critical_path();
    g_s.critical_path();

    g.print_critical_path();
    g_s.print_critical_path();

    auto merged_stream_filter = [&streams, &stream](const Stream &s) {
      return std::ranges::find(streams, &s) != streams.end() || &stream == &s;
    };

    TransmissionGraphMerger merger(stream_storage_, std::move(g), std::move(g_s),
                                   merged_stream_filter, crit_cost_eval);
    g = merger.generate(objective_type);
    streams.push_back(&stream);

    g.fix_stream_consistency(s_id);
    assert(g.check_consistency());
  }
  return g;
}

} // namespace tsndgm
