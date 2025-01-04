#include "heuristic/initial/initial.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include <argparse/argparse.hpp>
#include <print>

// #define NDEBUG
// #include <cassert>

using namespace tsndgm;

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
  program.parse_args(argc, argv);

  auto network_file = std::filesystem::path(program.get<std::string>("-n"));
  auto network = NetworkTopology(network_file);
  auto stream_file = std::filesystem::path(program.get<std::string>("-s"));
  auto stream_storage = StreamStorage(stream_file, network);

  StreamId count = 0;
  IncrementalHeuristic heuristic(&stream_storage, &network);
  for (StreamId id = 0; id < stream_storage.streams.size(); id++) {
    auto accepted = heuristic.add_stream(id);
    std::println("Result: {} {}", stream_storage.streams[id].name, accepted);
    count += accepted ? 1 : 0;
  }

  heuristic.g.critical_path();
  heuristic.g.print_critical_cost();

  std::println("Scheduled {} streams", count);

  return 0;
}
