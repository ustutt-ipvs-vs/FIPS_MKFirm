#ifndef TSN_DGM_TRANSMISSION_GRAPH_H
#define TSN_DGM_TRANSMISSION_GRAPH_H

#include "../network/histogram.h"
#include "../network/stream.h"
#include "../network/topology.h"
#include "critical_path.h"
#include "transmission_operations.h"
#include "traversal.h"
#include <vector>

namespace tsndgm {

struct TransmissionGraph {
  TransmissionGraph(StreamStorage stream_storage);

  void add_streams(const std::vector<Stream> &streams);
  void add_stream(const Stream &stream, bool rebuild = true);

  auto critical_path() -> const CriticalPath::Result &;
  template <TraversalDirection D>
  [[nodiscard]] auto traverse() -> Generator<DFSVisitor>;

private:
  Delay hyper_cycle_;
  ProcessingOrder processing_order_;
  StreamStorage stream_storage_;

  DFSTraversal dfs_;
  CriticalPath critical_path_;

  void rebuild();
  void connect_precedence_constraints(const Stream &stream);
};

} // namespace tsndgm

#endif // TSN_DGM_TRANSMISSION_GRAPH_H
