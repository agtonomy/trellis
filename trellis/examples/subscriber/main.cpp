#include "trellis/examples/subscriber/app.hpp"

using namespace trellis::examples;

int main(int argc, char* argv[]) {
  trellis::core::Node node("subscriber_example", trellis::core::Config("trellis/examples/config.yml"));
  subscriber::App subscriber(node);
  return node.Run();
}
