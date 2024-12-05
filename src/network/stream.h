#ifndef TSN_DGM_STREAM_H
#define TSN_DGM_STREAM_H

#include "histogram.h"
#include "topology.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace tsndgm {

using Probability = double;
using PCPValue = unsigned int;
using FrameIndex = unsigned int;
using PDBMap = std::map<Link, PDB>;
using DelayMap = std::map<Link, Delay>;

struct Stream {
  Route route;
  FrameSizeRange frame_size;
  Delay period;
  Delay phase = 0;
  Delay e2e_latency = 0;
  Delay jitter = 0;
  PCPValue pcp = 7;
  Probability reliability = 1;
  FrameIndex tolerated_loss = 0;
  std::string name;
  PDBMap pdb_map;

  static auto load_from_json(nlohmann::json &&json,
                             const NetworkTopology &network) -> Stream;

  void populate_wireline_pdbs();
  [[nodiscard]] auto frames(Delay hyper_cycle) const -> Generator<FrameIndex>;
};

struct StreamStorage {
  std::vector<Stream> streams;

  StreamStorage() = default;
  StreamStorage(const std::vector<Stream> &streams);

  using Iterator = decltype(streams)::const_iterator;
  [[nodiscard]] auto begin() const -> Iterator { return streams.begin(); }
  [[nodiscard]] auto end() const -> Iterator { return streams.end(); }
};

} // namespace tsndgm

#endif // TSN_DGM_STREAM_H
