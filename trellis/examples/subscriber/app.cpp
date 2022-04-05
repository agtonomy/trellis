#include "app.hpp"

namespace trellis {
namespace examples {
namespace subscriber {

using namespace trellis::core;

App::App(Node& node, const Config& config)
    : inputs_{node,
              {{config["examples"]["publisher"]["topic"].as<std::string>()}},
              [this](const std::string& topic, const trellis::examples::proto::HelloWorld& msg,
                     const time::TimePoint&) { NewMessage(topic, msg); },
              {{2000U}},
              {{[](const std::string&) { Log::Warn("Watchdog tripped on inbound messages!"); }}}} {}

void App::NewMessage(const std::string& topic, const trellis::examples::proto::HelloWorld& msg) {
  Log::Info("Received message on topic {} from {} with content {} and message number {}", topic, msg.name(), msg.msg(),
            msg.id());
}

}  // namespace subscriber
}  // namespace examples
}  // namespace trellis
