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

#include "app.hpp"

namespace trellis {
namespace examples {
namespace transforms {

using namespace trellis::core;

App::App(Which which, Node& node)
    : which_{which},
      transforms_{node},
      timer_{node.CreateTimer(
          node.GetConfig()["examples"]["transforms"]["interval_ms"].as<unsigned>(),
          [this](const trellis::core::time::TimePoint&) { Tick(); },
          node.GetConfig()["examples"]["transforms"]["initial_delay_ms"].as<unsigned>())} {}

void App::Tick() {
  // Send a different transform depending on which instance we are for the sake of demonstration
  const trellis::containers::Transforms::RigidTransform transform =
      (which_ == Which::kNodeA)
          ? trellis::containers::Transforms::RigidTransform(Eigen::Translation<double, 3>(3.0, 4.0, 5.0) *
                                                            Eigen::Quaterniond(0.70710678118, 0.0, 0.0, 0.70710678118))
          : trellis::containers::Transforms::RigidTransform(Eigen::Translation<double, 3>(1.0, 2.0, 3.0) *
                                                            Eigen::Quaterniond(1.0, 0.0, 0.0, 0.0));

  // Use a different "from" coordinate frame based on which node we are
  const std::string from = (which_ == Which::kNodeA) ? "nodea_sensor" : "nodeb_sensor";
  const std::string other_from = (which_ == Which::kNodeA) ? "nodeb_sensor" : "nodea_sensor";
  const std::string to = "base";

  // We update the transform, which will also update any other node that has a transform object
  transforms_.UpdateTransform(from, to, transform);

  // Print the valid transorms we have
  CheckAndPrint("sensor", "base");  // static transform from config
  CheckAndPrint(from, to);          // the transform we're sending
  CheckAndPrint(other_from, to);    // the transform we're receiving

  Log::Info("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
}

void App::CheckAndPrint(const std::string& from, const std::string& to) const {
  if (transforms_.HasTransform(from, to)) {
    const auto& [timestamp, transform] = transforms_.GetTransform(from, to);
    const double delta_sec =
        std::chrono::duration_cast<std::chrono::milliseconds>(time::Now() - timestamp).count() / 1000.0;
    Log::Info("We have the {} -> {} transform [{}, {}, {}] [{}, {}, {}, {}] which is {} seconds old", from, to,
              transform.translation.x(), transform.translation.y(), transform.translation.z(), transform.rotation.w(),
              transform.rotation.x(), transform.rotation.y(), transform.rotation.z(), delta_sec);

    // Demonstrating how we can use the transform on an Eigen 3D vector
    const Eigen::Vector3d rotated = transform.GetAffineRepresentation() * Eigen::Vector3d(1.0, 0.0, 0.0);
    (void)rotated;  // not used, just for demonstration
  } else {
    Log::Info("We don't have the {} -> {} transform", from, to);
  }
}

}  // namespace transforms
}  // namespace examples
}  // namespace trellis
