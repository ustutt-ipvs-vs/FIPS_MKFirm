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

using OpIndex = size_t;
struct TransmissionOperation {
  OpIndex id;
  const DeviceProperty *source;
  const DeviceProperty *target;
  std::set<const Stream *> streams;

  PCPValue pcp;
  TransmissionWeights weights;

  std::vector<TransmissionOperation *> route_pred;
  std::vector<TransmissionOperation *> route_succ;
};

using LinkTransmissions = std::vector<TransmissionOperation>;

static constexpr OpIndex SOURCE_ID = 0;
static constexpr OpIndex SINK_ID = 1;

struct ProcessingOrder {
  TransmissionOperation src{.id = SOURCE_ID};
  TransmissionOperation sink{.id = SINK_ID};
  std::map<Link, LinkTransmissions> map;
  OpIndex total_operations{SINK_ID + 1};

  auto operator[](const Link &link) -> LinkTransmissions & { return map[link]; }
};

} // namespace tsndgm

#endif // TSN_DGM_TRANSMISSION_OPERATIONS_H
