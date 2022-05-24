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

#include "trellis/containers/transforms.hpp"
#include "trellis/core/config.hpp"
#include "trellis/core/message_consumer.hpp"
#include "trellis/core/publisher.hpp"
#include "trellis/core/rigid_transform.pb.h"

namespace trellis {
namespace core {

class Transforms {
 public:
  Transforms(trellis::core::Node& node);

  /**
   * UpdateTransform update a transform associated with the current time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @param transform the actual transformation in terms of a translation and a rotation
   * @param validity_window how long the transform is valid for (static transforms should use kForever)
   */
  void UpdateTransform(const std::string& from, const std::string& to,
                       const containers::Transforms::RigidTransform& transform,
                       std::chrono::milliseconds validity_window);

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
   * GetTransform retrieve the transform for a given pair of reference frames nearest the given time
   *
   * @param from the starting reference frame for the transform
   * @param to the ending reference frame for the transform
   * @return the rigid transformation between the two reference frames
   * @param when the time point with which to find the nearest transform
   * @throws std::runtime_error if no valid transform exists
   */
  const containers::Transforms::RigidTransform& GetTransform(const std::string& from, const std::string& to,
                                                             const trellis::core::time::TimePoint& when);

 private:
  void NewTransform(const trellis::core::RigidTransform& msg, const time::TimePoint& when);
  static trellis::core::RigidTransform CreateMessageFromTransform(
      const std::string& from, const std::string& to, const containers::Transforms::RigidTransform& transform,
      std::chrono::milliseconds validity_window);
  static containers::Transforms::RigidTransform CreateTransformFromMessage(const trellis::core::RigidTransform& msg);
  static containers::Transforms::RigidTransform CreateTransformFromConfig(const trellis::core::Config& config);

  static constexpr unsigned kQueueDepth = 10u;
  containers::Transforms container_;

  trellis::core::Publisher<trellis::core::RigidTransform> publisher_;
  trellis::core::MessageConsumer<kQueueDepth, trellis::core::RigidTransform> inputs_;
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TRANSFORMS_HPP
