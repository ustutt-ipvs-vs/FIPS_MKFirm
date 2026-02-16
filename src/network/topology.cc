#include "topology.h"
#include "nlohmann/json_fwd.hpp"
#include "utils/generator.h"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <print>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tsndgm {

DelayInterval::DelayInterval(nlohmann::json json) {
  if (json.is_array()) {
    *this = DelayInterval(json[0], json[1]);
  } else {
    *this = DelayInterval(json.template get<Delay>());
  }
}

auto DeviceProperty::operator[](DeviceId id) const -> const DataLinkProperty & {
  auto data_link_it =
      std::ranges::find_if(out, [&](auto &data_link) -> auto { return data_link.target == id; });
  return *data_link_it;
}

NetworkTopology::NetworkTopology(nlohmann::json &&json) {
  for (auto &json_device : json["nodes"]) {
    const DeviceProperty device = {
        .id = json_device["id"],
        .type = json_device.contains("type") ? DeviceType(json_device["type"]) : UNSPECIFIED,
        .processing_delay = DelayInterval(json_device["processing_delay"]),
        .clock_resolution =
            json_get_or_default<>(json_device["clock_resolution"], static_cast<Delay>(1)),
        .name = json_device.contains("name") ? json_device["name"] : "",
    };
    add_device(device);
  }

  for (auto &json_link : json["links"]) {
    // C++23 does not yet allow designated and unnamed initializer clauses
    const DataLinkProperty data_link = {{
                                            .source = json_link["source"],
                                            .target = json_link["target"],
                                        },
                                        DataLinkType(json_link["type"]),
                                        json_link["data_rate"],
                                        json_link["propagation_delay"]};
    add_data_link(data_link);
  }
}

NetworkTopology::NetworkTopology(const std::filesystem::path &in)
    : NetworkTopology(nlohmann::json::parse(std::ifstream(in))) {}

auto NetworkTopology::get_device(DeviceId device_id) const -> const DeviceProperty * {
  auto device_it =
      std::ranges::find_if(devices_, [&](auto &device) -> auto { return device.id == device_id; });
  return device_it != devices_.end() ? &(*device_it) : nullptr;
}

auto NetworkTopology::get_device_index(DeviceId device_id) const {
  return std::ranges::find_if(devices_,
                              [&](auto &device) -> auto { return device.id == device_id; }) -
         devices_.begin();
}

auto NetworkTopology::get_data_link(Link link) const -> const DataLinkProperty * {
  auto source_index = get_device_index(link.source);
  if (std::cmp_greater_equal(source_index, devices_.size())) {
    throw std::invalid_argument(std::format("DeviceID does not exist: {}", link.source));
  }
  auto data_link_it =
      std::ranges::find_if(devices_[source_index].out, [&](auto &data_link) -> auto {
        return data_link.target == link.target;
      });
  return data_link_it != devices_[source_index].out.end() ? &(*data_link_it) : nullptr;
}

auto NetworkTopology::operator[](DeviceId device_id) const -> const DeviceProperty & {
  return *get_device(device_id);
}

auto NetworkTopology::at(DeviceId device_id) const -> const DeviceProperty & {
  const auto *device_ptr = get_device(device_id);
  if (device_ptr == nullptr) {
    throw std::out_of_range(std::format("DeviceID does not exist: {}", device_id));
  }
  return *device_ptr;
}

auto NetworkTopology::operator[](Link link) const -> const DataLinkProperty & {
  return *get_data_link(link);
}

auto NetworkTopology::at(Link link) const -> const DataLinkProperty & {
  const auto *link_ptr = get_data_link(link);
  if (link_ptr == nullptr) {
    throw std::out_of_range(std::format("Link does not exist: ({}, {})", link.source, link.target));
  }
  return *link_ptr;
}

auto NetworkTopology::devices() const -> Generator<const DeviceProperty *> {
  for (const auto &device : devices_) {
    co_yield &device;
  }
}

void NetworkTopology::add_device(const DeviceProperty &device) {
  if (get_device(device.id) != nullptr) {
    throw std::invalid_argument(std::format("DeviceID is not unique: {}", device.id));
  }

  devices_.push_back(device);
}

void NetworkTopology::add_data_link(const DataLinkProperty &data_link,
                                    DataLinkDirection direction) {
  if (direction == BIDIRECTIONAL) {
    add_data_link(data_link, UNIDIRECTIONAL);
    DataLinkProperty rev_data_link = data_link;
    std::swap(rev_data_link.source, rev_data_link.target);
    add_data_link(rev_data_link, UNIDIRECTIONAL);
  } else if (get_data_link(data_link) == nullptr) {
    DeviceProperty &source = devices_[get_device_index(data_link.source)];
    source.out.push_back(data_link);
  }
}

