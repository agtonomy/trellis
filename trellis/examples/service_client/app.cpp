#include "app.hpp"

namespace trellis {
namespace examples {
namespace service_client {

using namespace trellis::core;
using namespace trellis::examples::proto;

App::App(Node& node)
    : client_{node.CreateServiceClient<AdditionService>()},
      timer_{node.CreateTimer(
          node.GetConfig()["examples"]["service"]["interval_ms"].as<unsigned>(),
          [this](const trellis::core::time::TimePoint&) { Tick(); },
          node.GetConfig()["examples"]["service"]["initial_delay_ms"].as<unsigned>())},
      call_timeout_ms_{node.GetConfig()["examples"]["service"]["timeout_ms"].as<unsigned>()} {}

void App::HandleResponse(ServiceCallStatus status, const AdditionResponse* resp) {
  if (status == ServiceCallStatus::kFailure) {
    Log::Error("Request responded with failure!");
  } else if (status == ServiceCallStatus::kTimedOut) {
    Log::Error("Request timed out!");
  } else if (status == core::ServiceCallStatus::kSuccess) {
    Log::Info("Received response {}", resp->sum());
  }
}

void App::Tick() {
  ++request_count_;

  AdditionRequest req;
  unsigned arg1 = request_count_ * 2;
  unsigned arg2 = request_count_ * 3;

  req.set_arg1(arg1);
  req.set_arg2(arg2);

  Log::Info("Sending request for {} + {}", arg1, arg2);
  arg1 += 2;
  arg2 += 3;
  client_->CallAsync<AdditionRequest, AdditionResponse>(
      "Add", req, [this](ServiceCallStatus status, const AdditionResponse* resp) { HandleResponse(status, resp); },
      call_timeout_ms_);
}

}  // namespace service_client
}  // namespace examples
}  // namespace trellis
