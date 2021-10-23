#include "app.hpp"

namespace trellis {
namespace examples {
namespace subscriber {

using namespace trellis::core;

App::App(const Node& node, const Config& config)
    : inputs_{node,
              {{config["examples"]["publisher"]["topic"].as<std::string>()}},
              [this](const trellis::examples::proto::HelloWorld& msg, const time::TimePoint&) { NewMessage(msg); }} {}

void App::NewMessage(const trellis::examples::proto::HelloWorld& msg) {
  Log::Info("Received message from {} with content {} and message number {}", msg.name(), msg.msg(), msg.id());
}

}  // namespace subscriber
}  // namespace examples
}  // namespace trellis
