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

#ifndef TRELLIS_CONTAINERS_TRANSFORMS_HPP
#define TRELLIS_CONTAINERS_TRANSFORMS_HPP

#include <Eigen/Geometry>
#include <string>
#include <unordered_map>
#include <utility>

#include "trellis/core/time.hpp"

namespace trellis {
namespace containers {

/**
 * Transforms a container to hold rigid coordinate frame transformations
 *
 * This class holds transforms with associated timestamps. It also maintains a validity window, which is a duration of
 * time that the transform is valid for. Lookups are done based on a timestamp. The algorithm will first find the
 * transform with the timestamp nearest the requested one. Then the validity window is used to determine if the
 * timestamp is close enough in time. There is also a notion of forever in which a transform is always valid. This is
 * useful for transforms that are known to be unchanging.
 *
 * Future improvements:
 * - Support a graph structure to allow lookups that are transitive. Say we know A -> B -> C, we should support a lookup
 * of A -> C
 */
class Transforms {
 public:
  /**
   * Tranlsation represents a translation in 3D space
   */
  using Translation = Eigen::Vector3d;

  /**
   * Rotation represents a rotation in 3D space represented as a quaternion
   */
  using Rotation = Eigen::Vector4d;

  using AffineTransform3D = Eigen::Transform<double, 3, Eigen::Affine>;

  /**
   * RigidTransform represents a transformation comprised of a translation and rotation
   *
   * NOTE: The convention is such that the translation is performed before the rotation
   */
  struct RigidTransform {
    RigidTransform() = default;

    /**
     * RigidTransform construct a rigid transfrom from an Eigen affine transform representation
     *
     * @param transform the Eigen affine transform
     */
    RigidTransform(const AffineTransform3D& transform)
        : translation{GetTranslationFromAffineTransform(transform)},
          rotation{GetRotationFromAffineTransform(transform)} {}

    /**
     * GetAffineRepresentation return an Eigen affine transform representation of this rigid transform
     *
     * @return the Eigen affine transform
     */
    AffineTransform3D GetAffineRepresentation() const {
      return AffineTransform3D(Eigen::Translation<double, 3>(translation.x(), translation.y(), translation.z()) *
                               Eigen::Quaterniond(rotation.w(), rotation.x(), rotation.y(), rotation.z()));
    }

    /**
     * Inverse retrieve the inverse transform
     *
     * @return the inverse transform
     */
    RigidTransform Inverse() const { return Transforms::RigidTransform(GetAffineRepresentation().inverse()); }

    bool operator==(const RigidTransform& other) const {
      return this->translation.x() == other.translation.x() && this->translation.y() == other.translation.y() &&
             this->translation.z() == other.translation.z() && this->rotation.w() == other.rotation.w() &&
             this->rotation.x() == other.rotation.x() && this->rotation.y() == other.rotation.y() &&
             this->rotation.z() == other.rotation.z();
    }

    bool operator!=(const RigidTransform& other) const { return !(*this == other); }

    Translation translation;
    Rotation rotation;

   private:
    static Translation GetTranslationFromAffineTransform(const AffineTransform3D& transform) {
      return Translation{transform.translation().x(), transform.translation().y(), transform.translation().z()};
    }
    static Rotation GetRotationFromAffineTransform(const AffineTransform3D& transform) {
      const Eigen::Quaterniond q(transform.rotation());
      return Rotation{q.x(), q.y(), q.z(), q.w()};
    }
  };

  struct Sample {
    const trellis::core::time::TimePoint& timestamp;
    const RigidTransform& transform;
  };

  static constexpr std::size_t kMaxTransformLengthDefault = 100U;

  Transforms(std::size_t max_transform_length = kMaxTransformLengthDefault)
      : max_transform_length_{max_transform_length} {}

  /**
   * UpdateTransform update a transform associated with the current time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @param transform the actual transformation in terms of a translation and a rotation
   */
  void UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform);

  /**
   * UpdateTransform update a transform associated with the given time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @param transform the actual transformation in terms of a translation and a rotation
   * @param when the time point to associate with the transform
   */
  void UpdateTransform(const std::string& from, const std::string& to, const RigidTransform& transform,
                       const trellis::core::time::TimePoint& when);

  /**
   * HasTransform determine if a transform for a given pair of reference frames exists and is within the valid time
   * window
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @return true if the transform exists and is valid
   *
   */
  bool HasTransform(const std::string& from, const std::string& to) const;

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
  bool HasTransform(const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when) const;

  /**
   * GetTransform retrieve the most recent transform for a given pair of reference frames
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @return the rigid transformation between the two reference frames with associated timestamp
   * @throws std::runtime_error if no valid transform exists
   */
  Sample GetTransform(const std::string& from, const std::string& to) const;

  /**
   * GetTransform retrieve the transform for a given pair of reference frames nearest the given time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @param when the time point with which to find the nearest transform
   * @return the rigid transformation between the two reference frames with associated timestamp
   * @throws std::runtime_error if no valid transform exists
   */
  Sample GetTransform(const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when) const;

 private:
  std::optional<trellis::core::time::TimePoint> FindNearestTransformTimestamp(
      const std::string& from, const std::string& to, const trellis::core::time::TimePoint& when) const;

  void Insert(const std::string& from, const std::string& to, const RigidTransform& transform,
              const trellis::core::time::TimePoint& when);

  static void ValidateFrameName(const std::string& frame);

  using KeyType = std::string;

  struct FrameNames {
    const std::string from;
    const std::string to;
  };

  static KeyType CalculateKeyFromFrames(const std::string& from, const std::string& to);
  static FrameNames GetFrameNamesFromKey(const KeyType& key);

  static constexpr char kDelimiter = '|';

  using TransformHistoryContainer = std::map<trellis::core::time::TimePoint, RigidTransform>;
  std::unordered_map<KeyType, TransformHistoryContainer> transforms_;
  const std::size_t max_transform_length_;
};
}  // namespace containers
}  // namespace trellis

#endif  // TRELLIS_CONTAINERS_TRANSFORMS_HPP
