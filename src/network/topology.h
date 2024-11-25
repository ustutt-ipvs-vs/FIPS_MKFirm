#ifndef TSN_DGM_TOPOLOGY_H
#define TSN_DGM_TOPOLOGY_H

#include "../utils/interval.h"
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace tsndgm {

using DeviceId = unsigned int;
using Tick = unsigned long long; // in nanoseconds
using Delay = Tick;
using DataRate = unsigned long long; // in bps

[[maybe_unused]] constexpr Tick TicksPerMicroSec = 1e3;
[[maybe_unused]] constexpr Tick TicksPerMilliSec = 1e6;
[[maybe_unused]] constexpr Tick TicksPerSec = 1e9;

[[maybe_unused]] constexpr DataRate Ethernet10Mbps = 10e6;
[[maybe_unused]] constexpr DataRate Ethernet100Mbps = 100e6;
[[maybe_unused]] constexpr DataRate Ethernet1Gbps = 1e9;

struct DelayInterval : public Interval<Delay> {
  using Interval<Delay>::Interval;
  explicit DelayInterval(nlohmann::json json);
  auto operator=(const Interval<Delay> &interval) -> DelayInterval & {
    min = interval.min;
    max = interval.max;
    return *this;
  };
};

struct Link {
  DeviceId source;
  DeviceId target;
};

enum DataLinkType : std::uint8_t { WIRED, WIRELESS };
enum DataLinkDirection : std::uint8_t { UNIDIRECTIONAL, BIDIRECTIONAL };
struct DataLinkProperty : public Link {
  DataLinkType type;
  DataRate data_rate;
  Delay propagation_delay;
};

enum DeviceType : std::uint8_t {
  END_DEVICE,
  TSN_BRIDGE,
  TSN_TRANSLATOR,
  UNSPECIFIED
};
struct DeviceProperty {
  DeviceId id;
  DeviceType type;
  DelayInterval processing_delay;
  std::string name;
  std::vector<DataLinkProperty> out;
};
static DeviceProperty SOURCE(0, UNSPECIFIED, DelayInterval(0), "SOURCE");

struct NetworkTopology {
  NetworkTopology() = default;
  NetworkTopology(nlohmann::json &&j);
  NetworkTopology(const std::filesystem::path &in);

  void add_device(const DeviceProperty &device);
  void add_data_link(const DataLinkProperty &data_link,
                     DataLinkDirection direction = BIDIRECTIONAL);

  [[nodiscard]] auto get_device(DeviceId id) const -> const DeviceProperty *;
  [[nodiscard]] auto get_data_link(Link link) const -> const DataLinkProperty *;

  auto operator[](DeviceId id) const -> const DeviceProperty &;
  auto operator[](Link link) const -> const DataLinkProperty &;

  [[nodiscard]] auto dump_to_json() const -> nlohmann::json;
  void dump_to_file(const std::filesystem::path &out) const;

  void print() const;

private:
  std::vector<DeviceProperty> devices_;

  [[nodiscard]] auto get_device_index(DeviceId id) const;
};

using Path = std::vector<DeviceId>;
struct RouteHop {
  const DeviceProperty &device;
  std::vector<RouteHop *> parents;
  std::vector<RouteHop *> childs;

  explicit RouteHop(const DeviceProperty &device) : device(device) {};

  [[nodiscard]] auto is_virtual_source() const -> bool {
    return parents.empty();
  }
  [[nodiscard]] auto is_talker() const -> bool {
    return parents.size() == 1 && parents.front()->is_virtual_source();
  }
  [[nodiscard]] auto is_listener() const -> bool { return childs.empty(); }
  [[nodiscard]] auto is_replication_point() const -> bool {
    return childs.size() > 1;
  }
  [[nodiscard]] auto is_elimination_point() const -> bool {
    return parents.size() > 1;
  }

  [[nodiscard]] auto
  find_parent(DeviceId id) const -> std::vector<RouteHop *>::const_iterator {
    return find(id, parents);
  }
  [[nodiscard]] auto has_parent(DeviceId id) const -> bool {
    return find_parent(id) != parents.end();
  }
  [[nodiscard]] auto
  find_child(DeviceId id) const -> std::vector<RouteHop *>::const_iterator {
    return find(id, childs);
  }
  [[nodiscard]] auto has_child(DeviceId id) const -> bool {
    return find_child(id) != childs.end();
  }

private:
  [[nodiscard]] static auto find(DeviceId id,
                                 const std::vector<RouteHop *> &hops)
      -> std::vector<RouteHop *>::const_iterator {
    return std::ranges::find_if(hops,
                                [&](auto hop) { return hop->device.id == id; });
  }
};

struct Route {
private:
  std::map<DeviceId, RouteHop> hops_;
  std::vector<RouteHop *> listeners_;

  void recompute_listeners();

public:
  RouteHop source;

  Route() : source({SOURCE}) {};
  Route(nlohmann::json &&j, const NetworkTopology &network);

  void add_path(const Path &path, const NetworkTopology &network,
                bool recompute_listeners = true);
  void add_link(Link link, const NetworkTopology &network,
                bool recompute_listeners = true);

  [[nodiscard]] auto dump_to_json() const -> nlohmann::json;
  void print(std::ostream &out = std::cout) const;

  [[nodiscard]] auto listeners() const -> const std::vector<RouteHop *> & {
    return listeners_;
  };
  [[nodiscard]] auto talkers() const -> const std::vector<RouteHop *> & {
    return source.childs;
  };

  auto operator[](DeviceId id) -> const RouteHop & { return hops_.at(id); }
  using Iterator = decltype(hops_)::const_iterator;
  [[nodiscard]] auto begin() const -> Iterator { return hops_.begin(); }
  [[nodiscard]] auto end() const -> Iterator { return hops_.end(); }
};

} // namespace tsndgm

#endif // TSN_DGM_TOPOLOGY_H
