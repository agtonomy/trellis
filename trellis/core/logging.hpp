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

#include <ecal/ecal.h>
#include <fmt/core.h>

namespace trellis {
namespace core {
namespace Log {

namespace {

void DoLog(const std::string& msg, const std::string& prefix, eCAL_Logging_eLogLevel level) {
  if (!eCAL::IsInitialized(eCAL::Init::Logging)) {
    eCAL::Initialize(0, nullptr, nullptr, eCAL::Init::Logging);
  }
  const std::string full_msg = prefix + msg;
  eCAL::Logging::Log(level, full_msg);
}

}  // namespace

template <typename... Args>
inline void Info(const std::string& fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[INFO] ", log_level_info);
}

template <typename... Args>
inline void Warn(const std::string& fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[WARN] ", log_level_warning);
}

template <typename... Args>
inline void Error(const std::string& fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[ERROR] ", log_level_error);
}

template <typename... Args>
inline void Fatal(const std::string& fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[FATAL] ", log_level_fatal);
}

template <typename... Args>
inline void Debug(const std::string& fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[DEBUG] ", log_level_debug1);
}

}  // namespace Log
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_LOGGING_HPP
