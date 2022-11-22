#include "app.hpp"

namespace trellis {
namespace examples {
namespace publisher {

using namespace trellis::core;

App::App(Node& node)
    : publisher_{node.CreatePublisher<trellis::examples::proto::HelloWorld>(
          node.GetConfig()["examples"]["publisher"]["topic"].as<std::string>())},
      timer_{node.CreateTimer(
          node.GetConfig()["examples"]["publisher"]["interval_ms"].as<unsigned>(),
          [this](const trellis::core::time::TimePoint&) { Tick(); },
          node.GetConfig()["examples"]["publisher"]["initial_delay_ms"].as<unsigned>())} {}

void App::Tick() {
  const unsigned msg_number = send_count_++;
  Log::Info("Publishing message number {}", msg_number);

  trellis::examples::proto::HelloWorld msg;
  msg.set_name("Publisher Example");
  msg.set_msg("Hello World!");
  msg.set_id(msg_number);
  publisher_->Send(msg);
}

}  // namespace publisher
}  // namespace examples
}  // namespace trellis
