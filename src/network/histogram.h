#ifndef TSN_DGM_HISTOGRAM_H
#define TSN_DGM_HISTOGRAM_H

#include "topology.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

namespace tsndgm {

using Count = unsigned long long;
using Bits = Count;
using Bytes = Count;
using Histogram = std::map<Delay, Count>;
enum PDBPolicy : std::uint8_t { MINIMIZE_INTERVAL, MINIMIZE_DMAX };

[[maybe_unused]] constexpr Bits BitsPerByte = 8;

struct DelayHistogram {
  Histogram histogram;
  Count size;
  std::string name;

  DelayHistogram(const Histogram &histogram, std::string name = "");
  DelayHistogram(Histogram histogram, Count size, std::string name = "");
  explicit DelayHistogram(const std::filesystem::path &hist_path)
      : DelayHistogram(std::ifstream(hist_path)) {};
  explicit DelayHistogram(std::ifstream hist_istream)
      : DelayHistogram(nlohmann::json::parse(hist_istream)) {};
  explicit DelayHistogram(nlohmann::json hist_data);

  [[nodiscard]] auto
  compute_pdb(double reliability,
              PDBPolicy policy = MINIMIZE_INTERVAL) const -> DelayInterval;
  [[nodiscard]] auto
  compute_reliability(DelayInterval interval) const -> double;
  [[nodiscard]] auto compute_reliability(Count c) const -> double;

private:
  void verify_upper_bound() const;
};

struct InterFrameGap {
  DelayInterval delay;
  Bytes trailing_bytes;

  InterFrameGap() = default;
  explicit InterFrameGap(const DataLinkProperty &data_link);
};
using IFG = InterFrameGap;

using FrameSize = Bytes;
using FrameSizeRange = Interval<FrameSize>;

struct PacketDelayBudget {
  DelayInterval d_total;
  DelayInterval d_trans;
  DelayInterval wireless;
  DelayInterval processing;
  DelayInterval serialization;
  Delay propagation;
  InterFrameGap ifg;

  PacketDelayBudget(const NetworkTopology &network,
                    const DataLinkProperty &data_link,
                    FrameSizeRange frame_size);
  PacketDelayBudget(const NetworkTopology &network,
                    const DataLinkProperty &data_link,
                    FrameSizeRange frame_size, const DelayHistogram &hist,
                    double reliability, PDBPolicy policy = MINIMIZE_INTERVAL);
};
using PDB = PacketDelayBudget;

} // namespace tsndgm

#endif // TSN_DGM_HISTOGRAM_H
