#pragma once

#include "histogram.h"
#include "nlohmann/json.hpp"
#include "topology.h"

namespace tsndgm {

using Probability = double;
using PCPValue = unsigned int;
using FrameIndex = unsigned int;
using PDBMap = std::map<Link, PDB>;
using DelayMap = std::map<Link, Delay>;
using StreamId = size_t;

[[maybe_unused]] constexpr PCPValue DefaultPCP = 7;

enum StreamObjective : std::uint8_t {
  NO_OBJECTIVE,
  LATENESS,
  TARDINESS,
  JITTER,
  TARDINESS_AND_JITTER
};

struct MKFirmLatencyRequirement {
  std::vector<bool> mask{false}; // defaults to (0,1)-firm latency requirement
  Delay e2e_latency{0};

  [[nodiscard]] auto m() const noexcept -> Count {
    return std::ranges::fold_left(mask, static_cast<Count>(0),
                                  [](Count c, bool flag) { return c + flag; });
  }
  [[nodiscard]] auto k() const noexcept -> Count { return static_cast<Count>(mask.size()); }
  [[nodiscard]] auto required() const noexcept -> bool { return m() > 0; }

  static auto load_from_json(const nlohmann::json &json) -> MKFirmLatencyRequirement;
  static auto load_mask(std::string mask) -> std::vector<bool>;
};

struct StableQoSRequest {
  StreamObjective objective_type{NO_OBJECTIVE};
  Delay e2e_latency{0};
  Delay jitter{0};
  Probability reliability{0};

  static auto load_from_json(const nlohmann::json &json) -> StableQoSRequest;
};

struct Stream {
  Route route;
  FrameSizeRange frame_size;
  Delay period;
  Delay phase{0};
  PCPValue pcp{DefaultPCP};
  MKFirmLatencyRequirement mk_firm;
  StableQoSRequest stable_qos;
  std::string name;
  PDBMap pdb_map;
  StreamId id{0};

  static auto load_from_json(nlohmann::json &&json, const NetworkTopology &network) -> Stream;

  void populate_wireline_pdbs();

  [[nodiscard]] auto frames(Delay hyper_cycle) const -> Generator<FrameIndex>;
  [[nodiscard]] auto objective(DelayInterval arrival_interval, FrameIndex frame) const -> Delay;
};

struct Frame {
  const Stream *stream;
  FrameIndex id;

  [[nodiscard]] auto name() const -> std::string { return std::format("{}#{}", stream->name, id); }
  auto operator<=>(const Frame &other) const {
    return stream == other.stream ? id <=> other.id : stream <=> other.stream;
  }
};

} // namespace tsndgm
