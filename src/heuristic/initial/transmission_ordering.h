#pragma once

#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "network/stream_storage.h"

namespace tsndgm {

struct PrecedenceGraphs {
  std::map<StreamId, TransmissionGraph> stream_graphs;
  std::map<Link, std::vector<StreamId>> link_to_streams;

  PrecedenceGraphs(
      const StreamStorage *stream_storage,
      const std::function<bool(const Stream &)> &stream_filter = default_stream_filter) noexcept;

  void add_stream(StreamId id) noexcept;

  [[nodiscard]] auto effective_release(StreamId id, Link link) noexcept -> Delay;
  [[nodiscard]] auto effective_deadline(StreamId id, Link link) noexcept -> Delay;

private:
  const StreamStorage *stream_storage_;
};

} // namespace tsndgm
