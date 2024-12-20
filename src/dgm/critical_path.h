#ifndef TSN_DGM_CRITICAL_PATH_H
#define TSN_DGM_CRITICAL_PATH_H

#include "traversal.h"

namespace tsndgm {

struct CriticalPath {
  struct Result {
    Delay objective;
    Vertex *critical_vertex;
  };
  bool valid;

  CriticalPath() = default;
  CriticalPath(DFSTraversal *dfs, const ProcessingOrder &processing_order)
      : dfs_(dfs), src_(&processing_order.src()),
        sink_(&processing_order.sink()),
        crit_cost_(processing_order.total_operations) {};

  auto compute() -> std::optional<Result>;
  constexpr auto get_last() -> Result { return last_result_; }
  [[nodiscard]] auto print() const
      -> Generator<std::tuple<std::string, GlobalOpIndex, std::string>>;

  constexpr auto
  visitor_discover_vertex(auto visitor) noexcept -> TraversalStatus;
  constexpr auto visitor_finish_edge(auto visitor) noexcept -> TraversalStatus;

private:
  struct CriticalCost {
    Delay cost;
    GlobalOpIndex pred;
  };

  DFSTraversal *dfs_;
  const Vertex *src_, *sink_;
  Result last_result_;

  std::vector<CriticalCost> crit_cost_;

  struct VertexInfo {
    std::vector<std::pair<GlobalOpIndex, EdgeType>> succ;
    std::string indent;
  };
  [[nodiscard]] auto print(const std::vector<VertexInfo> &info,
                           std::string indent, GlobalOpIndex id,
                           GlobalOpIndex parent, bool is_last_child) const
      -> Generator<std::tuple<std::string, GlobalOpIndex, std::string>>;
};

} // namespace tsndgm

#endif // TSN_DGM_CRITICAL_PATH_H