auto NetworkTopology::dump_to_json() const -> nlohmann::json {
  nlohmann::json json = {{"nodes", nlohmann::json::array()}, {"links", nlohmann::json::array()}};

  for (const auto &device : devices_) {
    json["nodes"].push_back(
        {{"id", device.id},
         {"type", device.type},
         {"processing_delay", {device.processing_delay.min, device.processing_delay.max}},
         {"name", device.name}});
    for (const auto &data_link : device.out) {
      json["links"].push_back({{"source", data_link.source},
                               {"target", data_link.target},
                               {"type", data_link.type},
                               {"data_rate", data_link.data_rate},
                               {"propagation_delay", data_link.propagation_delay}});
    }
  }

  return json;
}

void NetworkTopology::dump_to_file(const std::filesystem::path &out) const {
  auto json = dump_to_json();
  std::ofstream ofstream(out);
  ofstream << std::setw(4) << json;
}

void NetworkTopology::print() const {
  auto json = dump_to_json();
  std::cout << std::setw(4) << json;
}

auto NetworkTopology::link_to_string(Link link) const -> std::string {
  return std::format("[{},{}]", at(link.source).name, at(link.target).name);
}

void Route::recompute_listeners() {
  listeners_.clear();
  for (auto &[id, hop] : hops_) {
    if (hop.is_listener()) {
      listeners_.push_back(&hop);
    }
  }
}

void RouteHop::print(std::ostream &out, std::string indent, DeviceId parent,
                     bool is_last_child) const {
  if (is_talker() || find_parent(parent) == parents.cbegin()) {
    std::println(out, "{}{}{}", indent, is_last_child ? "└──" : "├──", device->id);
    for (auto *child : childs) {
      child->print(out, std::format("{}{}", indent, is_last_child ? "   " : "│  "), device->id,
                   child == childs.back());
    }
  } else {
    std::println(out, "{}{}{} (elimination)", indent, is_last_child ? "└──" : "├──", device->id);
  }
}

void Route::add_path(const Path &path, const NetworkTopology &network, bool b_recompute_listeners) {
  RouteHop *parent = &source;
  for (DeviceId const id : path) {
    if (parent->device != nullptr && parent->device->id == id) {
      throw std::invalid_argument("Path is invalid, containing the same hop twice");
    }

    if (hops_.contains(id)) {
      RouteHop &hop = hops_.at(id);
      if (parent != &source && hop.is_talker()) {
        source.childs.erase(source.find_child(id));
        hop.parents.clear();
      }
      if (!parent->has_child(id) && id != path.front()) {
        parent->childs.push_back(&hop);
        hop.parents.push_back(parent);
      }
      parent = &hop;
    } else {
      auto it = hops_.insert({id, RouteHop(&network.at(id))});
      RouteHop &hop = it.first->second;
      hop.parents.push_back(parent);
      parent->childs.push_back(&hop);
      parent = &hop;
    }
  }
  if (b_recompute_listeners) {
    recompute_listeners();
  }
}

void Route::add_link(Link link, const NetworkTopology &network, bool b_recompute_listeners) {
  add_path({link.source, link.target}, network, b_recompute_listeners);
}

auto Route::get_or_create(const DeviceProperty &device) -> RouteHop & {
  return hops_.contains(device.id) ? hops_.at(device.id)
                                   : hops_.insert({device.id, RouteHop(&device)}).first->second;
}

void Route::add_link(const DeviceProperty &source, const DeviceProperty &target) {
  RouteHop &source_hop = get_or_create(source);
  RouteHop &target_hop = get_or_create(target);
  if (!source_hop.has_child(target.id)) {
    source_hop.childs.push_back(&target_hop);
    target_hop.parents.push_back(&source_hop);
  }
}

Route::Route(nlohmann::json &&json, const NetworkTopology &network) {
  for (auto &json_link : json) {
    add_link(Link(json_link[0], json_link[1]), network, false);
  }
  recompute_listeners();
}

Route::Route(const Path &path, const NetworkTopology &network) { add_path(path, network); }

void Route::copy_links(const Route &other) {
  hops_.clear();
  for (auto [hop1, hop2] : other.traverse_hops()) {
    add_link(*(hop1->device), *(hop2->device));
  }
  recompute_listeners();
}

