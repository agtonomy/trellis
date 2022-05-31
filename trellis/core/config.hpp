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
  Config(const std::string& file);

  /*
   * Construct a config object based on the given YAML node
   * @param root the YAML node that represents the root of the configuration
   */
  Config(const YAML::Node& root);

  /**
   * Retrieve a child node via the given key
   * @param key a string representing the key name
   */
  YAML::Node operator[](const std::string& key);

  /**
   * Retrieve a child node via the given key (const)
   * @param key a string representing the key name
   */
  const YAML::Node operator[](const std::string& key) const;

 private:
  YAML::Node root_;
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_CONFIG_HPP
