#pragma once

#include "critical_path.h"
#include "network/histogram.h"
#include "transmission_operations.h"
#include "traversal.h"

namespace tsndgm {

struct TokenBucket {
  Bytes bucket_size{0};
  DataRate token_rate{0};
  DataRate link_data_rate{0};
};

struct TokenStepFunction {
  std::map<Delay, Bytes> func{{0, 0}};
  Delay hyper_cycle{1};
  DataRate link_data_rate{0};

  void extend(Delay period);
};

struct MKFirmPSFPGate {
  std::set<Frame> frames;
  Delay open;
  Delay close;
};

using MKFirmPSFPConfiguration = std::map<DeviceId, std::deque<MKFirmPSFPGate>>;

struct MKFirmConfiguration {
  MKFirmPSFPConfiguration mkfirm_psfp_config;

  MKFirmConfiguration() = default;
  MKFirmConfiguration(DFSTraversal &dfs, const ProcessingOrder &processing_order,
                      Delay hyper_cycle) noexcept;

  constexpr auto traversal_events() {
    return critical_path_.traversal_events().add(
        std::make_tuple(std::make_pair(DFSVisitor::DISCOVER_VERTEX,
                                       [&](auto v) { return this->visitor_discover_vertex(v); }),
                        std::make_pair(DFSVisitor::FINISH_VERTEX,
                                       [&](auto v) { return this->visitor_finish_vertex(v); }),
                        std::make_pair(DFSVisitor::FINISH_EDGE,
                                       [&](auto e) { return this->visitor_finish_edge(e); })));
  }

  auto operator[](GlobalOpIndex id) const noexcept -> DelayInterval {
    return {crit_cost_[id], mu_[id]};
  }

  [[nodiscard]] auto dump_to_json(const NetworkTopology *topology,
                                  nlohmann::json &&j) const noexcept -> nlohmann::json;

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

  [[nodiscard]] auto check_stable_qos(const Vertex &v) const noexcept -> TraversalStatus;

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

  [[nodiscard]] auto mk_firm_streams_at(Link link) const noexcept -> Generator<const Stream *>;
  [[nodiscard]] auto mk_firm_stream_diff_at(Link link1, Link link2) const noexcept
      -> Generator<const Stream *>;

  [[nodiscard]] auto token_bucket(const Vertex &v) noexcept -> const TokenBucket &;
  [[nodiscard]] auto token_bucket_diff(const Edge &e) noexcept -> const TokenBucket &;

  [[nodiscard]] static auto compute_token_bucket(const DataLinkProperty &link,
                                                 Generator<const Stream *> &&streams) noexcept
      -> TokenBucket;
  [[nodiscard]] static auto
  compute_token_step_function(const DataLinkProperty &link,
                              Generator<const Stream *> &&streams) noexcept -> TokenStepFunction;
  [[nodiscard]] static auto compute_bucket_size(const std::map<Delay, Bytes> &func) noexcept
      -> Bytes;
  [[nodiscard]] static auto compute_token_rate(const std::map<Delay, Bytes> &func,
                                               Bytes bucket_size) noexcept -> DataRate;
};

} // namespace tsndgm
