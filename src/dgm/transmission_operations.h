#ifndef TSN_DGM_TRANSMISSION_OPERATIONS_H
#define TSN_DGM_TRANSMISSION_OPERATIONS_H

#include "../network/histogram.h"
#include "../network/stream.h"
#include "../network/topology.h"
#include <set>
#include <vector>

namespace tsndgm {

struct WeightPair {
  Delay incoming;
  Delay outgoing;
};

enum EdgeType : std::uint8_t { FIFO, MACHINE, JOB, NONE };
struct TransmissionWeights {
  WeightPair fifo;
  WeightPair machine;
  WeightPair job;

  auto operator[](EdgeType type) const -> WeightPair {
    switch (type) {
    case FIFO:
      return fifo;
    case MACHINE:
      return machine;
    case JOB:
      return job;
    default:
      return {0, 0};
    }
  }
};

using GlobalOpIndex = size_t;
using LinkOpPosition = size_t;
struct TransmissionOperation {
  GlobalOpIndex id;
  const DeviceProperty *source;
  const DeviceProperty *target;
  std::set<Frame> streams;

  PCPValue pcp;
  TransmissionWeights weights;

  std::vector<TransmissionOperation *> route_pred;
  std::vector<TransmissionOperation *> route_succ;
};

using LinkTransmissions = std::vector<TransmissionOperation *>;
using OperationPosition =
    std::vector<std::pair<LinkTransmissions *, LinkOpPosition>>;

static constexpr GlobalOpIndex SOURCE_ID = 0;
static constexpr GlobalOpIndex SINK_ID = 1;

struct ProcessingOrder {
  TransmissionOperation src{.id = SOURCE_ID};
  TransmissionOperation sink{.id = SINK_ID};
  std::vector<TransmissionOperation> operations;
  std::map<Link, LinkTransmissions> map;
  GlobalOpIndex total_operations{SINK_ID + 1};

  auto operator[](const Link &link) -> LinkTransmissions & { return map[link]; }
  auto operator[](GlobalOpIndex id) -> TransmissionOperation & {
    return operations[id];
  }
};

} // namespace tsndgm

#endif // TSN_DGM_TRANSMISSION_OPERATIONS_H
