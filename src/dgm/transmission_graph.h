#pragma once

#include "critical_path.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include "transmission_operations.h"
#include "traversal.h"
#include <vector>

namespace tsndgm {

struct FlipInstruction {
  Link link;
  GlobalOpIndex op_id;
  LinkOpPosition new_pos;
};

using OperationPair = std::pair<GlobalOpIndex, GlobalOpIndex>;
using MergeInstruction = OperationPair;
using InitialTransmissionOrder = std::vector<std::pair<Frame, RouteHopLink>>;

[[maybe_unused]] constexpr auto default_priority_filter = [](const TransmissionOperation & /*op*/) {
  return 0;
};

struct TransmissionGraph {
  GlobalObjective objective_type;

  TransmissionGraph() = default;
  explicit TransmissionGraph(
      const StreamStorage *stream_storage, GlobalObjective objective_type = MAKESPAN,
      const InitialTransmissionOrder &initial = {},
      const std::function<bool(const Stream &)> &stream_filter = default_stream_filter) noexcept;
  explicit TransmissionGraph(
      const StreamStorage *stream_storage, std::vector<TransmissionOperation> &&initial,
      GlobalObjective objective_type = MAKESPAN,
      const std::function<bool(const Stream &)> &stream_filter = default_stream_filter) noexcept;

  TransmissionGraph(const TransmissionGraph &other) noexcept;
  TransmissionGraph(TransmissionGraph &&other) noexcept;
  auto operator=(const TransmissionGraph &other) noexcept -> TransmissionGraph &;
  auto operator=(TransmissionGraph &&other) noexcept -> TransmissionGraph &;

  auto is_feasible() -> bool;
  auto critical_path() -> const CriticalPath *;
  void print_critical_path(std::ostream &out = std::cout) const;
  void print_critical_cost(std::ostream &out = std::cout) const;
  template <TraversalDirection D> [[nodiscard]] auto traverse() -> Generator<DFSVisitor>;

  [[nodiscard]] auto
  equivalence_class(OperationPair pair) const noexcept -> std::deque<OperationPair>;
  [[nodiscard]] auto
  related_neighbor_pairs(OperationPair pair) const noexcept -> Generator<OperationPair>;

  void flip(const FlipInstruction &inst) noexcept;
  void flip(GlobalOpIndex op_id, LinkOpPosition new_pos) noexcept;
  void undo_last_flip() noexcept;

  void merge(MergeInstruction inst) noexcept;

  auto operator[](const Link &link) const noexcept -> const LinkTransmissions & {
    return processing_order_[link];
  }
  auto operator[](GlobalOpIndex id) const noexcept
      -> std::pair<const TransmissionOperation *, LinkOpPosition> {
    auto [_, pos] = position_[id];
    return {&processing_order_[id], pos};
  }

  [[nodiscard]] auto number_of_transmissions(const Link &link) const noexcept -> LinkOpPosition {
    return processing_order_.number_of_transmissions(link);
  }
  [[nodiscard]] auto size() const noexcept -> GlobalOpIndex {
    return processing_order_.total_operations;
  }

  [[nodiscard]] auto check_consistency() const noexcept -> bool;
  [[nodiscard]] auto is_valid(GlobalOpIndex id) const noexcept -> bool;
  [[nodiscard]] auto contains(const Stream *stream) const noexcept -> bool;
  [[nodiscard]] auto operation_to_string(GlobalOpIndex id) const noexcept -> std::string;

private:
  enum FlipPolicy : std::uint8_t { MOVE_BEFORE, MOVE_AFTER };
  using FlipLog = std::map<GlobalOpIndex, LinkOpPosition>;

  ProcessingOrder processing_order_;
  const StreamStorage *stream_storage_;

  DFSTraversal dfs_;
  CriticalPath critical_path_;
  OperationPosition position_;
  FlipLog flip_log_;

  std::function<bool(const Stream &)> stream_filter_;

  void rebuild();
  void add_frame(Frame frame) noexcept;
  void add_operation(Frame frame, RouteHopLink port) noexcept;
  void connect_precedence_constraints(const Stream &stream);
  void recompute_positions(Link link);
  static void add_neighbor(std::vector<TransmissionOperation *> &neighbors,
                           TransmissionOperation *op) noexcept;

  void consistent_flip(const FlipInstruction &inst) noexcept;
  template <FlipPolicy P> void consistent_flip(const FlipInstruction &inst) noexcept;
  template <FlipPolicy P>
  void
  consistent_flip(std::map<std::pair<Link, GlobalOpIndex>, LinkOpPosition> &&req_flips) noexcept;

  static auto delete_merged_neighbors(const std::deque<OperationPair> &related_edges,
                                      std::vector<TransmissionOperation *> &neighbors) noexcept
      -> std::vector<TransmissionOperation *> &;
  static void relink_job_predecessors(TransmissionOperation *old_op,
                                      TransmissionOperation *new_op) noexcept;
  static void relink_job_successors(TransmissionOperation *old_op,
                                    TransmissionOperation *new_op) noexcept;

  template <FlipPolicy P>
  [[nodiscard]] auto
  adjacent_flips(const TransmissionOperation &first,
                 const TransmissionOperation &second) const noexcept -> Generator<FlipInstruction>;
  template <FlipPolicy P>
  [[nodiscard]] auto adjacent_flips(const std::vector<TransmissionOperation *> &first,
                                    const std::vector<TransmissionOperation *> &second)
      const noexcept -> Generator<FlipInstruction>;

  [[nodiscard]] static auto related_neighbor_pairs(
      const std::vector<TransmissionOperation *> &first,
      const std::vector<TransmissionOperation *> &second) noexcept -> Generator<OperationPair>;
};

} // namespace tsndgm
