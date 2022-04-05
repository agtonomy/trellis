#ifndef TRELLIS_EXAMPLES_SERVICE_CLIENT_APP_HPP
#define TRELLIS_EXAMPLES_SERVICE_CLIENT_APP_HPP

#include "trellis/core/node.hpp"
#include "trellis/examples/proto/addition_service.pb.h"

namespace trellis {
namespace examples {
namespace service_client {

class App {
 public:
  App(trellis::core::Node& node, const trellis::core::Config& config);

 private:
  void HandleResponse(trellis::core::ServiceCallStatus status, const trellis::examples::proto::AdditionResponse* resp);
  void Tick();

  trellis::core::ServiceClient<trellis::examples::proto::AdditionService> client_;
  trellis::core::Timer timer_;
  const unsigned call_timeout_ms_;
  unsigned request_count_{0};
};

}  // namespace service_client
}  // namespace examples
}  // namespace trellis

#endif  // TRELLIS_EXAMPLES_SERVICE_CLIENT_APP_HPP
