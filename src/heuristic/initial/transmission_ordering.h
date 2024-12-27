#pragma once

#include "dgm/transmission_graph.h"
#include "dgm/transmission_operations.h"
#include "network/stream_storage.h"

namespace tsndgm {

struct PrecedenceGraphs {
  std::map<StreamId, TransmissionGraph> stream_graphs;
  std::map<Link, std::vector<StreamId>> link_to_streams;

  PrecedenceGraphs() = default;
  PrecedenceGraphs(
      const StreamStorage *stream_storage,
      const std::function<bool(const Stream &)> &stream_filter = default_stream_filter) noexcept;

  [[nodiscard]] auto effective_release(StreamId id, Link link) noexcept -> Delay;
  [[nodiscard]] auto effective_deadline(StreamId id, Link link) noexcept -> Delay;

private:
  const StreamStorage *stream_storage_;
};

struct EffectiveRelease {
  PrecedenceGraphs graphs;

  EffectiveRelease(
      const StreamStorage *stream_storage, const NetworkTopology *network,
      const std::function<bool(const Stream &)> &stream_filter = default_stream_filter) noexcept;

  [[nodiscard]] auto generate(GlobalObjective objective_type) noexcept -> TransmissionGraph;

private:
  const StreamStorage *stream_storage_;
  std::function<bool(const Stream &)> stream_filter_;
};

struct IterativeEffectiveRelease {
  IterativeEffectiveRelease(
      const StreamStorage *stream_storage, const NetworkTopology *network,
      const std::function<bool(const Stream &)> &stream_filter = default_stream_filter) noexcept;

  [[nodiscard]] auto generate(GlobalObjective objective_type) noexcept -> TransmissionGraph;

private:
  const StreamStorage *stream_storage_;
  std::function<bool(const Stream &)> stream_filter_;
};

} // namespace tsndgm
