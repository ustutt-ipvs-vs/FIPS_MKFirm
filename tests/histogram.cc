#include "../src/network/histogram.h"
#include <gtest/gtest.h>

namespace tsndgm {

class HistogramTest : public testing::Test {};

TEST_F(HistogramTest, UniformHistogram) {
  int N = 10;
  Count c = 1000;
  Histogram histogram;
  for (int i = 0; i < N; i++) {
    histogram[i * TicksPerMilliSec] = c;
  }
  histogram[(N + 1) * TicksPerMilliSec] = 0;
  DelayHistogram delay_histogram(histogram);

  Count size = N * c;
  EXPECT_EQ(delay_histogram.size, size);

  for (int i = 0; i < N; i++) {
    EXPECT_EQ(delay_histogram.compute_reliability({0, i * TicksPerMilliSec}),
              static_cast<double>(i) / N);
    EXPECT_EQ(
        delay_histogram.compute_reliability({0, i * TicksPerMilliSec + 999}),
        static_cast<double>(i) / N);
    EXPECT_EQ(
        delay_histogram.compute_reliability({0, 2 * N * TicksPerMilliSec}), 1);
    EXPECT_EQ(delay_histogram.compute_reliability(
                  {N * TicksPerMilliSec, i * TicksPerMilliSec}),
              0);
  }

  NetworkTopology network;
  for (DeviceId i = 0; i < 3; i++) {
    network.add_device({i});
  }
  network.add_data_link({{0, 1}, WIRELESS, Ethernet100Mbps, 50});
  network.add_data_link({{1, 2}, WIRED, Ethernet100Mbps, 50});

  PDB wireline_pdb = PDB::wireline_pdb(network[1], network[2], {100, 200});
  EXPECT_EQ(wireline_pdb.ifg.delay, DelayInterval(0, 960));
  EXPECT_EQ(wireline_pdb.propagation, 50);
  EXPECT_EQ(wireline_pdb.serialization, DelayInterval(8000, 16000));
  EXPECT_EQ(wireline_pdb.processing, DelayInterval(0));
  EXPECT_EQ(wireline_pdb.wireless, DelayInterval(0));
  EXPECT_EQ(wireline_pdb.d_trans, DelayInterval(8000, 16960));
  EXPECT_EQ(wireline_pdb.d_total, DelayInterval(8050, 17010));

  PDB wireless_pdb = PDB::wireless_pdb(network[0], network[1], {100, 200},
                                       delay_histogram, 0.5, MINIMIZE_DMAX);
  EXPECT_EQ(wireless_pdb.ifg.delay, DelayInterval(0, 960));
  EXPECT_EQ(wireless_pdb.propagation, 50);
  EXPECT_EQ(wireless_pdb.serialization, DelayInterval(8000, 16000));
  EXPECT_EQ(wireless_pdb.processing, DelayInterval(0));
  EXPECT_EQ(wireless_pdb.wireless, DelayInterval(0, N / 2 * TicksPerMilliSec));
  EXPECT_EQ(wireless_pdb.d_trans, DelayInterval(8000, 16960));
  EXPECT_EQ(wireless_pdb.d_total,
            DelayInterval(8050, 17010 + N / 2 * TicksPerMilliSec));
}

} // namespace tsndgm

auto main(int argc, char **argv) -> int {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
