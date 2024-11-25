#include "../src/network/topology.h"
#include <gtest/gtest.h>

namespace tsndgm {

class TopologyTest : public testing::Test {};

TEST_F(TopologyTest, LineTopology) {
  NetworkTopology network;
  DeviceId N = 100;

  for (DeviceId i = 0; i < N; i++) {
    const DeviceProperty device = {
        .id = i,
        .type = i == 0 || i == N - 1 ? END_DEVICE : TSN_BRIDGE,
    };
    network.add_device(device);

    EXPECT_EQ(network[i].id, device.id);
    EXPECT_EQ(network[i].type, device.type);

    if (i > 0) {
      const Link link = {.source = i - 1, .target = i};
      const DataLinkProperty data_link = {link, WIRED, Ethernet100Mbps, 50};
      network.add_data_link(data_link);

      EXPECT_EQ(network[link].source, i - 1);
      EXPECT_EQ(network[link].target, i);
      EXPECT_EQ(network[link].type, WIRED);
      EXPECT_EQ(network[link].data_rate, Ethernet100Mbps);
      EXPECT_EQ(network[link].propagation_delay, 50);
    }
  }

  auto json = network.dump_to_json();
  NetworkTopology network1(std::move(json));

  for (DeviceId i = 0; i < N; i++) {
    EXPECT_EQ(network[i].id, network1[i].id);
    EXPECT_EQ(network[i].type, network1[i].type);

    for (auto &data_link : network[i].out) {
      Link link{data_link.source, data_link.target};

      EXPECT_EQ(network1[link].source, network[link].source);
      EXPECT_EQ(network1[link].target, network[link].target);
      EXPECT_EQ(network1[link].type, network1[link].type);
      EXPECT_EQ(network1[link].data_rate, network1[link].data_rate);
      EXPECT_EQ(network1[link].propagation_delay,
                network1[link].propagation_delay);
    }
  }
}

TEST_F(TopologyTest, GridTopology) {
  NetworkTopology network;
  Route route;
  DeviceId N = 100;

  auto device_id = [N](DeviceId i, DeviceId j) { return i * N + j; };

  for (DeviceId i = 0; i < N; i++) {
    for (DeviceId j = 0; j < N; j++) {
      const DeviceProperty device = {
          .id = device_id(i, j),
          .type = (i == 0 || j == 0 || i == N - 1 || j == N - 1) ? END_DEVICE
                                                                 : TSN_BRIDGE,
      };
      network.add_device(device);

      if (i > 0) {
        const Link link = {.source = device_id(i - 1, j),
                           .target = device_id(i, j)};
        const DataLinkProperty data_link = {link, WIRED, Ethernet100Mbps, 50};
        network.add_data_link(data_link);
        route.add_link(link, network, false);
      }
      if (j > 0) {
        const Link link = {.source = device_id(i, j - 1),
                           .target = device_id(i, j)};
        const DataLinkProperty data_link = {link, WIRED, Ethernet100Mbps, 50};
        network.add_data_link(data_link);
        route.add_link(link, network, i == N - 1 && j == N - 1);
      }
    }
  }

  EXPECT_EQ(route.talkers().size(), 1);
  EXPECT_EQ(route.listeners().size(), 1);
  for (DeviceId i = 0; i < N; i++) {
    for (DeviceId j = 0; j < N; j++) {
      EXPECT_EQ(route[device_id(i, j)].is_talker(), i == 0 && j == 0);
      EXPECT_EQ(route[device_id(i, j)].is_listener(), i == N - 1 && j == N - 1);
      EXPECT_EQ(route[device_id(i, j)].is_replication_point(),
                i < N - 1 && j < N - 1);
      EXPECT_EQ(route[device_id(i, j)].is_elimination_point(), i > 0 && j > 0);
    }
  }
  EXPECT_EQ(route[0].is_talker(), true);
}

} // namespace tsndgm

auto main(int argc, char **argv) -> int {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
