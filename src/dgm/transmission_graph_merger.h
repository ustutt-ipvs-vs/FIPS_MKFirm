#pragma once

#include "transmission_graph.h"

namespace tsndgm {

struct TransmissionGraphMerger {
  TransmissionGraph first, second;

  template <typename T1, typename T2>
  TransmissionGraphMerger(
      const StreamStorage *stream_storage, const NetworkTopology *topology, T1 &&first, T2 &&second,
      const std::function<bool(const Stream &)> &merged_stream_filter,
      const std::function<Delay(TransmissionGraph &, GlobalOpIndex)> &eval) noexcept
      : first(std::forward<T1>(first)), second(std::forward<T2>(second)),
        stream_storage_(stream_storage), topology_(topology),
        merged_stream_filter_(merged_stream_filter), eval_(eval) {}

  [[nodiscard]] auto generate(GlobalObjective objective_type) noexcept -> TransmissionGraph;

private:
  using Ordering =
      std::map<std::pair<Delay, Frame>, std::pair<TransmissionOperation, TransmissionGraph *>>;

  const StreamStorage *stream_storage_;
  const NetworkTopology *topology_;
  std::function<bool(const Stream &)> merged_stream_filter_;
  std::function<Delay(TransmissionGraph &, GlobalOpIndex)> eval_;

  [[nodiscard]] auto
  consistent_merge(GlobalObjective objective_type, std::vector<TransmissionOperation> &&initial,
                   const std::vector<GlobalOpIndex> &selection) -> TransmissionGraph;

  [[nodiscard]] auto compute_ordering() noexcept -> Ordering;
  [[nodiscard]] static auto
  compute_initial(const Ordering &order) noexcept -> std::vector<TransmissionOperation>;
  [[nodiscard]] static auto
  compute_old_to_new_mapping(const TransmissionGraph *g,
                             const Ordering &order) noexcept -> std::vector<GlobalOpIndex>;
};

} // namespace tsndgm
