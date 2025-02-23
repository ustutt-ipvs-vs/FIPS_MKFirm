#define NDEBUG

#include "heuristic/initial/initial.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include <argparse/argparse.hpp>
#include <cassert>
#include <print>

using namespace tsndgm;

template <typename Heuristic>
void execute_heuristic(const StreamStorage &stream_storage, const NetworkTopology &network,
                       const std::filesystem::path tsn_config_file) {
  StreamId count = 0;
  Heuristic heuristic(&stream_storage, &network);
  for (StreamId id = 0; id < stream_storage.streams.size(); id++) {
    auto accepted = heuristic.add_stream(id);
    std::println("Result: {} {}", stream_storage.streams[id].name, accepted);
    count += accepted ? 1 : 0;
  }
  if (count > 0) {
    auto tsn_configuration = heuristic.g.derive_tsn_configuration();
    tsn_configuration.add_meta_data("generated_by", "benchmarks/heuristic.cc");
    tsn_configuration.dump_to_file(tsn_config_file);
  }
  std::println("Scheduled {} streams", count);
}

int main(int argc, char **argv) {
  argparse::ArgumentParser program("Heuristics for Wireless IEEE 802.1Qbv Scheduling");
  program.add_argument("-n", "--network")
      .default_value(std::string("../data/network.json"))
      .required()
      .help("specify the network topology");
  program.add_argument("-s", "--streams")
      .default_value(std::string("../data/streams.json"))
      .required()
      .help("specify the time-triggered streams");
  program.add_argument("-o", "--output")
      .default_value(std::string("../data/tsn_configuration.json"))
      .required()
      .help("output file of the final TSN configuration");
  program.add_argument("-sti", "--strict_temporal_isolation")
      .default_value(false)
      .implicit_value(true)
      .help("use strict temporal isolation constraints (i.e., no batching)");
  program.parse_args(argc, argv);

  auto network_file = std::filesystem::path(program.get<std::string>("-n"));
  auto network = NetworkTopology(network_file);
  auto stream_file = std::filesystem::path(program.get<std::string>("-s"));
  auto stream_storage = StreamStorage(stream_file, network);
  auto tsn_config_file = std::filesystem::path(program.get<std::string>("-o"));

  if (program.get<bool>("-sti")) {
    execute_heuristic<StrictTemporalIsolationHeuristic>(stream_storage, network, tsn_config_file);
  } else {
    execute_heuristic<IncrementalHeuristic>(stream_storage, network, tsn_config_file);
  }

  return 0;
}
