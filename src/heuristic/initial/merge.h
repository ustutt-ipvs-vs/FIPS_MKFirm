#pragma once

#include "dgm/transmission_graph.h"

namespace tsndgm {

struct TransmissionGraphMerger {
  TransmissionGraph first, second;

  TransmissionGraphMerger(
      const StreamStorage *stream_storage, TransmissionGraph &&first, TransmissionGraph &&second,
      const std::function<bool(const Stream &)> &merged_stream_filter,
      const std::function<Delay(TransmissionGraph &, GlobalOpIndex)> &eval) noexcept;

  [[nodiscard]] auto generate(GlobalObjective objective_type) noexcept -> TransmissionGraph;

private:
  const StreamStorage *stream_storage_;
  std::function<bool(const Stream &)> merged_stream_filter_;
  std::function<Delay(TransmissionGraph &, GlobalOpIndex)> eval_;
};

} // namespace tsndgm
