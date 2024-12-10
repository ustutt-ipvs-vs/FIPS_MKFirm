#pragma once
#include "traversal.h"
#include <tuple>
#include <utility>

namespace tsndgm {

template <typename... Functions> class EventVisitor {
public:
  constexpr EventVisitor(
      std::pair<DFSVisitor::Event, Functions> &&...funcs) noexcept
      : funcs_(std::move(funcs)...) {}
  constexpr ~EventVisitor() noexcept = default;

  // copy
  constexpr EventVisitor(const EventVisitor &) noexcept = default;
  constexpr auto
  operator=(const EventVisitor &) noexcept -> EventVisitor & = default;
  // move
  constexpr EventVisitor(EventVisitor &&) noexcept = default;
  constexpr auto
  operator=(EventVisitor &&) noexcept -> EventVisitor & = default;

  template <typename... Args>
  void operator()(DFSVisitor::Event event, Args &&...args) const noexcept {
    [event, this]<size_t... I>(std::index_sequence<I...> unused,
                               Args &&...args) -> void {
        constexpr static auto f = [](auto &pair, DFSVisitor::Event event, Args &&...args) -> void {
        if (pair.first == event) {
          std::invoke(pair.second, args...);
        }
      };
        (std::invoke(f, std::get<I>(funcs_), event, std::forward<Args>(args)...), ...);
    }(std::make_index_sequence<sizeof...(Functions)>(),
      std::forward<Args>(args)...);
  }

private:
  std::tuple<std::pair<DFSVisitor::Event, Functions>...> funcs_;
};
} // namespace tsndgm
