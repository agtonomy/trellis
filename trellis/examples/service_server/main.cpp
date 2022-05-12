#include "app.hpp"

using namespace trellis::examples;

int main(int argc, char* argv[]) {
  trellis::core::Node node("service_server_example", trellis::core::LoadFromFile("trellis/examples/config.yml"));
  service_server::App service_server(node);
  return node.Run();
}
