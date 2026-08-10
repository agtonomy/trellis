/*
 * Copyright (C) 2026 Agtonomy
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

#ifndef TRELLIS_CORE_HEALTH_STATUS_HPP_
#define TRELLIS_CORE_HEALTH_STATUS_HPP_

#include <cstdint>
#include <ostream>
#include <string>

namespace trellis::core::health {

/**
 * Application-defined numeric code accompanying a health update. Opaque to trellis.
 */
using Code = uint64_t;

/**
 * Mirror of the trellis::core::HealthState proto enum, for code that composes health state without depending on
 * protobuf. Kept in a separate namespace because the generated proto types already occupy trellis::core.
 */
enum class HealthState { kUnspecified = 0, kNormal, kDegraded, kRecoverable, kCritical, kLost };

/**
 * Mirror of the trellis::core::HealthStatus proto message, minus the timestamp, which is stamped by Health at the
 * moment of the update rather than supplied by the caller.
 */
struct HealthStatus {
  HealthState state = HealthState::kUnspecified;
  Code code = 0;
  std::string description = "";

 private:
  friend std::ostream& operator<<(std::ostream& os, const HealthStatus& hs) {
    os << "State: " + std::to_string(static_cast<unsigned int>(hs.state)) + "\n";
    os << "Code: " + std::to_string(hs.code) + "\n";
    os << "Description: " + hs.description;
    return os;
  }

  friend bool operator<=>(const HealthStatus& lhs, const HealthStatus& rhs) = default;
};

}  // namespace trellis::core::health

#endif  // TRELLIS_CORE_HEALTH_STATUS_HPP_
