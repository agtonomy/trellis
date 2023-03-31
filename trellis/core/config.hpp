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

#ifndef TRELLIS_CORE_CONFIG_HPP
#define TRELLIS_CORE_CONFIG_HPP

#include "yaml-cpp/yaml.h"

namespace trellis {
namespace core {

/**
 * Config class to represent the configuration structure used by applications
 */
class Config {
 public:
  /**
   * Default construct an empty config object
   */
  Config() = default;

  /*
   * Construct a config object from the contents of a YAML file
   *
   * @param file the file path to read from
   * @throws YAML::BadFile on error
   */
  explicit Config(const std::string& file);

  /*
   * Construct a config object based on the given YAML node
   * @param root the YAML node that represents the root of the configuration
   */
  explicit Config(const YAML::Node& root);

  /**
   * Retrieve a child node via the given key
   * @param key a string representing the key name
   * @return the YAML node at the key
   */
  YAML::Node operator[](const std::string& key);

  /**
   * Retrieve a child node via the given key (const)
   * @param key a string representing the key name
   * @return the YAML node at the key
   */
  const YAML::Node operator[](const std::string& key) const;

  /**
   * Root get the root YAML node
   * @return the YAML node pointing to the root of the structure
   */
  const YAML::Node& Root() const { return root_; }

  /**
   * Overlay overlay the given Node on top of the existing configuration
   *
   * For any keys that overlap, they will be overwritten.
   *
   * @param overlay root of YAML node to overlay
   * @param verbose log output
   */
  void Overlay(const YAML::Node& overlay, bool verbose = false);

  /**
   * Overlay overlay the given config tree as a YAML string on top of the existing configuration
   *
   * @param raw_YAML a string containing YAML
   * @param verbose log output
   * @see Overlay(const YAML::Node&)
   */
  void Overlay(const std::string raw_yaml, bool verbose = false);

  /**
   * Overlay overlay the config tree from the given file
   *
   * @param filename the file name containing the configuration
   * @param verbose log output
   * @see Overlay(const YAML::Node&)
   */
  void OverlayFromFile(const std::string filename, bool verbose = false);

  /**
   * @brief Writes the configuration (including overlay) to the given file.
   *
   * @param filename the file name to write to
   * @param verbose log output
   */
  void WriteToFile(std::string_view filename, bool verbose = false) const;

 private:
  static void RecursiveOverlay(YAML::Node base, YAML::Node overlay, bool verbose, const std::string& key_prefix = "");
  YAML::Node root_;
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_CONFIG_HPP
