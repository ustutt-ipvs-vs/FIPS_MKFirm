#pragma once

#include "dgm/transmission_graph.h"
#include "network/stream_storage.h"

namespace tsndgm {

struct NecessaryQueueingMerge {
  enum MergePolicy : std::uint8_t { MERGE_BEFORE, MERGE_AFTER };

  NecessaryQueueingMerge(const StreamStorage *stream_storage,
                         const NetworkTopology *network) noexcept;

  template <MergePolicy P>
  [[nodiscard]] auto compress_stream(TransmissionGraph &&g,
                                     StreamId id) noexcept -> TransmissionGraph;
  [[nodiscard]] auto compress_stream(TransmissionGraph &&g,
                                     StreamId id) noexcept -> TransmissionGraph;

private:
  const StreamStorage *stream_storage_;
  const NetworkTopology *network_;

  [[nodiscard]] auto wireless_links(StreamId id) const noexcept -> Generator<Link>;
};

} // namespace tsndgm
