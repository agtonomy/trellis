/*
 * Copyright (C) 2021 Agtonomy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "config.hpp"

#include <fstream>
#include <iostream>

#include "logging.hpp"

namespace trellis {
namespace core {

Config::Config(const std::string& file) : Config(YAML::LoadFile(file)) {}

Config::Config(const YAML::Node& root) : root_{root} {
  if (!root_.IsMap()) {
    throw std::invalid_argument("YAML root node must be a map");
  }
}

YAML::Node Config::operator[](const std::string& key) { return root_[key]; }

const YAML::Node Config::operator[](const std::string& key) const { return root_[key]; }

void Config::Overlay(const YAML::Node& overlay, bool verbose) { RecursiveOverlay(root_, overlay, verbose); }

void Config::Overlay(const std::string raw_yaml, bool verbose) { Overlay(YAML::Load(raw_yaml), verbose); }

void Config::OverlayFromFile(const std::string filename, bool verbose) { Overlay(YAML::LoadFile(filename), verbose); }

void Config::RecursiveOverlay(YAML::Node base, YAML::Node overlay, bool verbose, std::string key_prefix) {
  if (!overlay.IsMap()) {
    throw std::invalid_argument("Overlay YAML root node must be a map");
  }
  if (overlay.size() == 0) {
    return;
  }

  for (const auto& it : overlay) {
    const std::string key = it.first.as<std::string>();
    const YAML::Node value = it.second;
    const bool exists_in_base = static_cast<bool>(base[key]);
    const bool value_is_map = value.IsMap();
    if (exists_in_base && value_is_map) {
      // If this particular key exists in the base config, and the value is a map itself, let's recurse
      if (key_prefix.empty()) {
        key_prefix = key;
      } else {
        key_prefix += "." + key;
      }
      RecursiveOverlay(base[key], value, verbose, key_prefix);
    } else {
      // If this key didn't exist in the base, our job is easy, let's just attach this branch from the overlay on to the
      // base configuration. Also, if the value is not a map (scalar or sequence), then we want to overwrite the value
      // in the base layer

      // Sanity check that we're overwriting the same type
      if (exists_in_base && (base[key].Type() != value.Type())) {
        // We don't want our overlay to actually change the type...
        throw std::invalid_argument("Overlay key " + key + " does not match the base key type!");
      }
      base[key] = value;
      if (verbose) {
        const std::string full_key = key_prefix + "." + key;
        std::stringstream value_str;
        value_str << value;
        Log::Info("Configuration overlay overwriting {} = {}", full_key, value_str.str());
      }
    }
  }
}

void Config::WriteToFile(const std::string_view filename, bool verbose) const {
  // Need to allocate a string because stl file maniuplations require null termination.
  auto fstream = std::ofstream{std::string{filename}};
  if (verbose) Log::Info("Writing configuration to {}...", filename);
  fstream << root_;
  if (verbose) Log::Info("Done writing configuration to {}.", filename);
}

}  // namespace core
}  // namespace trellis
