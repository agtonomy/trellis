#include "app.hpp"

using namespace trellis::examples;

int main(int argc, char* argv[]) {
  trellis::core::Node node("publisher_example");
  auto config = trellis::core::LoadFromFile("trellis/examples/config.yml");
  publisher::App publisher(node, config);
  return node.Run();
}
