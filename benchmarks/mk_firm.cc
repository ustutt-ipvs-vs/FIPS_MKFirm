#include "dgm/mk_firm_extension.h"
#include "heuristic/initial/initial.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include <argparse/argparse.hpp>
#include <cassert>
#include <chrono>
#include <print>

using namespace tsndgm;

template <typename Heuristic>
void execute_heuristic(const StreamStorage &stream_storage, const NetworkTopology &network,
                       const std::filesystem::path tsn_config_file,
                       const std::filesystem::path mkfirm_config_file) {
  auto t1 = std::chrono::high_resolution_clock::now();

  StreamId count = 0;
  std::vector<bool> accepted(stream_storage.streams.size());
  Heuristic heuristic(&stream_storage, &network);
  for (StreamId id = 0; id < stream_storage.streams.size(); id++) {
    accepted[id] = heuristic.add_stream(id);
    std::println("Result: {} {}", stream_storage.streams[id].name, accepted[id] ? "true" : "false");
    count += accepted[id] ? 1 : 0;
  }

  if (count > 0) {
    auto tsn_configuration =
        heuristic.g.template derive_tsn_configuration<CriticalPathConfiguration>();
    auto t2 = std::chrono::high_resolution_clock::now();

    auto mkfirm_configuration =
        heuristic.g.template derive_tsn_configuration<MKFirmConfiguration>();
    auto t3 = std::chrono::high_resolution_clock::now();

    if (mkfirm_configuration.status == COMPLETED) {
      tsn_configuration.add_meta_data("generated_by", "benchmarks/mk_firm.cc");
      tsn_configuration.dump_to_file(tsn_config_file);
      mkfirm_configuration.add_meta_data("generated_by", "benchmarks/mk_firm.cc");
      mkfirm_configuration.dump_to_file(mkfirm_config_file);

      count = 0;
      for (StreamId id = 0; id < stream_storage.streams.size(); id++) {
        accepted[id] =
            accepted[id] && !mkfirm_configuration.configuration.stable_qos_violations[id];
        if (accepted[id]) {
          count++;
        }
      }

      std::println("Scheduled {} streams: total {}, augmentation {}", count,
                   duration_cast<std::chrono::milliseconds>(t3 - t1),
                   duration_cast<std::chrono::microseconds>(t3 - t2));
    }
  }
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
  program.add_argument("--output_normal")
      .default_value(std::string("../data/tsn_configuration.json"))
      .required()
      .help("output file of the normal TSN configuration");
  program.add_argument("--output_mkfirm")
      .default_value(std::string("../data/mkfirm_configuration.json"))
      .required()
      .help("output file of the (m,k)-firm TSN configuration");
  program.parse_args(argc, argv);

  auto network_file = std::filesystem::path(program.get<std::string>("-n"));
  auto network = NetworkTopology(network_file);
  auto stream_file = std::filesystem::path(program.get<std::string>("-s"));
  auto stream_storage = StreamStorage(stream_file, network);
  auto tsn_config_file = std::filesystem::path(program.get<std::string>("--output_normal"));
  auto mkfirm_config_file = std::filesystem::path(program.get<std::string>("--output_mkfirm"));

  execute_heuristic<IncrementalHeuristic>(stream_storage, network, tsn_config_file,
                                          mkfirm_config_file);

  return 0;
}
