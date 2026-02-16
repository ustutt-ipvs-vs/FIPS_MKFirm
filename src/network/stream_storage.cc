#include "stream_storage.h"
#include "nlohmann/json_fwd.hpp"
#include "stream.h"
#include "topology.h"
#include "utils/generator.h"
#include <algorithm>
#include <bits/ranges_algo.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <utility>
#include <vector>

namespace tsndgm {

StreamStorage::StreamStorage(const std::vector<Stream> &streams) {
  hyper_cycle =
      std::ranges::fold_left(streams, static_cast<Delay>(1), [](Delay h, auto &stream) -> auto {
        return std::lcm(h, stream.period);
      });
  StreamId s_id = 0;
  for (auto stream : streams) {
    stream.id = s_id++;
    stream.populate_wireline_pdbs();
    this->streams.push_back(std::move(stream));
  }
}

StreamStorage::StreamStorage(nlohmann::json &&json, const NetworkTopology &network) {
  StreamId s_id = 0;
  std::vector<Stream> streams;
  for (auto &json_stream : json) {
    auto stream = Stream::load_from_json(std::move(json_stream), network);
    stream.id = s_id++;
    streams.emplace_back(std::move(stream));
  }
  *this = StreamStorage(streams);
}

StreamStorage::StreamStorage(const std::filesystem::path &in, const NetworkTopology &network)
    : StreamStorage(nlohmann::json::parse(std::ifstream(in)), network) {}

auto StreamStorage::filtered_streams(const std::function<bool(const Stream &)> &stream_filter) const
    -> Generator<const Stream &> {
  for (const auto &stream : streams) {
    if (stream_filter(stream)) {
      co_yield stream;
    }
  }
}

auto StreamStorage::filtered_streams_with_id(
    const std::function<bool(const Stream &)> &stream_filter) const
    -> Generator<std::pair<StreamId, const Stream *>> {
  StreamId s_id = 0;
  for (auto const &stream : streams) {
    if (stream_filter(stream)) {
      co_yield std::make_pair(s_id, &stream);
    }
    s_id++;
  }
}

auto StreamStorage::frames(const std::function<bool(const Stream &)> &stream_filter) const
    -> Generator<Frame> {
  for (const auto &stream : streams) {
    if (!stream_filter(stream)) {
      continue;
    }
    for (FrameIndex const f : stream.frames(hyper_cycle)) {
      auto frame = Frame(&stream, f);
      co_yield frame;
    }
  }
}

auto StreamStorage::number_of_frames(const std::function<bool(const Stream &)> &stream_filter) const
    -> size_t {
  size_t c = 0;
  for (const auto &stream : streams) {
    if (stream_filter(stream)) {
      c += hyper_cycle / stream.period;
    }
  }
  return c;
}

auto StreamStorage::number_of_transmissions(
    const std::function<bool(const Stream &)> &stream_filter) const -> size_t {
  size_t c = 0;
  for (const auto &stream : streams) {
    if (stream_filter(stream)) {
      size_t const stream_links = stream.route.number_of_links();
      c += stream_links * (hyper_cycle / stream.period);
    }
  }
  return c;
}

auto StreamStorage::get_stream_id(const Stream *ptr) const noexcept -> StreamId {
  return std::ranges::find_if(streams, [ptr](auto &stream) -> auto { return &stream == ptr; }) -
         streams.begin();
}

} // namespace tsndgm
