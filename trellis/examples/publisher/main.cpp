#include "trellis/examples/publisher/app.hpp"

using namespace trellis::examples;

int main(int argc, char* argv[]) {
  trellis::core::Node node("publisher_example", trellis::core::Config("trellis/examples/config.yml"));
  publisher::App publisher(node);
  return node.Run();
}
