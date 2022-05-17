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

#ifndef TRELLIS_CORE_TRANSFORMS_HPP
#define TRELLIS_CORE_TRANSFORMS_HPP

#include <string>
#include <unordered_map>
#include <utility>

#include "time.hpp"

namespace trellis {
namespace core {
class Transforms {
 public:
  struct Translation {
    double x;
    double y;
    double z;
  };

  struct Rotation {
    double x;
    double y;
    double z;
    double w;
  };

  struct RigidTransform {
    Translation translation;
    Rotation rotation;
  };

  static constexpr std::chrono::milliseconds kMaxDeltaDefault = std::chrono::milliseconds(50);

  Transforms(std::chrono::milliseconds max_history_duration) : max_history_duration_{max_history_duration} {}

  void UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform);

  void UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform,
                       const trellis::core::time::TimePoint& when);

  bool HasTransform(const std::string& from, const std::string& to);

  bool HasTransform(const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when,
                    const std::chrono::milliseconds max_delta = kMaxDeltaDefault);

  const RigidTransform& GetTransform(const std::string& from, const std::string& to);

  const RigidTransform& GetTransform(const std::string& from, const std::string& to,
                                     const trellis::core::time::TimePoint& when,
                                     const std::chrono::milliseconds max_delta = kMaxDeltaDefault);

 private:
  std::optional<trellis::core::time::TimePoint> FindNearestTransformTimestamp(
      const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when,
      const std::chrono::milliseconds max_delta);

  void PurgeStaleTransforms();

  using KeyType = std::string;

  struct FrameNames {
    const std::string from;
    const std::string to;
  };

  static KeyType CalculateKeyFromFrames(const std::string& from, const std::string& to);
  static FrameNames GetFrameNamesFromKey(const KeyType& key);

  using TransformHistoryContainer = std::map<trellis::core::time::TimePoint, RigidTransform>;
  std::unordered_map<KeyType, TransformHistoryContainer> transforms_;

  const std::chrono::milliseconds max_history_duration_;
};
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TRANSFORMS_HPP
