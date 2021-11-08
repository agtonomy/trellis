#include <sstream>
#include <thread>

#include "trellis/core/logging.hpp"

int main(int argc, char* argv[]) {
  unsigned count{0};
  while (true) {
    std::stringstream msg;
    msg << "Logging message " << count;

    // You can use the logging APIs directly without any other explicit trellis initialization. Depending on the
    // underlying eCAL settings, the log messages will go to various backends, including multicast UDP.

    // Pick and choose different log levels at different iteration counts for demonstration
    if (count % 10 == 0) {
      trellis::core::Log::Error(msg.str());
    } else if (count % 3 == 0) {
      trellis::core::Log::Warn(msg.str());
    } else {
      trellis::core::Log::Info(msg.str());
    }

    ++count;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  return 0;
}