void Route::relink_source(const std::vector<RouteHop *> &talkers) {
  source = RouteHop();
  for (auto *talker_other : talkers) {
    RouteHop &talker = get_or_create(*talker_other->device);
    talker.parents.clear();
    if (!source.has_child(talker.device->id)) {
      source.childs.push_back(&talker);
      talker.parents.push_back(&source);
    }
  }
}

Route::Route(const Route &other) : hops_(other.hops_), listeners_(other.listeners_) {
  copy_links(other);
  relink_source(other.talkers());
}

Route::Route(Route &&other) noexcept
    : hops_(std::move(other.hops_)), listeners_(std::move(other.listeners_)) {
  relink_source(other.source.childs);
}

auto Route::operator=(const Route &other) -> Route & {
  if (this == &other) {
    return *this;
  }
  copy_links(other);
  relink_source(other.talkers());
  return *this;
}

auto Route::operator=(Route &&other) noexcept -> Route & {
  if (this == &other) {
    return *this;
  }
  hops_ = std::move(other.hops_);
  listeners_ = std::move(other.listeners_);
  relink_source(other.source.childs);
  return *this;
}

auto Route::dump_to_json() const -> nlohmann::json {
  nlohmann::json json = nlohmann::json::array();
  for (const auto &[id, hop] : hops_) {
    for (auto *child : hop.childs) {
      json.push_back({id, child->device->id});
    }
  }
  return json;
}

void Route::print(std::ostream &out) const {
  for (const auto &[id, hop] : hops_) {
    out << std::format("{} ({}):", hop.device->name, id);
    for (auto *child : hop.childs) {
      out << std::format("{} ({}); ", child->device->name, id);
    }
    out << "\n";
  }
}

void Route::print_tree(std::ostream &out) const {
  for (auto *talker : source.childs) {
    talker->print(out, "", talker->device->id);
  }
}

auto Route::traverse_links() const
    -> Generator<std::pair<const DeviceProperty *, const DeviceProperty *>> {
  for (const auto &[device_id, hop] : hops_) {
    for (auto *child_ptr : hop.childs) {
      std::pair<const DeviceProperty *, const DeviceProperty *> device_pair = {hop.device,
                                                                               child_ptr->device};
      co_yield device_pair;
    }
  }
}

auto Route::traverse_talker_links() const -> Generator<Link> {
  for (auto *talker : talkers()) {
    for (auto *next_hop : talker->childs) {
      Link link(talker->device->id, next_hop->device->id);
      co_yield link;
    }
  }
}

auto Route::traverse_listener_links() const -> Generator<Link> {
  for (auto *listener : listeners()) {
    for (auto *prev_hop : listener->parents) {
      Link link(prev_hop->device->id, listener->device->id);
      co_yield link;
    }
  }
}

auto Route::traverse_consecutive_links() const -> Generator<std::pair<Link, Link>> {
  for (auto [hop1, hop2] : traverse_hops()) {
    Link const link12(hop1->device->id, hop2->device->id);
    for (auto *hop3 : hop2->childs) {
      // there are certain FRER replication pattern that can appear
      // like a cycle, while there is no real cyclic dependency.
      // More complicated replication patterns, however, can be rejected.
      if (hop3->device->id == hop1->device->id) {
        continue;
      }
      Link const link23(hop2->device->id, hop3->device->id);
      auto link_pair = std::make_pair(link12, link23);
      co_yield link_pair;
    }
  }
}

auto Route::traverse_hops() const -> Generator<RouteHopLink> {
  for (const auto &[device_id, hop] : hops_) {
    for (auto *child_ptr : hop.childs) {
      std::pair<const RouteHop *, const RouteHop *> hop_pair = {&hop, child_ptr};
      co_yield hop_pair;
    }
  }
}

auto Route::traverse_wireless_hops() const -> Generator<RouteHopLink> {
  for (auto &hop : traverse_hops()) {
    const DeviceProperty &source = *hop.first->device;
    if (source[hop.second->device->id].type == WIRELESS) {
      co_yield hop;
    }
  }
}

auto Route::number_of_links() const -> size_t {
  size_t links = 0;
  for (auto _ : traverse_links()) {
    links++;
  }
  return links;
}

auto Route::has_wireless_links() const -> bool {
  for (auto _ : traverse_wireless_hops()) {
    return true;
  }
  return false;
}

} // namespace tsndgm
