#include "app.hpp"

namespace trellis {
namespace examples {
namespace subscriber {

App::App(trellis::core::Node& node)
    : node_{node},
      inputs_{node,
              {{node.GetConfig()["examples"]["publisher"]["topic"].as<std::string>()}},
              [this](const std::string& topic, const trellis::examples::proto::HelloWorld& msg,
                     const trellis::core::time::TimePoint& now,
                     const trellis::core::time::TimePoint& msgtime) { NewMessage(topic, msg, now, msgtime); },
              {{2000U}},
              {{[this](const std::string&, const trellis::core::time::TimePoint&) {
                trellis::core::Log::Warn("Watchdog tripped on inbound messages!");
                node_.UpdateHealth(trellis::core::HealthState::HEALTH_STATE_CRITICAL, 0x01, "Inputs timed out");
              }}}} {}

void App::NewMessage(const std::string& topic, const trellis::examples::proto::HelloWorld& msg,
                     const trellis::core::time::TimePoint& now, const trellis::core::time::TimePoint& msgtime) {
  node_.UpdateHealth(trellis::core::HealthState::HEALTH_STATE_NORMAL);
  trellis::core::Log::Info(
      "Received message on topic {} from {} with content {} and message number {} (now = {} msgtime = {})", topic,
      msg.name(), msg.msg(), msg.id(), trellis::core::time::TimePointToSeconds(now),
      trellis::core::time::TimePointToSeconds(msgtime));
}

}  // namespace subscriber
}  // namespace examples
}  // namespace trellis
