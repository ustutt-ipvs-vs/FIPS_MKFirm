#include "heuristic/initial/initial.h"
#include "network/stream_storage.h"
#include "network/topology.h"
#include <argparse/argparse.hpp>
#include <cassert>
#include <print>

using namespace tsndgm;

auto get_jsp_instance(std::string &&arg) -> nlohmann::json {
  auto j = nlohmann::json::parse(std::ifstream("../data/jsp/instances.json"));
  bool is_int = !arg.empty() && std::ranges::find_if(arg, [](unsigned char c) {
                                  return !std::isdigit(c);
                                }) == arg.end();
  if (is_int) {
    int i = std::stoi(arg);
    return j[i];
  } else {
    return *std::ranges::find_if(j, [&arg](auto j) { return j["name"] == arg; });
  }
}

auto build_network(nlohmann::json &j) -> NetworkTopology {
  NetworkTopology network;
  size_t machines = j["machines"];
  for (DeviceId i = 0; i < machines; i++) {
    network.add_device({.id = 2 * i, .name = std::format("M{}#0", 2 * i)});
    network.add_device({.id = 2 * i + 1, .name = std::format("M{}#0", 2 * i + 1)});
  }

  for (DeviceId i = 0; i < machines; i++) {
    network.add_data_link({{
                               .source = 2 * i,
                               .target = 2 * i + 1,
                           },
                           WIRELESS});
    for (DeviceId j = i + 1; j < machines; j++) {
      network.add_data_link({{
                                 .source = 2 * i + 1,
                                 .target = 2 * j,
                             },
                             WIRELESS});
    }
  }

  return network;
}

auto jsp_pdb(Delay duration) -> PDB {
  return PDB{
      .d_total = duration,
      .d_trans = duration,
  };
}

auto build_jobs(nlohmann::json &j, NetworkTopology &network) -> StreamStorage {
  auto file = std::filesystem::path("../data/jsp/") /
              std::filesystem::path(j["path"].template get<std::string>());
  if (!std::filesystem::exists(file))
    throw std::runtime_error("file does not exist: " + file.string());

  std::ifstream f(file);
  while (f.peek() == '#')
    f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  size_t jobs, machines;
  f >> jobs >> machines;
  if (jobs != j["jobs"].template get<size_t>() || machines != j["machines"].template get<size_t>())
    throw std::runtime_error("instance description and actual data differs");

  std::vector<Stream> streams(jobs);
  for (size_t job = 0; job < jobs; job++) {
    Path path;
    Delay duration;
    DeviceId machine;
    PDBMap pdb_map;
    for (size_t i = 0; i < machines; i++) {
      f >> machine >> duration;
      path.push_back(2 * machine);
      path.push_back(2 * machine + 1);

      pdb_map.insert({Link(2 * machine, 2 * machine + 1), jsp_pdb(duration)});
      if (i > 0) {
        pdb_map.insert({Link(path[2 * i - 1], 2 * machine), jsp_pdb(0)});
      }
    }

    Stream stream{.route = Route(path, network),
                  .frame_size = 0,
                  .period = std::numeric_limits<Delay>::max(),
                  .pcp = static_cast<PCPValue>(job),
                  .name = std::format("J{}", job),
                  .pdb_map = std::move(pdb_map)};
    streams[job] = std::move(stream);
  }

  return StreamStorage(streams);
}

int main(int argc, char **argv) {
  argparse::ArgumentParser program("Job Shop Scheduling Benchmark");
  program.add_argument("benchmark").help("Index or name of benchmark in data/jsp/instances.json");
  program.parse_args(argc, argv);

  auto instance = get_jsp_instance(program.get<std::string>("benchmark"));
  auto network = build_network(instance);
  auto jobs = build_jobs(instance, network);

  // StrictTemporalIsolationHeuristic heuristic(&jobs, &network, MAKESPAN);
  // for (StreamId id = 0; id < jobs.streams.size(); id++) {
  //   if (!heuristic.add_stream(id)) {
  //     return 1;
  //   }
  // }
  // auto &g = heuristic.g;

  TransmissionGraph g(&jobs, &network, MAKESPAN);
  g.critical_path();
  g.print_critical_path();

  return 0;
}
