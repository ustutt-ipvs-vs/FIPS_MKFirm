#pragma once

#include "stream.h"
#include "topology.h"

namespace tsndgm {

[[maybe_unused]] constexpr auto default_stream_filter = [](const Stream & /*stream*/) -> bool {
  return true;
};
[[maybe_unused]] constexpr auto wireless_stream_filter = [](const Stream &stream) -> bool {
  return stream.route.has_wireless_links();
};
[[maybe_unused]] constexpr auto wired_stream_filter = [](const Stream &stream) -> bool {
  return !stream.route.has_wireless_links();
};

struct StreamStorage {
  Delay hyper_cycle;
  std::vector<Stream> streams;

  StreamStorage() = default;
  StreamStorage(const std::vector<Stream> &streams);
  StreamStorage(nlohmann::json &&json, const NetworkTopology &network);
  StreamStorage(const std::filesystem::path &in, const NetworkTopology &network);

  StreamStorage(const StreamStorage &other) = default;
  auto operator=(const StreamStorage &) -> StreamStorage & = default;
  StreamStorage(StreamStorage &&other) = default;
  auto operator=(StreamStorage &&) -> StreamStorage & = default;

  [[nodiscard]] auto
  filtered_streams(const std::function<bool(const Stream &)> &stream_filter) const
      -> Generator<const Stream &>;
  [[nodiscard]] auto
  filtered_streams_with_id(const std::function<bool(const Stream &)> &stream_filter) const
      -> Generator<std::pair<StreamId, const Stream *>>;
  [[nodiscard]] auto
  frames(const std::function<bool(const Stream &)> &stream_filter = default_stream_filter) const
      -> Generator<Frame>;

  [[nodiscard]] auto number_of_frames(const std::function<bool(const Stream &)> &stream_filter =
                                          default_stream_filter) const -> size_t;
  [[nodiscard]] auto number_of_transmissions(
      const std::function<bool(const Stream &)> &stream_filter = default_stream_filter) const
      -> size_t;

  [[nodiscard]] auto get_stream_id(const Stream *ptr) const noexcept -> StreamId;
};

} // namespace tsndgm
