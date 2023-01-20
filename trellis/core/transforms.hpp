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

#ifndef TRELLIS_CORE_TRANSFORMS_HPP_
#define TRELLIS_CORE_TRANSFORMS_HPP_

#include "trellis/containers/transforms.hpp"
#include "trellis/core/config.hpp"
#include "trellis/core/message_consumer.hpp"
#include "trellis/core/publisher.hpp"
#include "trellis/core/rigid_transform.pb.h"

namespace trellis {
namespace core {

/**
 * Transforms a utility to distribute and receive rigid coordinate frame transforms
 *
 * This uses pub/sub under the hood to distribute coordinate transforms that are updated during runtime. It also loads
 * and stores transforms from configuration that are static and unchanging during runtime.
 *
 * Configuration:
 * - This module will check the node's configuration for the toplevel "transforms" key and expect a list of transforms
 * underneath.
 * Example...
 * transforms:
 *  - from: "sensor"
 *    to: "base"
 *    translation:
 *      x: 1.0
 *      y: 0.0
 *      z: 0.5
 *    rotation:
 *      w: 0.707
 *      x: 0.0
 *      y: 0.0
 *      z: 0.707
 */
class Transforms {
 public:
  Transforms(Node& node);

  /**
   * UpdateTransform update a transform associated with the current time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @param transform the actual transformation in terms of a translation and a rotation
   * @param when the time point to associate with the transform
   */
  void UpdateTransform(const std::string& from, const std::string& to,
                       const containers::Transforms::RigidTransform& transform,
                       const time::TimePoint& when = time::Now());

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
  bool HasTransform(const std::string& from, const std::string& to, const time::TimePoint& when = time::Now()) const;

  /**
   * GetTransform retrieve the transform for a given pair of reference frames nearest the given time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @return the rigid transformation between the two reference frames
   * @param when the time point with which to find the nearest transform
   * @throws std::runtime_error if no valid transform exists
   */
  const containers::Transforms::Sample GetTransform(const std::string& from, const std::string& to,
                                                    const time::TimePoint& when = time::Now()) const;

  /**
   * @brief Create a proto from a RigidTransform object.
   *
   * @param from The frame to transform from.
   * @param to The frame to transform to.
   * @param transform The transform.
   * @return RigidTransform The created proto.
   */
  static RigidTransform CreateMessageFromTransform(const std::string& from, const std::string& to,
                                                   const containers::Transforms::RigidTransform& transform);

  /**
   * @brief Create a RigidTransform from a proto.
   *
   * @param msg The proto containing the transform.
   * @return containers::Transforms::RigidTransform The created transform.
   */
  static containers::Transforms::RigidTransform CreateTransformFromMessage(const RigidTransform& msg);

  /**
   * @brief Create a RigidTransform from a config.
   *
   * @param config The config containing the transform.
   * @return containers::Transforms::RigidTransform The created transform.
   */
  static containers::Transforms::RigidTransform CreateTransformFromConfig(const Config& config);

 private:
  void NewTransform(const RigidTransform& msg, const time::TimePoint& when);

  static constexpr unsigned kQueueDepth = 10u;
  containers::Transforms container_;

  Publisher<RigidTransform> publisher_;
  MessageConsumer<kQueueDepth, RigidTransform> inputs_;
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TRANSFORMS_HPP_
