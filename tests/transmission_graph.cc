#include "../src/dgm/transmission_graph.h"
#include <gtest/gtest.h>
#include <print>

namespace tsndgm {

class TransmissionGraphTest : public testing::Test {
protected:
  TransmissionGraphTest() {
    build_network();
    std::vector<Stream> streams;
    for (size_t i = 0; i < Ns; i++) {
      streams.push_back({.route = build_route(i % Ny),
                         .frame_size = 100,
                         .period = 100000 * ((i % 5) + 1),
                         .name = std::format("Stream{}", i)});
      std::println("Stream {}", i);
      streams.back().route.print_tree();
    }
    this->streams = StreamStorage(streams);
  }

  DeviceId device_id(DeviceId x, DeviceId y) { return x * Ny + y; }

  // Nx * Ny grid
  void build_network() {
    for (DeviceId i = 0; i < Nx; i++) {
      for (DeviceId j = 0; j < Ny; j++) {
        const DeviceProperty device = {
            .id = device_id(i, j),
            .type = (i == 0 || j == 0 || i == Nx - 1 || j == Ny - 1)
                        ? END_DEVICE
                        : TSN_BRIDGE,
        };
        network.add_device(device);

        if (i > 0) {
          const Link link = {.source = device_id(i - 1, j),
                             .target = device_id(i, j)};
          const DataLinkProperty data_link = {link, WIRED, Ethernet100Mbps, 50};
          network.add_data_link(data_link);
        }
        if (j > 0) {
          const Link link = {.source = device_id(i, j - 1),
                             .target = device_id(i, j)};
          const DataLinkProperty data_link = {link, WIRED, Ethernet100Mbps, 50};
          network.add_data_link(data_link);
        }
      }
    }
  }

  // mixture of elimination and replication points
  Route build_route(DeviceId y) {
    Route route;
    for (int j : {-1, 1}) {
      Path path;
      for (DeviceId i = 0; i < Nx; i++) {
        path.push_back(device_id(i, y));
        if (i == Nx / 5 || i == 3 * Nx / 5) {
          if (y + j < Ny && y + j >= 0) {
            y += j;
            path.push_back(device_id(i, y));
          }
        } else if (i == 2 * Nx / 5 || i == 4 * Nx / 5) {
          if (y - j < Ny && y - j >= 0) {
            y -= j;
            path.push_back(device_id(i, y));
          }
        }
      }
      route.add_path(path, network);
    }
    return route;
  }

  const DeviceId Nx = 10;
  const DeviceId Ny = 5;
  const size_t Ns = 20;
  NetworkTopology network;
  StreamStorage streams;
};

TEST_F(TransmissionGraphTest, BuildTransmissionGraph) {
  TransmissionGraph g = TransmissionGraph(streams);
  auto res = g.critical_path();
  std::println("Result: {} {}", res.critical_vertex->id, res.objective);
}

} // namespace tsndgm

auto main(int argc, char **argv) -> int {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
