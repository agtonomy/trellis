#include "app.hpp"

namespace trellis {
namespace examples {
namespace subscriber {

using namespace trellis::core;

App::App(const Node& node, const Config& config)
    : inputs_{node, {{{config["examples"]["publisher"]["topic"].as<std::string>()}}}, [this]() { Tick(); }} {}

void App::Tick() {
  const auto& msg = inputs_.Newest<trellis::examples::proto::HelloWorld>().message;
  Log::Warn("Received message from {} with content {} and message number {}", msg.name(), msg.msg(), msg.id());
}

}  // namespace subscriber
}  // namespace examples
}  // namespace trellis
