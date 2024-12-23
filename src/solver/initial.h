#ifndef TSN_DGM_INITIAL_H
#define TSN_DGM_INITIAL_H

#include "../dgm/transmission_graph.h"
#include "../dgm/transmission_operations.h"
#include "../network/stream_storage.h"

namespace tsndgm {

struct EffectiveRelease {
  EffectiveRelease(const StreamStorage *stream_storage);
  [[nodiscard]] auto generate() -> std::vector<Frame>;

private:
  const StreamStorage *stream_storage_;
  std::deque<TransmissionGraph> stream_graphs_;
  std::deque<StreamStorage> streams_;
  std::map<Link, std::vector<StreamId>> link_to_streams_;

  [[nodiscard]] auto
  get_common_link(StreamId first, StreamId second) const -> std::optional<Link>;
};

} // namespace tsndgm

#endif // TSN_DGM_INITIAL_H
