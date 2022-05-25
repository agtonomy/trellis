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

App::App(Witch witch, Node& node)
    : witch_{witch},
      validity_window_ms{node.GetConfig()["examples"]["transforms"]["validity_window_ms"].as<unsigned>()},
      transforms_{node},
      timer_{node.CreateTimer(
          node.GetConfig()["examples"]["transforms"]["interval_ms"].as<unsigned>(), [this]() { Tick(); },
          node.GetConfig()["examples"]["transforms"]["initial_delay_ms"].as<unsigned>())} {}

void App::Tick() {
  const trellis::containers::Transforms::RigidTransform transform =
      (witch_ == Witch::kNodeA)
          ? trellis::containers::Transforms::RigidTransform(Eigen::Translation<double, 3>(3.0, 4.0, 5.0) *
                                                            Eigen::Quaterniond(0.70710678118, 0.0, 0.0, 0.70710678118))
          : trellis::containers::Transforms::RigidTransform(Eigen::Translation<double, 3>(1.0, 2.0, 3.0) *
                                                            Eigen::Quaterniond(1.0, 0.0, 0.0, 0.0));

  const std::string from = (witch_ == Witch::kNodeA) ? "nodea_sensor" : "nodeb_sensor";
  const std::string other_from = (witch_ == Witch::kNodeA) ? "nodeb_sensor" : "nodea_sensor";
  const std::string to = "base";

  transforms_.UpdateTransform(from, to, transform, std::chrono::milliseconds(validity_window_ms));

  CheckAndPrint("sensor", "base");  // static transform from config
  CheckAndPrint(from, to);          // the transform we're sending
  CheckAndPrint(other_from, to);    // the transform we're receiving

  Log::Info("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
}

void App::CheckAndPrint(const std::string& from, const std::string& to) const {
  if (transforms_.HasTransform(from, to, trellis::core::time::Now())) {
    const auto& transform = transforms_.GetTransform(from, to, trellis::core::time::Now());
    Log::Info("We have the {} -> {} transform [{}, {}, {}] [{}, {}, {}, {}]", from, to, transform.translation.x,
              transform.translation.y, transform.translation.z, transform.rotation.w, transform.rotation.x,
              transform.rotation.y, transform.rotation.z);
  } else {
    Log::Info("We don't have the {} -> {} transform", from, to);
  }
}

}  // namespace transforms
}  // namespace examples
}  // namespace trellis
