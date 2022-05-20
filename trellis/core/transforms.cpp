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

#include "transforms.hpp"

#include <sstream>

namespace trellis {
namespace core {

void Transforms::UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform,
                                 std::chrono::milliseconds validity_window) {
  UpdateTransform(from, to, transform, validity_window, time::Now());
}

void Transforms::UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform,
                                 std::chrono::milliseconds validity_window,
                                 const trellis::core::time::TimePoint& when) {
  auto& transform_map = transforms_[CalculateKeyFromFrames(from, to)];
  TransformData entry{transform, validity_window};
  transform_map.insert({when, entry});
}

Transforms::RigidTransform Transforms::GetTransform(const std::string& from, const std::string& to) {
  return GetTransform(from, to, time::Now());
}

bool Transforms::HasTransform(const std::string& from, const std::string& to) {
  return HasTransform(from, to, time::Now());
}

bool Transforms::HasTransform(const std::string& from, const std::string& to,
                              const trellis::core::time::TimePoint& when) {
  const auto timestamp = FindNearestTransformTimestamp(from, to, when);
  return !timestamp ? false : true;
}

Transforms::RigidTransform Transforms::GetTransform(const std::string& from, const std::string& to,
                                                    const trellis::core::time::TimePoint& when) {
  auto timestamp = FindNearestTransformTimestamp(from, to, when);

  if (!timestamp) {
    std::stringstream msg;
    msg << "No transform found for " << from << " -> " << to << " at " << trellis::core::time::TimePointToSeconds(when);
    throw std::runtime_error(msg.str());
  }

  // Our value is guaranteed to exist at this point
  const auto& transform_map = transforms_.at(CalculateKeyFromFrames(from, to));
  return transform_map.at(*timestamp).transform;
}

std::optional<trellis::core::time::TimePoint> Transforms::FindNearestTransformTimestamp(
    const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when) {
  const auto transform_map_it = transforms_.find(CalculateKeyFromFrames(from, to));
  if (transform_map_it == transforms_.end()) {
    return {};  // transform doesn't exist at all
  }
  const auto& transform_map = transform_map_it->second;
  if (transform_map.empty()) {
    return {};  // no records exist
  }

  // At this point we know the map has at least one item in it, so we can count on .begin()
  auto it = transform_map.lower_bound(when);

  if (it == transform_map.end()) {
    // In this case there was nothing greater than or equal to our time point, so let's just look at the most recent
    it = transform_map.begin();
  }

  if (it == transform_map.begin()) {
    // If we're looking at the most recent time, we just have this one item to evaluate
    auto time_delta = std::chrono::abs(std::chrono::duration_cast<std::chrono::milliseconds>(when - it->first));
    if (time_delta <= it->second.validity_window) {
      return it->first;
    } else {
      return {};
    }
  }

  // At this point we're not looking at the most recent timestamp so we can look at the previous as well
  const auto it_prev = std::prev(it);
  const auto time_delta = std::chrono::abs(std::chrono::duration_cast<std::chrono::milliseconds>(when - it->first));
  const auto time_delta_prev =
      std::chrono::abs(std::chrono::duration_cast<std::chrono::milliseconds>(when - it->first));

  if (time_delta <= it->second.validity_window || time_delta_prev <= it_prev->second.validity_window) {
    return (time_delta < time_delta_prev) ? it->first : it_prev->first;
  }

  return {};  // these times are still too far away
}

void Transforms::PurgeStaleTransforms() {
  const trellis::core::time::TimePoint cutoff = trellis::core::time::Now() - max_history_duration_;

  for (auto& [key, transform_map] : transforms_) {
    if (transform_map.empty()) {
      continue;
    }
    auto it = transform_map.lower_bound(cutoff);
    while (it != transform_map.end()) {
      auto prev = std::prev(it);
      transform_map.erase(it);
      it = prev;
    }
  }
}

Transforms::KeyType Transforms::CalculateKeyFromFrames(const std::string& from, const std::string& to) {
  return from + "|" + to;
}

}  // namespace core
}  // namespace trellis
