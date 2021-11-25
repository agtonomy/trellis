#ifndef TRELLIS_EXAMPLES_SUBSCRIBER_APP_HPP
#define TRELLIS_EXAMPLES_SUBSCRIBER_APP_HPP

#include "trellis/core/message_consumer.hpp"
#include "trellis/core/node.hpp"
#include "trellis/examples/proto/hello_world.pb.h"

namespace trellis {
namespace examples {
namespace subscriber {

class App {
 public:
  App(const trellis::core::Node& node, const trellis::core::Config& config);

 private:
  void NewMessage(const std::string& topic, const trellis::examples::proto::HelloWorld& msg);

  trellis::core::MessageConsumer<1, trellis::examples::proto::HelloWorld> inputs_;
};

}  // namespace subscriber
}  // namespace examples
}  // namespace trellis

#endif  // TRELLIS_EXAMPLES_SUBSCRIBER_APP_HPP
