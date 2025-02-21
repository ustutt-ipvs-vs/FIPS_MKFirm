#pragma once

#include "nlohmann/json.hpp"
#include "utils/generator.h"
#include "utils/interval.h"
#include <cstddef>
#include <filesystem>
#include <generator>
#include <iostream>
#include <string>
#include <vector>

template <typename T> auto json_get_or_default(const nlohmann::json &j, T default_value) -> T {
  return (j.is_null() ? default_value : j.template get<T>());
}

namespace tsndgm {

using DeviceId = unsigned int;
using Tick = long long; // in nanoseconds
using Delay = long long;
using DataRate = long long; // in bps

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

  auto operator<=>(const Link &other) const {
    return source == other.source ? target <=> other.target : source <=> other.source;
  }
};

enum DataLinkType : std::uint8_t { WIRED, WIRELESS };
enum DataLinkDirection : std::uint8_t { UNIDIRECTIONAL, BIDIRECTIONAL };
struct DataLinkProperty : public Link {
  DataLinkType type;
  DataRate data_rate{0};
  Delay propagation_delay{0};
};

enum DeviceType : std::uint8_t { END_DEVICE, TSN_BRIDGE, DS_TT, NW_TT, UNSPECIFIED };
struct DeviceProperty {
  DeviceId id;
  DeviceType type{UNSPECIFIED};
  DelayInterval processing_delay{0};
  std::string name;
  std::vector<DataLinkProperty> out;

  auto operator[](DeviceId id) const -> const DataLinkProperty &;
};

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
  [[nodiscard]] auto at(DeviceId id) const -> const DeviceProperty &;
  auto operator[](Link link) const -> const DataLinkProperty &;
  [[nodiscard]] auto at(Link link) const -> const DataLinkProperty &;

  [[nodiscard]] auto devices() const -> Generator<const DeviceProperty *>;

  [[nodiscard]] auto dump_to_json() const -> nlohmann::json;
  void dump_to_file(const std::filesystem::path &out) const;

  void print() const;
  [[nodiscard]] auto link_to_string(Link link) const -> std::string;

private:
  std::vector<DeviceProperty> devices_;

  [[nodiscard]] auto get_device_index(DeviceId id) const;
};

using Path = std::vector<DeviceId>;
struct RouteHop {
  const DeviceProperty *device;
  std::vector<RouteHop *> parents;
  std::vector<RouteHop *> childs;

  RouteHop() = default;
  explicit RouteHop(const DeviceProperty *device) : device(device) {};

  [[nodiscard]] auto is_virtual_source() const -> bool { return device == nullptr; }
  [[nodiscard]] auto is_talker() const -> bool {
    return parents.size() == 1 && parents.front()->is_virtual_source();
  }
  [[nodiscard]] auto is_listener() const -> bool { return childs.empty(); }
  [[nodiscard]] auto is_replication_point() const -> bool { return childs.size() > 1; }
  [[nodiscard]] auto is_elimination_point() const -> bool { return parents.size() > 1; }

  [[nodiscard]] auto find_parent(DeviceId id) const -> std::vector<RouteHop *>::const_iterator {
    return find(id, parents);
  }
  [[nodiscard]] auto has_parent(DeviceId id) const -> bool {
    return !parents.empty() && find_parent(id) != parents.end();
  }
  [[nodiscard]] auto find_child(DeviceId id) const -> std::vector<RouteHop *>::const_iterator {
    return find(id, childs);
  }
  [[nodiscard]] auto has_child(DeviceId id) const -> bool {
    return !childs.empty() && find_child(id) != childs.end();
  }
  void print(std::ostream &out, std::string indent, DeviceId parent,
             bool is_last_child = false) const;

private:
  [[nodiscard]] static auto find(DeviceId id, const std::vector<RouteHop *> &hops)
      -> std::vector<RouteHop *>::const_iterator {
    return std::ranges::find_if(hops, [&](auto &hop) {
      assert(hop->device != nullptr);
      return hop->device->id == id;
    });
  }
};

using RouteHopLink = std::pair<const RouteHop *, const RouteHop *>;

struct Route {
private:
  std::map<DeviceId, RouteHop> hops_;
  std::vector<RouteHop *> listeners_;

  void recompute_listeners();
  auto get_or_create(const DeviceProperty &device) -> RouteHop &;
  void add_link(const DeviceProperty &source, const DeviceProperty &target);
  void copy_links(const Route &other);
  void relink_source(const std::vector<RouteHop *> &talkers);

public:
  RouteHop source;

  Route() = default;
  Route(nlohmann::json &&j, const NetworkTopology &network);
  Route(const Path &path, const NetworkTopology &network);

  Route(const Route &other);
  auto operator=(const Route &other) -> Route &;
  Route(Route &&other) noexcept;
  auto operator=(Route &&other) noexcept -> Route &;
  ~Route() = default;

  void add_path(const Path &path, const NetworkTopology &network, bool recompute_listeners = true);
  void add_link(Link link, const NetworkTopology &network, bool recompute_listeners = true);

  [[nodiscard]] auto dump_to_json() const -> nlohmann::json;
  void print(std::ostream &out = std::cout) const;
  void print_tree(std::ostream &out = std::cout) const;

  [[nodiscard]] auto listeners() const -> const std::vector<RouteHop *> & { return listeners_; };
  [[nodiscard]] auto talkers() const -> const std::vector<RouteHop *> & { return source.childs; };

  auto operator[](DeviceId id) const -> const RouteHop & { return hops_.at(id); }
  using Iterator = decltype(hops_)::const_iterator;
  [[nodiscard]] auto begin() const -> Iterator { return hops_.begin(); }
  [[nodiscard]] auto end() const -> Iterator { return hops_.end(); }

  [[nodiscard]] auto traverse_links() const
      -> Generator<std::pair<const DeviceProperty *, const DeviceProperty *>>;
  [[nodiscard]] auto traverse_talker_links() const -> Generator<Link>;
  [[nodiscard]] auto traverse_listener_links() const -> Generator<Link>;
  [[nodiscard]] auto traverse_consecutive_links() const -> Generator<std::pair<Link, Link>>;
  [[nodiscard]] auto traverse_hops() const -> Generator<RouteHopLink>;
  [[nodiscard]] auto traverse_wireless_hops() const -> Generator<RouteHopLink>;

  [[nodiscard]] auto number_of_links() const -> size_t;
  [[nodiscard]] auto has_wireless_links() const -> bool;
};

} // namespace tsndgm
