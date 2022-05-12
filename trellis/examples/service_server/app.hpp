#ifndef TRELLIS_EXAMPLES_SERVICE_SERVER_APP_HPP
#define TRELLIS_EXAMPLES_SERVICE_SERVER_APP_HPP

#include "trellis/core/node.hpp"
#include "trellis/examples/proto/addition_service.pb.h"

namespace trellis {
namespace examples {
namespace service_server {

class App {
 public:
  App(trellis::core::Node& node);

 private:
  trellis::core::ServiceServer<trellis::examples::proto::AdditionService> server_;
};

}  // namespace service_server
}  // namespace examples
}  // namespace trellis

#endif  // TRELLIS_EXAMPLES_SERVICE_SERVER_APP_HPP
