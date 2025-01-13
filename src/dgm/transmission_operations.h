#pragma once

#include "network/histogram.h"
#include "network/stream.h"
#include "network/topology.h"
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
  }

  auto operator[](EdgeType type) const -> WeightPair;

  void merge(const TransmissionWeights &other);
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
  [[nodiscard]] auto contains(const Stream *stream) const {
    return std::ranges::find_if(frames, [stream](auto &f) { return f.stream == stream; }) !=
           frames.end();
  }
  [[nodiscard]] auto contains(Frame frame) const { return frames.contains(frame); }
  [[nodiscard]] auto valid() const { return !frames.empty(); }
};

using LinkTransmissions = std::vector<TransmissionOperation *>;
using OperationPosition = std::vector<std::pair<LinkTransmissions *, LinkOpPosition>>;

[[maybe_unused]] static constexpr GlobalOpIndex SOURCE_ID = 0;
[[maybe_unused]] static constexpr GlobalOpIndex SINK_ID = 1;

struct ProcessingOrder {
  std::vector<TransmissionOperation> operations;
  std::map<Link, LinkTransmissions> map;
  GlobalOpIndex total_operations{2};

  ProcessingOrder();

  ProcessingOrder(const ProcessingOrder &other) noexcept;
  ProcessingOrder(ProcessingOrder &&other) noexcept;
  auto operator=(const ProcessingOrder &other) noexcept -> ProcessingOrder &;
  auto operator=(ProcessingOrder &&other) noexcept -> ProcessingOrder &;
  ~ProcessingOrder() = default;

  [[nodiscard]] auto operator[](const Link &link) noexcept -> LinkTransmissions &;
  [[nodiscard]] auto operator[](const Link &link) const noexcept -> const LinkTransmissions &;
  [[nodiscard]] auto operator[](GlobalOpIndex id) noexcept -> TransmissionOperation &;
  [[nodiscard]] auto operator[](GlobalOpIndex id) const noexcept -> const TransmissionOperation &;

  [[nodiscard]] auto number_of_transmissions(const Link &link) const noexcept -> LinkOpPosition;

  [[nodiscard]] auto src() noexcept -> TransmissionOperation &;
  [[nodiscard]] auto src() const noexcept -> const TransmissionOperation &;
  [[nodiscard]] auto sink() noexcept -> TransmissionOperation &;
  [[nodiscard]] auto sink() const noexcept -> const TransmissionOperation &;

  [[nodiscard]] auto
  traverse_operations(Frame frame) const noexcept -> Generator<const TransmissionOperation *>;

private:
  void relink_pointers() noexcept;
};

} // namespace tsndgm
