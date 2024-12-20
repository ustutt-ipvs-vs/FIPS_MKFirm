/* Generator support while std::generator is not available.
 *
 * @author Simon König
 */

#ifndef TSN_DGM_GENERATOR_H
#define TSN_DGM_GENERATOR_H

#include <cassert>
#include <coroutine>
#include <iterator>
#include <ranges>
#include <stack>
#include <type_traits>
#include <utility>

// generator<T>
//
//   A
// |___| <- begin(), we call resume() before passing out begin()
// |   |
// |===| _______ B    A calls co_yield B; we switch the current coroutine to B
// |   | \     |   |
// |   |  \    |___| co_yield T; no special treatment
// |   |   \   |   |
// |   |    \  |   |
// |___| C   \ |___| co_yield T;
// |   |      \|   | -> operator++ results in B.done() == true;
// |   |       |xxx|    thus, we discard B, resume A to bring it to the next
// co_yield point (C) |xxx|
//      <- end() sentinel
//

namespace tsndgm {

namespace detail {

template <typename T> class [[nodiscard]] GeneratorPromise;
template <typename T> class [[nodiscard]] GeneratorIterator;

// only necessary to provide the end iterator and allow the operator != for the
// iterator
struct GeneratorSentinel {};
} // namespace detail

template <typename T> class [[nodiscard]] Gen {
public:
  using promise_type = detail::GeneratorPromise<T>;
  using iterator = detail::GeneratorIterator<T>;
  using sentinel = detail::GeneratorSentinel;
  using coroutine_handle = std::coroutine_handle<promise_type>;

  Gen() noexcept = default;

  // move-only
  Gen(const Gen &) = delete;
  auto operator=(const Gen &) = delete;
  Gen(Gen &&other) noexcept : coro_(std::exchange(other.coro_, nullptr)) {}
  auto operator=(Gen &&other) noexcept -> Gen & {
    coro_ = std::exchange(other.coro_, nullptr);
    return *this;
  }

  ~Gen() noexcept {
    if (coro_) {
      coro_.destroy();
    }
  }

  // Calling begin() does not invalidate iterators.
  // However, an iterator of a generator should only be retrieved once at the
  // start as the generator is a consuming range and interacting with a range
  // through multiple iterators can lead to undefined behavior
  auto begin() {
    auto coro = current_coro();
    // Call resume to get the coroutine to its first co_yield point
    if (coro != nullptr and not started_) {
      coro.resume();
      started_ = true;
    }
    return iterator{*this};
  }

  // Never invalidates iterators and the end() iterator is never invalidated
  auto end() noexcept { return sentinel{}; }

  // We don't expose empty() as it can lead to weird behavior with recursive
  // generators. Compare the iterators if you absolutely need to. Otherwise, use
  // co_yield generator or for(auto v: generator)
  [[nodiscard]] auto empty() const noexcept -> bool = delete;
  // NOTE: weird behavior means that recursive generators don't attach to the
  // caller if you initiate them with the instantiation of an iterator.

private:
  [[nodiscard]] constexpr auto
  current_coro() const noexcept -> coroutine_handle {
    return coro_.promise().current_coro();
  }
  auto mark_current_coro_finished() noexcept {
    auto &promise = coro_.promise();
    while (promise.active_.size() > 1) {
      // The top coroutine must be done at this point
      assert(promise.active_.top().done());
      promise.active_.pop();

      // If we pop a coroutine from the stack of coroutines, we have to bring
      // the caller to its next co_yield point.
      auto coro = promise.current_coro();
      coro.resume();

      // We have to do this over and over again until we resume a coroutine that
      // is
      //  1. not finished after resumption or
      //  2. only one coroutine remains on the active_ stack. If this one is
      //  done() as well,
      //     we have reached end()
      if (not coro.done()) {
        return;
      }
      // NOTE to 1.: if a coroutine body contains
      // co_yield <some other generator>
      // co_return
      // then it will be done after the recursive call finishes.
    }
  }

  friend class detail::GeneratorIterator<T>;
  friend promise_type;

  // used by get_return_object
  explicit Gen(std::coroutine_handle<promise_type> coroutine) noexcept
      : coro_(coroutine) {}

  coroutine_handle coro_;
  // protects that multiple invocations of begin() don't do an implicit
  // operator++
  bool started_ = false;
};

namespace detail {
template <typename T> class GeneratorPromise {
public:
  using value_type = std::remove_reference_t<T>;
  using reference_type = std::conditional_t<std::is_reference_v<T>, T, T &>;
  using pointer_type = value_type *;
  using coroutine_handle = std::coroutine_handle<GeneratorPromise>;

  constexpr GeneratorPromise() noexcept {
    auto self = std::coroutine_handle<GeneratorPromise>::from_promise(*this);
    active_.push(self);
    caller_ = this;
  }

  auto get_return_object() noexcept -> Gen<T>;

  [[nodiscard]] auto initial_suspend() const { return std::suspend_always{}; }

  [[nodiscard]] auto final_suspend() const noexcept {
    return std::suspend_always{};
  }

  // Disallow co_await in the generator as it makes it difficult to figure out
  // whether co_yield was called prior to calling iterator::operator*
  auto await_transform() = delete;

  // yield an intermediate value
  template <typename U>
  auto yield_value(U &&value) noexcept
    requires(std::is_convertible_v<U &&, T> and std::is_reference_v<U &&>)
  {
    value_ = std::addressof(value);
    return std::suspend_always{};
  }

  // enter another generator that functions as a sub-generator
  auto yield_value(Gen<T> &&generator) noexcept {
    struct SuspendMaybe {
      constexpr SuspendMaybe(bool ready) : ready_(ready) {}
      [[nodiscard]] constexpr auto await_ready() const noexcept -> bool {
        return ready_;
      }
      constexpr void await_suspend(std::coroutine_handle<> h) const noexcept {}
      constexpr void await_resume() const noexcept {}

    private:
      bool ready_;
    };
    static constexpr auto suspend_always = SuspendMaybe{false};
    static constexpr auto suspend_never = SuspendMaybe{true};

    auto coro = generator.coro_;
    caller_->active_.push(coro);
    coro.promise().caller_ = caller_;

    coro.resume();
    if (coro.done()) {
      // there was no yield statement in the generator
      // continue the caller instead
      caller_->active_.pop();
      return suspend_never;
    }
    // change the currently active coroutine and suspend the caller
    return suspend_always;
  }

  // end the generator
  auto return_void() noexcept -> void {}

  [[noreturn]] auto unhandled_exception() -> void { throw; }

  [[nodiscard]] auto value() const noexcept -> reference_type {
    return static_cast<reference_type>(*value_);
  }

  [[nodiscard]] constexpr auto
  current_coro() const noexcept -> coroutine_handle {
    return active_.top();
  }

private:
  friend Gen<T>;

  pointer_type value_ = nullptr;
  std::stack<coroutine_handle> active_;

  // Always points to the top-most generator promise in a recursive call chain
  // Is equal to this if this is the top-most promise
  GeneratorPromise *caller_ = nullptr;
};

template <typename T> class GeneratorIterator {
public:
  using iterator_category = std::input_iterator_tag;
  using value_type = typename GeneratorPromise<T>::value_type;
  using difference_type = std::ptrdiff_t;
  using pointer = typename GeneratorPromise<T>::pointer_type;
  using reference = typename GeneratorPromise<T>::reference_type;
  using sentinel_type = GeneratorSentinel;

  GeneratorIterator() noexcept = default;

  explicit GeneratorIterator(Gen<T> &gen) noexcept : generator_(gen) {}

  friend auto operator==(const GeneratorIterator &it,
                         sentinel_type /*unused*/) noexcept -> bool {
    return it.generator_.current_coro().done();
  }

  friend auto operator!=(const GeneratorIterator &it,
                         sentinel_type s) noexcept -> bool {
    return !(it == s);
  }

  // Calling operator++ invalidates other iterators (apart from end())
  auto operator++() -> GeneratorIterator & {
    // we always ensure that the current_coro() is valid and resumable as long
    // as iterator::operator==(*this, end()) returns false;
    auto coro = generator_.current_coro();
    coro.resume();
    if (coro.done()) {
      generator_.mark_current_coro_finished();
    }
    return *this;
  }

  // Calling operator++ invalidates other iterators (apart from end())
  auto operator++(int) -> void { this->operator++(); }

  auto operator*() const noexcept -> reference {
    return generator_.current_coro().promise().value();
  }

  auto operator->() const noexcept -> pointer {
    return std::addressof(this->operator*());
  }

private:
  Gen<T> &generator_;
};

} // namespace detail

namespace detail {
template <typename T>
auto GeneratorPromise<T>::get_return_object() noexcept -> Gen<T> {
  return Gen<T>{
      std::coroutine_handle<GeneratorPromise<T>>::from_promise(*this)};
}

} // namespace detail

template <typename T> using gen = Gen<T>;

template <std::ranges::input_range Range,
          typename T = std::ranges::range_value_t<Range>>
static auto generate(Range &range) noexcept -> gen<T> {
  for (T &v : range) {
    co_yield v;
  }
  co_return;
}

template <typename InputIterator, typename T = InputIterator::value_type>
static auto generate(InputIterator begin,
                     InputIterator end) noexcept -> gen<T> {
  for (auto it = begin; it != end; ++it) {
    co_yield *it;
  }
  co_return;
}

template <typename T> using Generator = Gen<T>;

} // namespace tsndgm

#endif // TSN_DGM_GENERATOR_H
