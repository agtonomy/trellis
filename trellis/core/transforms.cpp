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

Transforms::Transforms(trellis::core::Node& node)
    : container_{},
      publisher_{node.CreatePublisher<trellis::core::RigidTransform>("/trellis/transforms")},
      inputs_{node,
              {{"/trellis/transforms"}},
              [this](const std::string& topic, const trellis::core::RigidTransform& msg, const time::TimePoint& when) {
                NewTransform(msg, when);
              }} {
  // TODO pull static transforms from config
}

void Transforms::NewTransform(const trellis::core::RigidTransform& msg, const time::TimePoint& when) {
  containers::Transforms::RigidTransform transform;
  transform.translation.x = msg.translation().x();
  transform.translation.y = msg.translation().y();
  transform.translation.z = msg.translation().z();
  transform.rotation.w = msg.rotation().w();
  transform.rotation.x = msg.rotation().x();
  transform.rotation.y = msg.rotation().y();
  transform.rotation.z = msg.rotation().z();
  container_.UpdateTransform(msg.frame_from(), msg.frame_to(), transform,
                             std::chrono::milliseconds(msg.validity_window_ms()), when);
}

bool Transforms::HasTransform(const std::string& from, const std::string& to,
                              const trellis::core::time::TimePoint& when) {
  return container_.HasTransform(from, to, when);
}

void Transforms::UpdateTransform(const std::string& from, const std::string& to,
                                 const containers::Transforms::RigidTransform& transform,
                                 std::chrono::milliseconds validity_window) {
  trellis::core::RigidTransform msg;
  msg.mutable_translation()->set_x(transform.translation.x);
  msg.mutable_translation()->set_y(transform.translation.y);
  msg.mutable_translation()->set_z(transform.translation.z);
  msg.mutable_rotation()->set_w(transform.rotation.w);
  msg.mutable_rotation()->set_x(transform.rotation.x);
  msg.mutable_rotation()->set_y(transform.rotation.y);
  msg.mutable_rotation()->set_z(transform.rotation.z);
  msg.set_frame_from(from);
  msg.set_frame_to(to);
  msg.set_validity_window_ms(validity_window.count());
  publisher_->Send(msg);
}

const containers::Transforms::RigidTransform& Transforms::GetTransform(const std::string& from, const std::string& to,
                                                                       const trellis::core::time::TimePoint& when) {
  return container_.GetTransform(from, to, when);
}

}  // namespace core
}  // namespace trellis
