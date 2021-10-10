#include "app.hpp"

namespace trellis {
namespace examples {
namespace service_server {

using namespace trellis::core;
using namespace trellis::examples::proto;

class AdditionServiceImpl : public AdditionService {
  void Add(::google::protobuf::RpcController* /* controller_ */, const AdditionRequest* request_,
           AdditionResponse* response_, ::google::protobuf::Closure* /* done_ */) override {
    const auto arg1 = request_->arg1();
    const auto arg2 = request_->arg2();
    const auto result = arg1 + arg2;
    Log::Info("Got request for {} + {}, sum is {}", arg1, arg2, result);
    response_->set_sum(result);
  }
};

App::App(const Node& node, const Config& config) : server_{node.CreateServiceServer<AdditionService>(std::make_shared<AdditionServiceImpl>())} {}

}  // namespace service_server
}  // namespace examples
}  // namespace trellis
