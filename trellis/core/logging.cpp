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

#include <ecal/ecal.h>

namespace trellis {
namespace core {
namespace Log {

namespace {

eCAL_Logging_eLogLevel LogLevelToEcalLevel(LogLevel level) {
  static const std::unordered_map<LogLevel, eCAL_Logging_eLogLevel> logmap {
    {kInfo, log_level_info}, {kWarn, log_level_warning}, {kError, log_level_error}, {kFatal, log_level_fatal},
        {kDebug, log_level_debug1},
  }
  return logmap[level];
}

}  // namespace

void DoLog(const std::string& msg, const std::string& prefix, LogLevel level) {
  if (!eCAL::IsInitialized(eCAL::Init::Logging)) {
    eCAL::Initialize(0, nullptr, nullptr, eCAL::Init::Logging);
  }
  const std::string full_msg = prefix + msg;
  eCAL::Logging::Log(LogLevelToEcalLevel(level), full_msg);
}

}  // namespace Log
}  // namespace core
}  // namespace trellis
