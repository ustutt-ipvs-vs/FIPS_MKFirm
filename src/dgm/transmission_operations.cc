#include "transmission_operations.h"
#include "network/histogram.h"
#include "network/topology.h"
#include <utility>

namespace tsndgm {

auto TransmissionWeights::operator[](EdgeType type) const -> WeightPair {
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

void TransmissionWeights::merge(const TransmissionWeights &other) {
  auto merge_pairs = [&](auto &first, auto &second, auto pdb_val) {
    first.incoming = std::max(first.incoming, second.incoming);
    first.outgoing = pdb_val;
  };

  pdb.merge(other.pdb);
  merge_pairs(fifo, other.fifo, pdb.d_trans.max);
  merge_pairs(machine, other.machine, pdb.d_trans.max);
  merge_pairs(job, other.job, pdb.d_total.max);
}

ProcessingOrder::ProcessingOrder() : operations({{.id = SOURCE_ID}, {.id = SINK_ID}}) {}

ProcessingOrder::ProcessingOrder(const ProcessingOrder &other) noexcept
    : operations(other.operations), map(other.map), total_operations(other.total_operations) {
  relink_pointers();
}

ProcessingOrder::ProcessingOrder(ProcessingOrder &&other) noexcept
    : operations(std::move(other.operations)), map(std::move(other.map)),
      total_operations(other.total_operations) {
  relink_pointers();
}

auto ProcessingOrder::operator=(const ProcessingOrder &other) noexcept -> ProcessingOrder & {
  if (this == &other) {
    return *this;
  }

  operations = other.operations;
  map = other.map;
  total_operations = other.total_operations;
  relink_pointers();
  return *this;
}

auto ProcessingOrder::operator=(ProcessingOrder &&other) noexcept -> ProcessingOrder & {
  if (this == &other) {
    return *this;
  }

  std::swap(operations, other.operations);
  std::swap(map, other.map);
  total_operations = other.total_operations;
  relink_pointers();
  return *this;
}

void ProcessingOrder::relink_pointers() noexcept {
  for (auto &[link, transmissions] : map) {
    for (auto &op_ptr : transmissions) {
      op_ptr = &operations[op_ptr->id];
    }
  }

  for (auto &op : operations) {
    for (auto &op_ptr : op.route_pred) {
      op_ptr = &operations[op_ptr->id];
    }
    for (auto &op_ptr : op.route_succ) {
      op_ptr = &operations[op_ptr->id];
    }
  }
}

auto ProcessingOrder::operator[](const Link &link) noexcept -> LinkTransmissions & {
  return map[link];
}

auto ProcessingOrder::operator[](const Link &link) const noexcept -> const LinkTransmissions & {
  return map.at(link);
}

auto ProcessingOrder::operator[](GlobalOpIndex id) noexcept -> TransmissionOperation & {
  return operations[id];
}

auto ProcessingOrder::operator[](GlobalOpIndex id) const noexcept -> const TransmissionOperation & {
  return operations[id];
}

auto ProcessingOrder::number_of_transmissions(const Link &link) const noexcept -> LinkOpPosition {
  auto it = map.find(link);
  return it == map.end() ? 0 : it->second.size();
}

auto ProcessingOrder::src() noexcept -> TransmissionOperation & { return operations[SOURCE_ID]; }

auto ProcessingOrder::src() const noexcept -> const TransmissionOperation & {
  return operations[SOURCE_ID];
}

auto ProcessingOrder::sink() noexcept -> TransmissionOperation & { return operations[SINK_ID]; }

auto ProcessingOrder::sink() const noexcept -> const TransmissionOperation & {
  return operations[SINK_ID];
}

} // namespace tsndgm
