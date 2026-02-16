#include "stream.h"
#include "histogram.h"
#include "nlohmann/json_fwd.hpp"
#include "topology.h"
#include "utils/generator.h"
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <print>
#include <string>
#include <utility>
#include <vector>

namespace tsndgm {

auto MKFirmLatencyRequirement::load_from_json(const nlohmann::json &json)
    -> MKFirmLatencyRequirement {
  return {.mask = load_mask(json["mask"]), .e2e_latency = json["latency"]};
}

auto MKFirmLatencyRequirement::load_mask(std::string mask) -> std::vector<bool> {
  std::vector<bool> bmask;
  std::ranges::transform(mask, std::back_inserter(bmask), [](char c) -> bool { return c == '1'; });
  return bmask;
}

auto StableQoSRequest::load_from_json(const nlohmann::json &json) -> StableQoSRequest {
  return {
      .objective_type =
          json_get_or_default<>(json["objective_type"], static_cast<StreamObjective>(TARDINESS)),
      .e2e_latency = json["latency"],
      .jitter = json["jitter"],
      .reliability = 1,
  };
}

auto Stream::load_from_json(nlohmann::json &&json, const NetworkTopology &network) -> Stream {
  auto const frame_size = FrameSizeRange(json["frame_size"]);
  Stream stream = {
      .route = Route(std::move(json["route"]), network),
      .frame_size = frame_size,
      .period = json["period"],
      .phase = json["phase"],
      .pcp = json_get_or_default<>(json["pcp"], static_cast<PCPValue>(DefaultPCP)),
      .mk_firm = json_get_or_default<MKFirmLatencyRequirement>(json["mk_firm"]),
      .stable_qos = json_get_or_default<StableQoSRequest>(json["stable_qos"]),
      .name = json["name"],
  };

  if (json["mk_firm"].is_null() && json["stable_qos"].is_null()) {
    std::println("Warning: {} has neither (m,k)-firm requirement nor stable QoS request",
                 stream.name);
  }

  if (json["pdb_map"].is_null()) {
    return stream;
  }

  for (const auto &j_entry : json["pdb_map"]) {
    Link const link = Link(j_entry["link"][0], j_entry["link"][1]);
    PDB const pdb = PDB::wireless_pdb(network[link.source], network[link.target], frame_size,
                                      DelayHistogram(std::filesystem::path(j_entry["histogram"])),
                                      j_entry["reliability"],
                                      json_get_or_default<>(j_entry["policy"], MINIMIZE_INTERVAL));
    stream.pdb_map.insert({link, pdb});
    stream.stable_qos.reliability *= j_entry["reliability"].template get<double>();
  }

  return stream;
}

void Stream::populate_wireline_pdbs() {
  for (auto [source, target] : route.traverse_links()) {
    Link const link(source->id, target->id);
    if (!pdb_map.contains(link)) {
      pdb_map.insert({link, PDB::wireline_pdb(*source, *target, frame_size)});
    }
  }
}

auto Stream::frames(Delay hyper_cycle) const -> Generator<FrameIndex> {
  for (FrameIndex f = 0; f * period < hyper_cycle; f++) {
    co_yield f;
  }
}

auto Stream::objective(DelayInterval arrival_interval, FrameIndex frame) const -> Delay {
  Delay const lateness = arrival_interval.max - (phase + frame * period + stable_qos.e2e_latency);
  Delay const jitter_violation = std::max(
      arrival_interval.max - arrival_interval.min - stable_qos.jitter, static_cast<Delay>(0));
  switch (stable_qos.objective_type) {
  case NO_OBJECTIVE:
    return 0;
  case LATENESS:
    return lateness;
  case TARDINESS:
    return std::max(lateness, static_cast<Delay>(0));
  case JITTER:
    return jitter_violation;
  case TARDINESS_AND_JITTER:
    return std::max(lateness, jitter_violation);
  }
  std::unreachable();
}

} // namespace tsndgm
