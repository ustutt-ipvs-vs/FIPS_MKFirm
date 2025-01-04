#pragma once

#include "generator.h"
#include <cstdint>
#include <set>
#include <vector>

namespace tsndgm {

using PartialOrder = std::vector<std::set<size_t>>;

namespace detail {
enum Color : std::uint8_t { WHITE, GRAY, BLACK };

template <typename T>
auto topological_sort(std::vector<T> &collection, PartialOrder &partial_order,
                      std::vector<Color> &color, size_t cur) -> Generator<T> {
  switch (color[cur]) {
  case WHITE:
    color[cur] = GRAY;
    for (auto pred : partial_order[cur]) {
      co_yield topological_sort(collection, partial_order, color, pred);
    }
    break;
  case GRAY:
    assert(false);
  case BLACK:
    co_return;
  }

  color[cur] = BLACK;
  co_yield collection[cur];
}

template <typename T>
auto topological_sort(std::vector<T> &collection, PartialOrder &partial_order) -> Generator<T> {
  auto color = std::vector<Color>(collection.size(), WHITE);
  for (size_t i = 0; i < color.size(); i++) {
    if (color[i] == WHITE) {
      co_yield topological_sort(collection, partial_order, color, i);
    }
  }
}
} // namespace detail

template <typename T>
auto topological_sort(std::vector<T> &collection, PartialOrder &partial_order) -> Generator<T> {
  co_yield detail::topological_sort(collection, partial_order);
}

} // namespace tsndgm
