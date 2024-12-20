#ifndef TSN_DGM_STREAM_STORAGE_H
#define TSN_DGM_STREAM_STORAGE_H

#include "histogram.h"
#include "stream.h"
#include "topology.h"

namespace tsndgm {

struct StreamStorage {
  std::vector<Stream> streams;
  Delay hyper_cycle;

  StreamStorage() = default;
  StreamStorage(const std::vector<Stream> &streams);
  StreamStorage(const StreamStorage &other) = default;
  auto operator=(const StreamStorage &) -> StreamStorage & = default;
  StreamStorage(StreamStorage &&other) = default;
  auto operator=(StreamStorage &&) -> StreamStorage & = default;

  void specify_frame_order(std::vector<Frame> &&sorted_frames);

  using Iterator = decltype(streams)::const_iterator;
  [[nodiscard]] auto begin() const -> Iterator { return streams.begin(); }
  [[nodiscard]] auto end() const -> Iterator { return streams.end(); }

  [[nodiscard]] auto frames() const -> Generator<Frame>;
  [[nodiscard]] auto sorted_frames() const -> Generator<Frame>;

  [[nodiscard]] auto number_of_transmissions() const -> size_t;

private:
  std::vector<Frame> sorted_frames_;
};

} // namespace tsndgm

#endif // TSN_DGM_STREAM_STORAGE_H
