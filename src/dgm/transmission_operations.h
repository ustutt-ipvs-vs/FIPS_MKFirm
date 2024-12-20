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
  PacketDelayBudget pdb;

  static auto from_pdb(const PacketDelayBudget &pdb) {
    return TransmissionWeights{.fifo = {-pdb.d_total.min, pdb.d_trans.max},
                               .machine = {0, pdb.d_trans.max},
                               .job = {0, pdb.d_total.max},
                               .pdb = pdb};
  };

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

  void merge(const TransmissionWeights &other) {
    auto merge_pairs = [&](auto &first, auto &second, auto pdb_val) {
      first.incoming = std::max(first.incoming, second.incoming);
      first.outgoing = pdb_val;
    };

    pdb.merge(other.pdb);
    merge_pairs(fifo, other.fifo, pdb.d_trans.max);
    merge_pairs(machine, other.machine, pdb.d_trans.max);
    merge_pairs(job, other.job, pdb.d_total.max);
  }
};

using GlobalOpIndex = size_t;
using LinkOpPosition = size_t;
struct TransmissionOperation {
  GlobalOpIndex id;
  const DeviceProperty *source;
  const DeviceProperty *target;
  std::set<Frame> frames;

  PCPValue pcp;
  TransmissionWeights weights;

  std::vector<TransmissionOperation *> route_pred;
  std::vector<TransmissionOperation *> route_succ;

  [[nodiscard]] auto link() const { return Link(source->id, target->id); }
};

using LinkTransmissions = std::vector<TransmissionOperation *>;
using OperationPosition =
    std::vector<std::pair<LinkTransmissions *, LinkOpPosition>>;

static constexpr GlobalOpIndex SOURCE_ID = 0;
static constexpr GlobalOpIndex SINK_ID = 1;

struct ProcessingOrder {
  std::vector<TransmissionOperation> operations;
  std::map<Link, LinkTransmissions> map;
  GlobalOpIndex total_operations;

  ProcessingOrder() {
    operations.push_back({.id = SOURCE_ID});
    operations.push_back({.id = SINK_ID});
    total_operations += 2;
  }

  [[nodiscard]] auto
  operator[](const Link &link) noexcept -> LinkTransmissions & {
    return map[link];
  }
  [[nodiscard]] auto
  operator[](const Link &link) const noexcept -> const LinkTransmissions & {
    return map.at(link);
  }
  [[nodiscard]] auto
  operator[](GlobalOpIndex id) noexcept -> TransmissionOperation & {
    return operations[id];
  }
  [[nodiscard]] auto
  operator[](GlobalOpIndex id) const noexcept -> const TransmissionOperation & {
    return operations[id];
  }

  [[nodiscard]] auto src() noexcept -> TransmissionOperation & {
    return operations[SOURCE_ID];
  }
  [[nodiscard]] auto src() const noexcept -> const TransmissionOperation & {
    return operations[SOURCE_ID];
  }
  [[nodiscard]] auto sink() noexcept -> TransmissionOperation & {
    return operations[SINK_ID];
  }
  [[nodiscard]] auto sink() const noexcept -> const TransmissionOperation & {
    return operations[SINK_ID];
  }
};

} // namespace tsndgm

#endif // TSN_DGM_TRANSMISSION_OPERATIONS_H
