#ifndef TSN_DGM_INTERVAL_H
#define TSN_DGM_INTERVAL_H

#include "nlohmann/json.hpp"

namespace tsndgm {

template <typename T> struct Interval {
  T min;
  T max;

  Interval() = default;
  Interval(T val) : min(val), max(val) {};
  Interval(T min, T max) : min(min), max(max) {};

  auto merge(const Interval &other) -> Interval & {
    min = std::min(min, other.min);
    max = std::max(max, other.max);
    return *this;
  }

  friend auto operator+(Interval lhs, const Interval &rhs) -> Interval {
    lhs.min += rhs.min;
    lhs.max += rhs.max;
    return lhs;
  }
  friend auto operator+(Interval lhs, T val) -> Interval {
    lhs.min += val;
    lhs.max += val;
    return lhs;
  }
  friend auto operator*(Interval lhs, T val) -> Interval {
    lhs.min *= val;
    lhs.max *= val;
    return lhs;
  }
  friend auto operator/(Interval lhs, T val) -> Interval {
    lhs.min /= val;
    lhs.max /= val;
    return lhs;
  }

  friend auto operator==(const Interval &lhs, const Interval &rhs) -> bool {
    return lhs.min == rhs.min && lhs.max == rhs.max;
  }
};

template <typename T> auto json_get_or_default(nlohmann::json j, T default_value) -> T {
  return (j.is_null() ? default_value : j.template get<T>());
}

} // namespace tsndgm

#endif // TSN_DGM_INTERVAL_H
