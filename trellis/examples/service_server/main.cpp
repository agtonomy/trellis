#include "app.hpp"

using namespace trellis::examples;

int main(int argc, char* argv[]) {
  trellis::core::Node node("service_server_example");
  auto config = trellis::core::LoadFromFile("trellis/examples/config.yml");
  service_server::App service_server(node, config);
  return node.Run();
}
