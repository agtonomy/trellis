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

#ifndef TRELLIS_EXAMPLES_TRANSFORMS_APP_HPP
#define TRELLIS_EXAMPLES_TRANSFORMS_APP_HPP

#include "trellis/core/node.hpp"
#include "trellis/core/transforms.hpp"

namespace trellis {
namespace examples {
namespace transforms {

class App {
 public:
  enum Which { kNodeA = 0, kNodeB };
  App(Which which, trellis::core::Node& node);

 private:
  void Tick();
  void CheckAndPrint(const std::string& from, const std::string& to) const;

  const Which which_;  // which node are we
  const unsigned validity_window_ms;

  trellis::core::Transforms transforms_;
  trellis::core::Timer timer_;
};

}  // namespace transforms
}  // namespace examples
}  // namespace trellis

#endif  // TRELLIS_EXAMPLES_TRANSFORMS_APP_HPP
