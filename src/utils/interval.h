#ifndef TSN_DGM_INTERVAL_H
#define TSN_DGM_INTERVAL_H

namespace tsndgm {

template <typename T> struct Interval {
  T min;
  T max;

  Interval() = default;
  Interval(T val) : min(val), max(val) {};
  Interval(T min, T max) : min(min), max(max) {};

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

} // namespace tsndgm

#endif // TSN_DGM_INTERVAL_H
