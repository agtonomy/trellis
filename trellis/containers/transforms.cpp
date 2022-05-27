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
namespace containers {

void Transforms::UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform) {
  UpdateTransform(from, to, transform, core::time::Now());
}

void Transforms::UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform,
                                 const trellis::core::time::TimePoint& when) {
  Insert(from, to, transform, when);
  // We will derive and insert the inverse transform as well
  Insert(to, from, transform.Inverse(), when);
}

bool Transforms::HasTransform(const std::string& from, const std::string& to) const {
  return HasTransform(from, to, core::time::Now());
}

bool Transforms::HasTransform(const std::string& from, const std::string& to,
                              const trellis::core::time::TimePoint& when) const {
  const auto timestamp = FindNearestTransformTimestamp(from, to, when);
  return !timestamp ? false : true;
}

Transforms::Sample Transforms::GetTransform(const std::string& from, const std::string& to) const {
  return GetTransform(from, to, core::time::Now());
}

Transforms::Sample Transforms::GetTransform(const std::string& from, const std::string& to,
                                            const trellis::core::time::TimePoint& when) const {
  const auto timestamp = FindNearestTransformTimestamp(from, to, when);

  if (!timestamp) {
    std::stringstream msg;
    msg << "No transform found for " << from << " -> " << to << " at " << trellis::core::time::TimePointToSeconds(when);
    throw std::runtime_error(msg.str());
  }

  // Our value is guaranteed to exist at this point
  const auto& transform_map = transforms_.at(CalculateKeyFromFrames(from, to));
  // const auto& transform = transform_map.at(*timestamp);
  const auto it = transform_map.find(*timestamp);
  return Sample{it->first, it->second};
}

std::optional<trellis::core::time::TimePoint> Transforms::FindNearestTransformTimestamp(
    const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when) const {
  const auto transform_map_it = transforms_.find(CalculateKeyFromFrames(from, to));
  if (transform_map_it == transforms_.end()) {
    return {};  // transform doesn't exist at all
  }
  const auto& transform_map = transform_map_it->second;
  if (transform_map.empty()) {
    return {};  // no records exist
  }

  // At this point we know the map has at least one item in it, so we can count on std::prev(transform_map.end())
  auto it = transform_map.lower_bound(when);

  if (it == transform_map.end()) {
    // In this case there was nothing greater than or equal to our time point, so let's just look at the most recent
    it = std::prev(transform_map.end());
  }

  if (it == transform_map.begin()) {
    return it->first;
  }

  // At this point we're not looking at the oldest timestamp so we can look at the previous as well
  const auto it_prev = std::prev(it);
  const auto time_delta = std::chrono::abs(std::chrono::duration_cast<std::chrono::milliseconds>(when - it->first));
  const auto time_delta_prev =
      std::chrono::abs(std::chrono::duration_cast<std::chrono::milliseconds>(when - it_prev->first));

  return (time_delta < time_delta_prev) ? it->first : it_prev->first;
}

Transforms::KeyType Transforms::CalculateKeyFromFrames(const std::string& from, const std::string& to) {
  ValidateFrameName(from);
  ValidateFrameName(to);
  return from + kDelimiter + to;
}

void Transforms::Insert(const std::string& from, const std::string& to, const RigidTransform& transform,
                        const trellis::core::time::TimePoint& when) {
  auto& transform_map = transforms_[CalculateKeyFromFrames(from, to)];
  transform_map.emplace(std::make_pair(when, transform));
  if (transform_map.size() > max_transform_length_) {
    transform_map.erase(transform_map.begin());
  }
}

void Transforms::ValidateFrameName(const std::string& frame) {
  if (frame.find(kDelimiter) != std::string::npos) {
    std::stringstream msg;
    msg << "Frame name must not contain '" << kDelimiter << "'. Got " << frame;
    throw std::invalid_argument(msg.str());
  }
}

}  // namespace containers
}  // namespace trellis
