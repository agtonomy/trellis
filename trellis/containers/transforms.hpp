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
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "trellis/core/time.hpp"

namespace trellis {
namespace containers {

/**
 * Transforms a container to hold rigid coordinate frame transformations
 *
 * This class holds transforms with associated timestamps. Lookups are done based on a timestamp. The algorithm will
 * find the transform with the timestamp nearest the requested one.
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

  /**
   * AffineTransform3D an Eigen 3-dimensional affine transform representation
   * TODO(matt): Deprecate in favor of Isometry, a more efficient and accurate representation.
   */
  using AffineTransform3D = Eigen::Transform<double, 3, Eigen::Affine>;

  /**
   * @brief An Eigen 3-dimensional isometry representation
   */
  using Isometry3D = Eigen::Transform<double, 3, Eigen::Isometry>;

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
        : translation{transform.translation()}, rotation{Eigen::Quaterniond{transform.rotation()}.coeffs()} {}

    /**
     * @brief Construct a rigid transfrom from an Eigen isometry representation
     *
     * @param isometry the Eigen isometry
     */
    RigidTransform(const Isometry3D& isometry)
        : translation{isometry.translation()}, rotation{Eigen::Quaterniond{isometry.rotation()}.coeffs()} {}

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
     * @brief Get the Isometry representation.
     *
     * @return the isometry
     */
    Isometry3D GetIsometry() const { return Eigen::Translation<double, 3>{translation} * Eigen::Quaterniond{rotation}; }

    /**
     * Inverse retrieve the inverse transform
     *
     * @return the inverse transform
     */
    RigidTransform Inverse() const { return Transforms::RigidTransform(GetIsometry().inverse()); }

    Translation translation;
    Rotation rotation;

   private:
    /**
     * @brief Exact equality operator.
     *
     * Using hidden friend idiom to avoid polluting namespace.
     *
     * Note: for approximations use Eigen's GetIsometry().isApprox() instead
     *
     * @returns true if both operands are exact copies of each other
     */
    friend bool operator==(const RigidTransform&, const RigidTransform&) = default;
    friend bool operator!=(const RigidTransform&, const RigidTransform&) = default;
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
