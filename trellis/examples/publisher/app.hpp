#ifndef TRELLIS_EXAMPLES_PUBLISHER_APP_HPP
#define TRELLIS_EXAMPLES_PUBLISHER_APP_HPP

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

  unsigned send_count_{0};
};

}  // namespace publisher
}  // namespace examples
}  // namespace trellis

#endif  // TRELLIS_EXAMPLES_PUBLISHER_APP_HPP
