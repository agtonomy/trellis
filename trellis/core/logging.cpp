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

#include "trellis/core/logging.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <unordered_map>

namespace trellis {
namespace core {
namespace Log {

namespace {
static constexpr LogLevel kDefaultLogLevel{kFatal};
std::atomic<LogLevel> g_log_level{kDefaultLogLevel};
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

void SetLogLevel(LogLevel log_level) { g_log_level.store(log_level, std::memory_order_relaxed); }

bool detail::IsEnabled(LogLevel level) { return level <= g_log_level.load(std::memory_order_relaxed); }

void detail::Log(LogLevel level, fmt::string_view fmt_msg, fmt::format_args args) {
  static constexpr std::array<std::string_view, 5> kPrefix{"[INFO]  ", "[WARN]  ", "[ERROR] ", "[FATAL] ", "[DEBUG] "};
  // Build the whole line in one buffer to pass to a single fwrite, which stdio serializes across threads. The fmt
  // buffer lives on the stack and only heap-allocates for lines over 1 KiB, vs gcc std::string's 15 chars
  fmt::basic_memory_buffer<char, 1024> line;
  line.append(kPrefix[level]);
  fmt::vformat_to(std::back_inserter(line), fmt_msg, args);
  line.push_back('\n');
  std::fwrite(line.data(), 1, line.size(), stdout);
  // stdout is fully buffered when it is a pipe (journald), and abort() in Fatal does not flush stdio.
  std::fflush(stdout);
}

}  // namespace Log
}  // namespace core
}  // namespace trellis
