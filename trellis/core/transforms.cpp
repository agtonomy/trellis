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

Transforms::Transforms(Node& node)
    : node_{node},
      container_{},
      publisher_{nullptr},  // we construct lazily upon use
      inputs_{node,
              {{"/trellis/transforms"}},
              [this](const std::string& topic, const RigidTransform& msg, const time::TimePoint& now,
                     const time::TimePoint& when) { NewTransform(msg, when); }} {
  if (node.GetConfig()["transforms"]) {
    // Load static transforms from configuration into the container
    const auto& transforms_cfg = node.GetConfig()["transforms"];
    for (const auto& config : transforms_cfg) {
      const auto transform = CreateTransformFromConfig(Config(config));
      container_.UpdateTransform(config["from"].as<std::string>(), config["to"].as<std::string>(), transform);
    }
  }
}

void Transforms::NewTransform(const RigidTransform& msg, const time::TimePoint& when) {
  auto transform = CreateTransformFromMessage(msg);
  container_.UpdateTransform(msg.frame_from(), msg.frame_to(), transform, when);
}

bool Transforms::HasTransform(const std::string& from, const std::string& to, const time::TimePoint& when) const {
  return container_.HasTransform(from, to, when);
}

void Transforms::UpdateTransform(const std::string& from, const std::string& to,
                                 const containers::Transforms::RigidTransform& transform, const time::TimePoint& when) {
  auto msg = CreateMessageFromTransform(from, to, transform);
  // We'll add the transform to our container now since our publishes don't seem to be looping
  // back through our subscriber
  NewTransform(msg, when);

  // Not all users of this library call UpdateTransform, so we only want to create the publisher if it was used
  if (publisher_ == nullptr) {
    publisher_ = node_.CreatePublisher<RigidTransform>("/trellis/transforms");
  }
  publisher_->Send(msg, when);
}

const containers::Transforms::Sample Transforms::GetTransform(const std::string& from, const std::string& to,
                                                              const time::TimePoint& when) const {
  return container_.GetTransform(from, to, when);
}

RigidTransform Transforms::CreateMessageFromTransform(const std::string& from, const std::string& to,
                                                      const containers::Transforms::RigidTransform& transform) {
  RigidTransform msg;
  msg.mutable_translation()->set_x(transform.translation.x());
  msg.mutable_translation()->set_y(transform.translation.y());
  msg.mutable_translation()->set_z(transform.translation.z());
  msg.mutable_rotation()->set_w(transform.rotation.w());
  msg.mutable_rotation()->set_x(transform.rotation.x());
  msg.mutable_rotation()->set_y(transform.rotation.y());
  msg.mutable_rotation()->set_z(transform.rotation.z());
  msg.set_frame_from(from);
  msg.set_frame_to(to);
  return msg;
}

containers::Transforms::RigidTransform Transforms::CreateTransformFromMessage(const RigidTransform& msg) {
  containers::Transforms::RigidTransform transform;
  transform.translation.x() = msg.translation().x();
  transform.translation.y() = msg.translation().y();
  transform.translation.z() = msg.translation().z();
  transform.rotation.w() = msg.rotation().w();
  transform.rotation.x() = msg.rotation().x();
  transform.rotation.y() = msg.rotation().y();
  transform.rotation.z() = msg.rotation().z();
  return transform;
}

containers::Transforms::RigidTransform Transforms::CreateTransformFromConfig(const Config& config) {
  containers::Transforms::RigidTransform transform;
  transform.translation.x() = config["translation"]["x"].as<double>();
  transform.translation.y() = config["translation"]["y"].as<double>();
  transform.translation.z() = config["translation"]["z"].as<double>();
  transform.rotation.w() = config["rotation"]["w"].as<double>();
  transform.rotation.x() = config["rotation"]["x"].as<double>();
  transform.rotation.y() = config["rotation"]["y"].as<double>();
  transform.rotation.z() = config["rotation"]["z"].as<double>();
  return transform;
}

}  // namespace core
}  // namespace trellis
