#ifndef TRELLIS_EXAMPLES_SERVICE_CLIENT_APP_HPP
#define TRELLIS_EXAMPLES_SERVICE_CLIENT_APP_HPP

#include "trellis/core/message_consumer.hpp"
#include "trellis/core/node.hpp"
#include "trellis/examples/proto/addition_service.pb.h"

namespace trellis {
namespace examples {
namespace service_client {

class App {
 public:
  App(const trellis::core::Node& node, const trellis::core::Config& config);

 private:
  void HandleResponse(const trellis::examples::proto::AdditionResponse* resp);
  void Tick();

  trellis::core::ServiceClient<trellis::examples::proto::AdditionService> client_;
  trellis::core::Timer timer_;

  unsigned request_count_{0};
};

}  // namespace service_client
}  // namespace examples
}  // namespace trellis

#endif  // TRELLIS_EXAMPLES_SERVICE_CLIENT_APP_HPP
