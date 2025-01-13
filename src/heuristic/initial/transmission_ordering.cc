#include "transmission_ordering.h"
#include "dgm/critical_path.h"
#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "network/stream.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include <cassert>
#include <functional>
#include <map>
#include <vector>

namespace tsndgm {

PrecedenceGraphs::PrecedenceGraphs(const StreamStorage *stream_storage) noexcept
    : stream_storage_(stream_storage) {}

void PrecedenceGraphs::add_stream(StreamId id) noexcept {
  const auto &stream = stream_storage_->streams[id];

  auto stream_filter = [&stream](const auto &other) { return &stream == &other; };
  stream_graphs.insert(
      {id, TransmissionGraph(stream_storage_, nullptr, MAKESPAN, {}, stream_filter)});

  for (auto [dev1, dev2] : stream.route.traverse_links()) {
    Link const link(dev1->id, dev2->id);
    auto it = link_to_streams.find(link);
    if (it == link_to_streams.end()) {
      link_to_streams.insert({link, {id}});
    } else {
      link_to_streams[link].push_back(id);
    }
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

} // namespace tsndgm
