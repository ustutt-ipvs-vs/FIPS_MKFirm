#include "dgm/transmission_graph.h"
#include "heuristic/initial/initial.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include <argparse/argparse.hpp>
#include <print>

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
  auto streams = StreamStorage(stream_file, network);

  MergingInitialHeuristic heuristic(&streams, &network);
  auto g = heuristic.generate(PER_FRAME);
  auto crit_path = g.critical_path();
  if (crit_path != nullptr) {
    std::println("MergingInitialHeuristic Result: {} {}", crit_path->get_last().objective,
                 g.size());
  } else {
    std::println("MergingInitialHeuristic Result: -");
  }

  return 0;
}
