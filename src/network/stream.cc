#include "stream.h"
#include "histogram.h"
#include "nlohmann/json_fwd.hpp"
#include "topology.h"
#include "utils/generator.h"
#include <algorithm>
#include <filesystem>
#include <limits>
#include <utility>

namespace tsndgm {

auto Stream::load_from_json(nlohmann::json &&json, const NetworkTopology &network) -> Stream {
  auto const frame_size = FrameSizeRange(json["frame_size"]);
  Stream stream = {.route = Route(std::move(json["route"]), network),
                   .frame_size = frame_size,
                   .period = json["period"],
                   .phase = json["phase"],
                   .objective_type = json_get_or_default<>(json["objective_type"],
                                                           static_cast<StreamObjective>(TARDINESS)),
                   .e2e_latency = json["e2e_latency"],
                   .jitter = json["jitter"],
                   .pcp = json_get_or_default<>(json["pcp"], static_cast<PCPValue>(DefaultPCP)),
                   .reliability = 1,
                   .tolerated_loss = json_get_or_default<>(json["frame_loss"],
                                                           std::numeric_limits<FrameIndex>::max()),
                   .name = json["name"]};

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
    stream.reliability *= j_entry["reliability"].template get<double>();
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
  Delay const lateness = arrival_interval.max - (phase + frame * period + e2e_latency);
  Delay const jitter_violation =
      std::max(arrival_interval.max - arrival_interval.min - jitter, static_cast<Delay>(0));
  switch (objective_type) {
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
