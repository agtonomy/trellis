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

#ifndef TRELLIS_EXAMPLES_PUBLISHER_APP_HPP_
#define TRELLIS_EXAMPLES_PUBLISHER_APP_HPP_

#include "trellis/core/node.hpp"
#include "trellis/examples/proto/hello_world.pb.h"

namespace trellis {
namespace examples {
namespace publisher {

class App {
 public:
  App(trellis::core::Node& node);

 private:
  void Tick();
  trellis::core::Publisher<trellis::examples::proto::HelloWorld> publisher_;
  trellis::core::Timer timer_;
  const unsigned repeated_field_len_;

  unsigned send_count_{0};
};

}  // namespace publisher
}  // namespace examples
}  // namespace trellis

#endif  // TRELLIS_EXAMPLES_PUBLISHER_APP_HPP_
