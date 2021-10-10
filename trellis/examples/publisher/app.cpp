#include "app.hpp"

namespace trellis {
namespace examples {
namespace publisher {

App::App(const trellis::core::Node& node, const trellis::core::Config& config)
    : publisher_{node.CreatePublisher<trellis::examples::HelloWorld>(
          config["examples"]["publisher"]["topic"].as<std::string>())},
      timer_{node.CreateTimer(config["examples"]["publisher"]["interval_ms"].as<unsigned>(),
                              [this]() { SendMessageTimer(); })} {}

void App::SendMessageTimer() {
  trellis::examples::HelloWorld msg;
  msg.set_name("Publisher Example");
  msg.set_msg("Hello World!");
  msg.set_id(++send_count_);
  publisher_->Send(msg);
}

}  // namespace publisher
}  // namespace examples
}  // namespace trellis
