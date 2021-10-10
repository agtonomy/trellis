#include "app.hpp"

using namespace trellis::examples;

int main(int argc, char* argv[]) {
  trellis::core::Node node("service_client_example");
  auto config = trellis::core::LoadFromFile("trellis/examples/config.yml");
  service_client::App service_client(node, config);
  return node.Run();
}
