/*
 * Copyright (C) 2022 Agtonomy
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

#include "logging.hpp"

#include <iostream>
#include <mutex>
#include <unordered_map>

namespace trellis {
namespace core {
namespace Log {

namespace {
static constexpr LogLevel kDefaultLogLevel{kFatal};
LogLevel g_log_level{kDefaultLogLevel};
std::mutex g_log_mutex{};
}  // namespace

void SetLogLevel(const std::string& log_level_string) {
  static const std::unordered_map<std::string, LogLevel> map{{"info", kInfo},   {"warn", kWarn},   {"warning", kWarn},
                                                             {"error", kError}, {"fatal", kFatal}, {"debug", kDebug}};

  std::string key = log_level_string;
  std::transform(key.begin(), key.end(), key.begin(), ::tolower);

  auto it = map.find(key);
  const LogLevel log_level = (it != map.end()) ? it->second : kDefaultLogLevel;
  SetLogLevel(log_level);
}

void SetLogLevel(LogLevel log_level) { g_log_level = log_level; }

void DoLog(const std::string& msg, const std::string& prefix, LogLevel level) {
  std::lock_guard lock(g_log_mutex);
  if (level <= g_log_level) {
    std::cout << prefix << msg << std::endl;
  }
}

}  // namespace Log
}  // namespace core
}  // namespace trellis
