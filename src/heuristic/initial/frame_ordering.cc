#include "frame_ordering.h"
#include "dgm/transmission_graph.h"
#include "network/stream.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include "utils/topological_sort.h"
#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace tsndgm {

EffectiveRelease::EffectiveRelease(const StreamStorage *stream_storage)
    : stream_storage_(stream_storage) {
  for (StreamId i = 0; i < stream_storage->streams.size(); i++) {
    streams_.push_back(StreamStorage({stream_storage->streams[i]}));
    auto g = TransmissionGraph(&streams_.back());
    stream_graphs_.emplace_back(std::move(g));

    for (auto [dev1, dev2] : stream_storage->streams[i].route.traverse_links()) {
      Link const link(dev1->id, dev2->id);
      auto it = link_to_streams_.find(link);
      if (it == link_to_streams_.end()) {
        link_to_streams_.insert({link, {i}});
      } else {
        link_to_streams_[link].push_back(i);
      }
    }
  }
}

auto EffectiveRelease::generate() -> std::vector<Frame> {
  size_t const n = stream_storage_->number_of_frames();
  auto hyper_cycle = stream_storage_->hyper_cycle;
  std::vector<Frame> frames = stream_storage_->frames().collect(n);
  PartialOrder partial_order(frames.size());

  auto critical_cost = [&](auto &g, auto link) {
    auto op_id = g[link][0]->id;
    return (*g.critical_path())[op_id].cost;
  };

  size_t i = 0;
  for (StreamId s1 = 0; s1 < stream_storage_->streams.size(); s1++) {
    const auto &stream1 = stream_storage_->streams[s1];
    for (StreamId s2 = 0; s2 < stream_storage_->streams.size(); s2++) {
      const auto &stream2 = stream_storage_->streams[s2];

      auto link = get_common_link(s1, s2);
      if (!link.has_value()) {
        continue;
      }
      Delay const crit_cost1 = critical_cost(stream_graphs_[s1], *link);
      Delay const crit_cost2 = critical_cost(stream_graphs_[s2], *link);

      for (auto f1 : stream1.frames(hyper_cycle)) {
        // get maximum f2 with:
        // f1 * stream1.period + crit_cost1 > f2 * stream2.period + crit_cost2
        if (f1 * stream1.period + crit_cost1 - crit_cost2 - 1 < 0) {
          continue;
        }

        FrameIndex f2 = (f1 * stream1.period + crit_cost1 - crit_cost2 - 1) / stream2.period;
        auto f2_it = std::ranges::find_if(
            frames, [f2, &stream2](auto &f) { return f.stream == &stream2 && f.id == f2; });
        if (f2_it != frames.end()) {
          partial_order[i + f1].push_back(f2_it - frames.begin());
        }
      }
    }
    i += hyper_cycle / stream1.period;
  }

  return topological_sort<Frame>(frames, partial_order).collect(n);
}

auto EffectiveRelease::get_common_link(StreamId first,
                                       StreamId second) const -> std::optional<Link> {
  for (const auto &[link, streams] : link_to_streams_) {
    auto it_first = std::ranges::find(streams, first);
    if (it_first == streams.end()) {
      continue;
    }
    auto it_second = std::ranges::find(streams, second);
    if (it_second != streams.end()) {
      return link;
    }
  }
  return {};
}

} // namespace tsndgm
