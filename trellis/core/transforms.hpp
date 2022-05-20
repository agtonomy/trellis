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

  struct TransformData {
    RigidTransform transform;
    std::chrono::milliseconds validity_window;
  };

  static constexpr std::chrono::milliseconds kForever = std::chrono::milliseconds::max();

  Transforms(std::chrono::milliseconds max_history_duration) : max_history_duration_{max_history_duration} {}

  void UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform,
                       std::chrono::milliseconds validity_window = kForever);

  void UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform,
                       std::chrono::milliseconds validity_window, const trellis::core::time::TimePoint& when);

  bool HasTransform(const std::string& from, const std::string& to);

  bool HasTransform(const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when);

  RigidTransform GetTransform(const std::string& from, const std::string& to);

  RigidTransform GetTransform(const std::string& from, const std::string& to,
                              const trellis::core::time::TimePoint& when);

 private:
  std::optional<trellis::core::time::TimePoint> FindNearestTransformTimestamp(
      const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when);

  void PurgeStaleTransforms();

  using KeyType = std::string;

  struct FrameNames {
    const std::string from;
    const std::string to;
  };

  static KeyType CalculateKeyFromFrames(const std::string& from, const std::string& to);
  static FrameNames GetFrameNamesFromKey(const KeyType& key);

  const std::chrono::milliseconds max_history_duration_;

  using TransformHistoryContainer = std::map<trellis::core::time::TimePoint, TransformData>;
  std::unordered_map<KeyType, TransformHistoryContainer> transforms_;
};
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TRANSFORMS_HPP
