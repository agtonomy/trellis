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
  /**
   * Tranlsation represents a translation in 3D space
   */
  struct Translation {
    double x;
    double y;
    double z;
  };

  /**
   * Rotation represents a rotation in 3D space represented as a quaternion
   */
  struct Rotation {
    double x;
    double y;
    double z;
    double w;
  };

  /**
   * RigidTransform represents a transformation comprised of a tranlsation and rotation
   */
  struct RigidTransform {
    Translation translation;
    Rotation rotation;
  };

  static constexpr std::chrono::milliseconds kForever = std::chrono::milliseconds::max();
  static constexpr std::size_t kMaxTransformLength = 100U;

  Transforms(std::size_t max_transform_length = kMaxTransformLength) : max_transform_length_{max_transform_length} {}

  /**
   * UpdateTransform update a transform associated with the current time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @param transform the actual transformation in terms of a translation and a rotation
   * @param validity_window how long the transform is valid for (static transforms should use kForever)
   */
  void UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform,
                       std::chrono::milliseconds validity_window = kForever);

  /**
   * UpdateTransform update a transform associated with the given time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @param transform the actual transformation in terms of a translation and a rotation
   * @param validity_window how long the transform is valid for (static transforms should use kForever)
   * @param when the time point to associate with the transform
   */
  void UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform,
                       std::chrono::milliseconds validity_window, const trellis::core::time::TimePoint& when);

  /**
   * HasTransform determine if a transform for a given pair of reference frames exists and is within the valid time
   * window
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @return true if the transform exists and is valid
   *
   */
  bool HasTransform(const std::string& from, const std::string& to);

  /**
   * HasTransform determine if a transform for a given pair of reference frames exists and is within the valid time
   * window relative to the given time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @param when the time point to associate with the transform
   * @return true if the transform exists and is valid
   *
   */
  bool HasTransform(const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when);

  /**
   * GetTransform retrieve the most recent transform for a given pair of reference frames
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @return the rigid transformation between the two reference frames
   * @throws std::runtime_error if no valid transform exists
   */
  RigidTransform GetTransform(const std::string& from, const std::string& to);

  /**
   * GetTransform retrieve the transform for a given pair of reference frames nearest the given time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @return the rigid transformation between the two reference frames
   * @param when the time point with which to find the nearest transform
   * @throws std::runtime_error if no valid transform exists
   */
  RigidTransform GetTransform(const std::string& from, const std::string& to,
                              const trellis::core::time::TimePoint& when);

 private:
  std::optional<trellis::core::time::TimePoint> FindNearestTransformTimestamp(
      const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when);

  using KeyType = std::string;

  struct FrameNames {
    const std::string from;
    const std::string to;
  };

  struct TransformData {
    RigidTransform transform;
    std::chrono::milliseconds validity_window;
  };

  static KeyType CalculateKeyFromFrames(const std::string& from, const std::string& to);
  static FrameNames GetFrameNamesFromKey(const KeyType& key);

  using TransformHistoryContainer = std::map<trellis::core::time::TimePoint, TransformData>;
  std::unordered_map<KeyType, TransformHistoryContainer> transforms_;
  const std::size_t max_transform_length_;
};
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TRANSFORMS_HPP
