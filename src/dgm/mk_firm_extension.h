#pragma once

#include "critical_path.h"
#include "network/histogram.h"
#include "network/stream_storage.h"
#include "transmission_operations.h"
#include "traversal.h"

namespace tsndgm {

struct TokenBucket {
  Bytes bucket_size{0};
  DataRate token_rate{0};
  DataRate link_data_rate{0};
};

struct ElevationCount {
  const Stream *stream;

  ElevationCount() = default;
  ElevationCount(const Stream *stream) noexcept : stream(stream) {}

  auto operator()(Delay t1, Delay t2) const noexcept -> Count;
  auto operator()(Delay t) const noexcept { return (*this)(t, t); }
};

struct ElevationStepFunctions {
  Delay hyper_cycle{1};
  std::vector<ElevationCount> funcs;
  std::set<Delay> increments;
  std::set<Delay> decrements;

  ElevationStepFunctions(Generator<const Stream *> &&streams) noexcept;
};

struct MKFirmPSFPGate {
  std::set<Frame> frames;
  Delay open;
  Delay close;
};

using MKFirmPSFPConfiguration = std::map<DeviceId, std::deque<MKFirmPSFPGate>>;

struct MKFirmConfiguration {
  MKFirmPSFPConfiguration mkfirm_psfp_config;
  std::vector<bool> stable_qos_violations;

  MKFirmConfiguration() = default;
  MKFirmConfiguration(DFSTraversal &dfs, const ProcessingOrder &processing_order,
                      const StreamStorage &streams) noexcept;

  constexpr auto traversal_events() {
    return critical_path_.traversal_events().add(std::make_tuple(
        std::make_pair(DFSVisitor::DISCOVER_VERTEX,
                       [&](auto v) -> auto { return this->visitor_discover_vertex(v); }),
        std::make_pair(DFSVisitor::FINISH_VERTEX,
                       [&](auto v) -> auto { return this->visitor_finish_vertex(v); }),
        std::make_pair(DFSVisitor::FINISH_EDGE,
                       [&](auto e) -> auto { return this->visitor_finish_edge(e); })));
  }

  auto operator[](GlobalOpIndex id) const noexcept -> DelayInterval {
    return {crit_cost_[id], mu_[id]};
  }

  [[nodiscard]] auto dump_to_json(const NetworkTopology *topology,
                                  nlohmann::ordered_json &&j) const noexcept
      -> nlohmann::ordered_json;

private:
  Delay hyper_cycle_;
  CriticalPath critical_path_;
  const ProcessingOrder *processing_order_;
  std::vector<Delay> crit_cost_;
  std::vector<Delay> mu_;
  std::map<Link, TokenBucket> token_bucket_;
  std::map<std::pair<Link, Link>, TokenBucket> token_bucket_diff_;

  void prolongation(const Vertex &v) noexcept;
  void update_prolongation(const Edge &e) noexcept;
  void deferment(const Edge &e) noexcept;
  void fault_isolation(const Edge &e) noexcept;
  void sequential_transmission(const Edge &e) noexcept;

  [[nodiscard]] auto check_stable_qos(const Vertex &v) noexcept -> TraversalStatus;

  constexpr auto visitor_discover_vertex(auto visitor) noexcept -> TraversalStatus {
    const auto &v = *std::get<Vertex *>(visitor);
    crit_cost_[v.id] = 0;
    mu_[v.id] = 0;
    return CONTINUE;
  }

  constexpr auto visitor_finish_vertex(auto visitor) noexcept -> TraversalStatus {
    const auto &v = *std::get<Vertex *>(visitor);
    if (v.id > SINK_ID) {
      prolongation(v);
      add_mk_firm_psfp(v);
    }
    if (std::ranges::find(v.route_succ, &processing_order_->sink()) != v.route_succ.end()) {
      return check_stable_qos(v);
    }
    return CONTINUE;
  }

  constexpr auto visitor_finish_edge(auto visitor) noexcept -> TraversalStatus {
    auto e = std::get<Edge>(visitor);

    switch (e.edge_type) {
    case MACHINE:
      if (e.source->pcp <= e.target->pcp) {
        deferment(e);
      } else {
        update_prolongation(e);
      }
      break;
    case FIFO:
      fault_isolation(e);
      break;
    case JOB:
      if (e.source->id != SOURCE_ID && e.target->id != SINK_ID) {
        sequential_transmission(e);
      }
      break;
    default:
      std::unreachable();
    }
    return CONTINUE;
  }

  void add_mk_firm_psfp(Link link) noexcept;
  void add_mk_firm_psfp(const Vertex &v) noexcept;

  [[nodiscard]] auto mk_firm_hypercycle_at(Link link) const noexcept -> Delay;
  [[nodiscard]] auto mk_firm_streams_at(Link link) const noexcept -> Generator<const Stream *>;
  [[nodiscard]] auto mk_firm_stream_diff_at(Link link1, Link link2) const noexcept
      -> Generator<const Stream *>;

  [[nodiscard]] auto token_bucket(const Vertex &v) noexcept -> const TokenBucket &;
  [[nodiscard]] auto token_bucket_diff(const Edge &e) noexcept -> const TokenBucket &;

  [[nodiscard]] static auto compute_token_bucket(const DataLinkProperty &link,
                                                 Generator<const Stream *> &&streams) noexcept
      -> TokenBucket;
  [[nodiscard]] static auto
  compute_bucket_size(const ElevationStepFunctions &stream_elevation) noexcept -> Bytes;
  [[nodiscard]] static auto compute_token_rate(const ElevationStepFunctions &stream_elevation,
                                               Bytes bucket_size) noexcept -> DataRate;
};

} // namespace tsndgm
