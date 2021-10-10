#include "app.hpp"

namespace trellis {
namespace examples {
namespace publisher {

using namespace trellis::core;

App::App(const Node& node, const Config& config)
    : publisher_{node.CreatePublisher<trellis::examples::HelloWorld>(
          config["examples"]["publisher"]["topic"].as<std::string>())},
      timer_{node.CreateTimer(config["examples"]["publisher"]["interval_ms"].as<unsigned>(),
                              [this]() { Tick(); })} {}

void App::Tick() {
  Log::Info("Publishing message number {}", send_count_);

  trellis::examples::HelloWorld msg;
  msg.set_name("Publisher Example");
  msg.set_msg("Hello World!");
  msg.set_id(++send_count_);
  publisher_->Send(msg);
}

}  // namespace publisher
}  // namespace examples
}  // namespace trellis
