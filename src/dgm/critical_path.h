#ifndef TSN_DGM_CRITICAL_PATH_H
#define TSN_DGM_CRITICAL_PATH_H

#include "traversal.h"

namespace tsndgm {

struct CriticalPath {
  struct Result {
    Delay objective;
    Vertex *critical_vertex;
  };

  CriticalPath() = default;
  CriticalPath(DFSTraversal *dfs, const ProcessingOrder &processing_order)
      : dfs_(dfs), sink_(&processing_order.sink),
        crit_cost_(processing_order.total_operations) {};
  auto compute() -> const Result &;
  auto get_last() -> const Result & { return last_result_; }

private:
  struct CriticalCost {
    Delay cost;
    OpIndex pred;
  };

  DFSTraversal *dfs_;
  const Vertex *sink_;
  Result last_result_;

  std::vector<CriticalCost> crit_cost_;
};

} // namespace tsndgm

#endif // TSN_DGM_CRITICAL_PATH_H
