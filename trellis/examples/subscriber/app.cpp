#include "app.hpp"

namespace trellis {
namespace examples {
namespace subscriber {

App::App(trellis::core::Node& node)
    : node_{node},
      inputs_{node,
              {{node.GetConfig()["examples"]["publisher"]["topic"].as<std::string>()}},
              [this](const std::string& topic, const trellis::examples::proto::HelloWorld& msg,
                     const trellis::core::time::TimePoint&) { NewMessage(topic, msg); },
              {{2000U}},
              {{[this](const std::string&, const trellis::core::time::TimePoint&) {
                trellis::core::Log::Warn("Watchdog tripped on inbound messages!");
                node_.UpdateHealth(trellis::core::HealthState::HEALTH_STATE_CRITICAL, 0x01, "Inputs timed out");
              }}}} {}

void App::NewMessage(const std::string& topic, const trellis::examples::proto::HelloWorld& msg) {
  node_.UpdateHealth(trellis::core::HealthState::HEALTH_STATE_NORMAL);
  trellis::core::Log::Info("Received message on topic {} from {} with content {} and message number {}", topic,
                           msg.name(), msg.msg(), msg.id());
}

}  // namespace subscriber
}  // namespace examples
}  // namespace trellis
