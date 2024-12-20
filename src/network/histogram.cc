#include "histogram.h"
#include "nlohmann/json_fwd.hpp"
#include "topology.h"
#include <bits/ranges_algo.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace tsndgm {

void DelayHistogram::verify_upper_bound() const {
  auto it = histogram.end();
  if ((--it)->second != 0) {
    throw std::invalid_argument("Histogram does not have an upper bound (last "
                                "bin should have a count of zero)");
  }
}

DelayHistogram::DelayHistogram(Histogram histogram, Count size,
                               std::string name)
    : histogram(std::move(histogram)), size(size),
      name(std::move(std::move(name))) {
  verify_upper_bound();
};

DelayHistogram::DelayHistogram(const Histogram &histogram, std::string name)
    : histogram(histogram),
      size(std::ranges::fold_left(
          histogram, static_cast<Count>(0),
          [](Count c, auto it) { return c + it.second; })),
      name(std::move(std::move(name))) {
  verify_upper_bound();
};

DelayHistogram::DelayHistogram(nlohmann::json hist_data)
    : histogram({}), size(0), name(hist_data["name"]) {
  Delay lower_bound = 0;
  for (auto bin : hist_data["data"]) {
    if (!histogram.empty()) {
      histogram[lower_bound] = bin["count"].template get<Delay>();
      size += histogram[lower_bound];
    }
    if (!bin["upper_bound"].is_null()) {
      std::string const ub = bin["upper_bound"].template get<std::string>();
      std::string const delay_str = ub.substr(0, ub.find(' '));
      lower_bound = static_cast<Delay>(stod(delay_str) * TicksPerMilliSec);
      histogram[lower_bound] = 0;
    }
  }
  verify_upper_bound();
}

auto DelayHistogram::compute_pdb(double reliability,
                                 PDBPolicy policy) const -> DelayInterval {
  if (reliability == 0) {
    return {0};
  }

  if (policy == MINIMIZE_INTERVAL) {
    DelayInterval pdb(std::numeric_limits<Delay>::max());
    for (auto min_it = histogram.begin(); min_it != histogram.end(); ++min_it) {
      Count c = 0;
      auto max_it = min_it;
      for (; max_it != histogram.end(); ++max_it) {
        if (compute_reliability(c) >= reliability) {
          if (max_it->first - min_it->first < pdb.max - pdb.min) {
            pdb = DelayInterval(min_it->first, max_it->first);
          }
          break;
        }
        c += max_it->second;
      }
    }
    return pdb;
  }
  if (policy == MINIMIZE_DMAX) {
    Count c = 0;
    Delay const min = histogram.begin()->first;
    for (const auto &bin : histogram) {
      if (compute_reliability(c) >= reliability) {
        return {min, bin.first};
      }
      c += bin.second;
    }
  }
  std::unreachable();
}

auto DelayHistogram::compute_reliability(DelayInterval interval) const
    -> double {
  Count c = 0;

  auto lower = histogram.contains(interval.min)
                   ? --histogram.upper_bound(interval.min)
                   : histogram.upper_bound(interval.min);
  if (lower == histogram.end()) {
    return 0;
  }

  auto upper = histogram.upper_bound(interval.max);
  if (upper == histogram.end()) {
    for (auto it = lower; it != histogram.end(); ++it) {
      c += it->second;
    }
  } else {
    upper--;
    for (auto it = lower; it->first < upper->first; ++it) {
      c += it->second;
    }
  }

  return compute_reliability(c);
}

auto DelayHistogram::compute_reliability(Count c) const -> double {
  return static_cast<double>(c) / static_cast<double>(size);
}

InterFrameGap::InterFrameGap(const DataLinkProperty &data_link) {
  if (data_link.data_rate == 0) {
    trailing_bytes = 0;
    delay = DelayInterval(0);
  } else {
    trailing_bytes = IFGBytes;
    delay = DelayInterval(0, (IFGBytes * BitsPerByte * TicksPerSec) /
                                 data_link.data_rate);
  }
}

auto PacketDelayBudget::wireline_pdb(
    const DeviceProperty &source, const DeviceProperty &target,
    FrameSizeRange frame_size) -> PacketDelayBudget {
  const auto &data_link = source[target.id];
  PacketDelayBudget pdb = {
      .wireless = DelayInterval(0),
      .processing = target.processing_delay,
      .propagation = data_link.propagation_delay,
      .ifg = InterFrameGap(data_link),
  };

  pdb.serialization =
      (frame_size * BitsPerByte * TicksPerSec) / data_link.data_rate,
  pdb.d_trans = pdb.serialization + pdb.ifg.delay;
  pdb.d_total = pdb.d_trans + pdb.propagation + pdb.processing;
  return pdb;
}

auto PacketDelayBudget::wireless_pdb(const DeviceProperty &source,
                                     const DeviceProperty &target,
                                     FrameSizeRange frame_size,
                                     const DelayHistogram &hist,
                                     double reliability,
                                     PDBPolicy policy) -> PacketDelayBudget {
  auto pdb = PacketDelayBudget::wireline_pdb(source, target, frame_size);
  pdb.wireless = hist.compute_pdb(reliability, policy);
  pdb.d_trans = pdb.serialization + pdb.ifg.delay;
  pdb.d_total = pdb.d_trans + pdb.wireless + pdb.propagation + pdb.processing;
  return pdb;
}

void PacketDelayBudget::merge(const PacketDelayBudget &other) {
  serialization = serialization + other.serialization;
  ifg += other.ifg;
  wireless.merge(other.wireless);
  processing.merge(other.processing); // they are likely identical...

  d_trans = serialization + ifg.delay;
  d_total = d_trans + wireless + propagation + processing;
}

} // namespace tsndgm
