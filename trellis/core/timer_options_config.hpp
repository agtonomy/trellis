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

#ifndef TRELLIS_CORE_TIMER_OPTIONS_CONFIG_HPP_
#define TRELLIS_CORE_TIMER_OPTIONS_CONFIG_HPP_

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "trellis/core/config.hpp"
#include "trellis/core/timer_options.hpp"

namespace trellis::core {

/**
 * TimerOptionsFromConfig reads the timer policy from the `trellis.timers.*` keys
 *
 * Kept out of timer_options.hpp so that TimerOptions itself, and therefore EventLoop and everything
 * built against a loop, does not acquire a dependency on the configuration library.
 *
 * A key that is absent takes the default. A key that is present but not a policy name is a mistake in
 * the configuration rather than something to paper over, so it throws instead of quietly running with a
 * policy the author did not ask for. A Node resolves its options while constructing, so that mistake
 * surfaces as an exception at startup rather than as scheduling behavior nobody chose.
 *
 * @param config the config object to read
 *
 * @return the timer policy described by the configuration, defaults where a key is absent
 *
 * @throws std::invalid_argument if a key is present but holds an unrecognized value
 */
inline TimerOptions TimerOptionsFromConfig(const Config& config) {
  static const std::unordered_map<std::string, RearmPolicy> kRearmPolicies{{"catch_up", RearmPolicy::kCatchUp},
                                                                           {"skip_aligned", RearmPolicy::kSkipAligned}};

  TimerOptions options{};
  auto policy_name = config.AsIfExists<std::string>("trellis.timers.rearm_policy", "catch_up");
  // Through unsigned char: std::tolower is undefined for a negative argument, which a plain char is for any
  // byte above 0x7F on a platform where it is signed
  std::transform(policy_name.begin(), policy_name.end(), policy_name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const auto it = kRearmPolicies.find(policy_name);
  if (it == kRearmPolicies.end()) {
    throw std::invalid_argument("Unrecognized trellis.timers.rearm_policy '" + policy_name +
                                "', expected catch_up or skip_aligned");
  }
  options.rearm_policy = it->second;
  return options;
}

}  // namespace trellis::core

#endif  // TRELLIS_CORE_TIMER_OPTIONS_CONFIG_HPP_
