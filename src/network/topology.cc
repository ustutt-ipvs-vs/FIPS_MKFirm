#include "topology.h"
#include "nlohmann/json_fwd.hpp"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace tsndgm {

DelayInterval::DelayInterval(nlohmann::json json) {
  if (json.is_array()) {
    *this = DelayInterval(json[0], json[1]);
  } else {
    *this = DelayInterval(json[0]);
  }
}

auto DeviceProperty::operator[](DeviceId id) const -> const DataLinkProperty & {
  auto data_link_it = std::ranges::find_if(
      out, [&](auto &data_link) { return data_link.target == id; });
  return *data_link_it;
}

NetworkTopology::NetworkTopology(nlohmann::json &&json) {
  for (auto &json_device : json["nodes"]) {
    const DeviceProperty device = {
        .id = json_device["id"],
        .type = json_device.contains("type") ? DeviceType(json_device["type"])
                                             : UNSPECIFIED,
        .processing_delay = DelayInterval(json_device["processing_delay"]),
        .name = json_device.contains("name") ? json_device["name"] : "",
    };
    add_device(device);
  }

  for (auto &json_link : json["links"]) {
    // C++23 does not yet allow designated and unnamed initializer clauses
    const DataLinkProperty data_link = {{
                                            json_link["source"],
                                            json_link["target"],
                                        },
                                        DataLinkType(json_link["type"]),
                                        json_link["data_rate"],
                                        json_link["propagation_delay"]};
    add_data_link(data_link);
  }
}

NetworkTopology::NetworkTopology(const std::filesystem::path &in)
    : NetworkTopology(nlohmann::json::parse(std::ifstream(in))) {}

auto NetworkTopology::get_device(DeviceId device_id) const
    -> const DeviceProperty * {
  auto device_it = std::ranges::find_if(
      devices_, [&](auto &device) { return device.id == device_id; });
  return device_it != devices_.end() ? &(*device_it) : nullptr;
}

auto NetworkTopology::get_device_index(DeviceId device_id) const {
  return std::ranges::find_if(
             devices_, [&](auto &device) { return device.id == device_id; }) -
         devices_.begin();
}

auto NetworkTopology::get_data_link(Link link) const
    -> const DataLinkProperty * {
  auto source_index = get_device_index(link.source);
  if (std::cmp_greater_equal(source_index, devices_.size())) {
    throw std::invalid_argument(
        std::format("DeviceID does not exist: %u", link.source));
  }
  auto data_link_it =
      std::ranges::find_if(devices_[source_index].out, [&](auto &data_link) {
        return data_link.target == link.target;
      });
  return data_link_it != devices_[source_index].out.end() ? &(*data_link_it)
                                                          : nullptr;
}

auto NetworkTopology::operator[](DeviceId device_id) const
    -> const DeviceProperty & {
  return *get_device(device_id);
}

auto NetworkTopology::operator[](Link link) const -> const DataLinkProperty & {
  return *get_data_link(link);
}

void NetworkTopology::add_device(const DeviceProperty &device) {
  if (get_device(device.id) != nullptr) {
    throw std::invalid_argument(
        std::format("DeviceID is not unique: %u", device.id));
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
  nlohmann::json json = {{"nodes", nlohmann::json::array()},
                         {"links", nlohmann::json::array()}};

  for (const auto &device : devices_) {
    json["nodes"].push_back(
        {{"id", device.id},
         {"type", device.type},
         {"processing_delay",
          {device.processing_delay.min, device.processing_delay.max}},
         {"name", device.name}});
    for (const auto &data_link : device.out) {
      json["links"].push_back(
          {{"source", data_link.source},
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

void Route::recompute_listeners() {
  listeners_.clear();
  for (auto &[id, hop] : hops_) {
    if (hop.is_listener()) {
      listeners_.push_back(&hop);
    }
  }
}

void Route::add_path(const Path &path, const NetworkTopology &network,
                     bool b_recompute_listeners) {
  RouteHop *parent = &source;
  for (DeviceId const id : path) {
    if (parent->device->id == id) {
      throw std::invalid_argument(
          "Path is invalid, containing the same hop twice");
    }

    if (hops_.contains(id)) {
      RouteHop &hop = hops_.at(id);
      if (!parent->has_child(id) && id != path.front()) {
        parent->childs.push_back(&hop);
        hop.parents.push_back(parent);
      }
      parent = &hop;
    } else {
      auto it = hops_.insert({id, RouteHop(&network[id])});
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

void Route::add_link(Link link, const NetworkTopology &network,
                     bool b_recompute_listeners) {
  add_path({link.source, link.target}, network, b_recompute_listeners);
}

auto Route::get_or_create(const DeviceProperty &device) -> RouteHop & {
  return hops_.contains(device.id)
             ? hops_.at(device.id)
             : hops_.insert({device.id, RouteHop(&device)}).first->second;
}

void Route::add_link(const DeviceProperty &source,
                     const DeviceProperty &target) {
  RouteHop &source_hop = get_or_create(source);
  RouteHop &target_hop = get_or_create(target);
  if (!source_hop.has_child(target.id)) {
    source_hop.childs.push_back(&target_hop);
    target_hop.parents.push_back(&source_hop);
  }
}

Route::Route(nlohmann::json &&json, const NetworkTopology &network)
    : source(&SOURCE) {
  for (auto &json_link : json) {
    add_link(Link(json_link[0], json_link[1]), network, false);
  }
  recompute_listeners();
}

Route::Route(const Route &other) : Route() {
  for (auto [hop1, hop2] : other.traverse_hops()) {
    add_link(*(hop1->device), *(hop2->device));
  }
  recompute_listeners();
}

auto Route::operator=(const Route &other) -> Route & {
  hops_.clear();
  source = RouteHop(&SOURCE);
  for (auto [hop1, hop2] : other.traverse_hops()) {
    add_link(*(hop1->device), *(hop2->device));
  }
  recompute_listeners();
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
    out << std::format("%s (%u):", hop.device->name, id);
    for (auto *child : hop.childs) {
      out << std::format("%s (%u); ", child->device->name, id);
    }
    out << "\n";
  }
}

auto Route::traverse_links() const
    -> Generator<std::pair<const DeviceProperty *, const DeviceProperty *>> {
  for (const auto &[device_id, hop] : hops_) {
    for (auto *child_ptr : hop.childs) {
      std::pair<const DeviceProperty *, const DeviceProperty *> device_pair = {
          hop.device, child_ptr->device};
      co_yield device_pair;
    }
  }
}

auto Route::traverse_hops() const
    -> Generator<std::pair<const RouteHop *, const RouteHop *>> {
  for (const auto &[device_id, hop] : hops_) {
    for (auto *child_ptr : hop.childs) {
      std::pair<const RouteHop *, const RouteHop *> hop_pair = {&hop,
                                                                child_ptr};
      co_yield hop_pair;
    }
  }
}

} // namespace tsndgm
