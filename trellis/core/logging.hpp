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

#ifndef TRELLIS_CORE_LOGGING_HPP
#define TRELLIS_CORE_LOGGING_HPP

#include <fmt/core.h>
#include <unistd.h>

#include <string>

namespace trellis {
namespace core {
namespace Log {

enum LogLevel { kInfo = 0, kWarn, kError, kFatal, kDebug };

void DoLog(const std::string& msg, const std::string& prefix, LogLevel level);

template <typename... Args>
inline void Info(const std::string& fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[INFO]  ", LogLevel::kInfo);
}

template <typename... Args>
inline void Warn(const std::string& fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[WARN]  ", LogLevel::kWarn);
}

template <typename... Args>
inline void Error(const std::string& fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[ERROR] ", LogLevel::kError);
}

template <typename... Args>
inline void Fatal(const std::string& fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[FATAL] ", LogLevel::kFatal);
  // Sleep briefly because DoLog is not necessarily synchronous, so without sleeping this could abort before the fatal
  // log can be fully dispatched.
  sleep(1);
  abort();
}

template <typename... Args>
inline void Debug(const std::string& fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[DEBUG] ", LogLevel::kDebug);
}

}  // namespace Log
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_LOGGING_HPP
