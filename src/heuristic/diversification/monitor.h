#pragma once

#include "dgm/transmission_graph.h"
#include <thread>

namespace tsndgm {

template <typename Intensification, typename Diversification> struct SolutionMonitor {
  struct ThreadMetrics {
    Delay best_objective;
    const Intensification::Config &int_config;
  };

  void log_solution(Delay objective) noexcept;

private:
  mutable std::mutex mutex_;
  std::map<std::thread::id, ThreadMetrics> statistics_;
};

} // namespace tsndgm
