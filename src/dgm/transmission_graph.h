#ifndef TSN_DGM_TRANSMISSION_GRAPH_H
#define TSN_DGM_TRANSMISSION_GRAPH_H

#include "../network/histogram.h"
#include "../network/stream_storage.h"
#include "../network/topology.h"
#include "critical_path.h"
#include "transmission_operations.h"
#include "traversal.h"
#include <vector>

namespace tsndgm {

struct FlipInstruction {
  Link link;
  GlobalOpIndex op_id;
  LinkOpPosition new_pos;
};

struct TransmissionGraph {
  TransmissionGraph(const StreamStorage *stream_storage);

  auto critical_path() -> const CriticalPath::Result &;
  void print_critical_path(std::ostream &out = std::cout) const;
  template <TraversalDirection D>
  [[nodiscard]] auto traverse() -> Generator<DFSVisitor>;

  void flip(const FlipInstruction &inst) noexcept;

private:
  ProcessingOrder processing_order_;
  const StreamStorage *stream_storage_;

  DFSTraversal dfs_;
  CriticalPath critical_path_;
  OperationPosition position_;

  void rebuild();
  void add_frame(const Stream &stream, FrameIndex f);
  void connect_precedence_constraints(const Stream &stream);
  void recompute_positions(Link link);

  void consistent_flip(const FlipInstruction &inst) noexcept;

  [[nodiscard]] auto adjacent_flips(const TransmissionOperation &first,
                                    const TransmissionOperation &second)
      const noexcept -> Generator<FlipInstruction>;
  [[nodiscard]] auto
  adjacent_flips(const std::vector<TransmissionOperation *> &first,
                 const std::vector<TransmissionOperation *> &second)
      const noexcept -> Generator<FlipInstruction>;

  [[nodiscard]] auto check_consistency() const noexcept -> bool;
};

} // namespace tsndgm

#endif // TSN_DGM_TRANSMISSION_GRAPH_H
