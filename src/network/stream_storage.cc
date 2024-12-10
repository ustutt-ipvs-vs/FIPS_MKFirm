#include "stream_storage.h"
#include "stream.h"
#include "topology.h"
#include <bits/ranges_algo.h>
#include <utility>
#include <vector>

namespace tsndgm {

StreamStorage::StreamStorage(const std::vector<Stream> &streams) {
  hyper_cycle = std::ranges::fold_left(
      streams, static_cast<Delay>(1),
      [](Delay h, auto &stream) { return std::lcm(h, stream.period); });
  for (auto stream : streams) {
    stream.populate_wireline_pdbs();
    this->streams.push_back(std::move(stream));
  }
}

auto StreamStorage::frames() const -> Generator<Frame> {
  for (const auto &stream : streams) {
    for (FrameIndex const f : stream.frames(hyper_cycle)) {
      auto frame = Frame(&stream, f);
      co_yield frame;
    }
  }
}

auto StreamStorage::sorted_frames() const -> Generator<Frame> {
  if (sorted_frames_.empty()) {
    // std::ranges::elements_of not yet available?
    for (Frame &frame : frames()) {
      co_yield frame;
    }
  } else {
    for (Frame frame : sorted_frames_) {
      co_yield frame;
    }
  }
}

void StreamStorage::specify_frame_order(std::vector<Frame> &&sorted_frames) {
  sorted_frames_ = std::move(sorted_frames);
}

} // namespace tsndgm
