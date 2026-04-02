/*
 * Copyright (C) 2021 Agtonomy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <cxxopts.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "trellis/core/node.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"
#include "trellis/tools/trellis-cli/topic/tui_components/ui_stream_publisher.hpp"
#include "trellis/tools/trellis-cli/topic/tui_components/ui_stream_subscriber.hpp"

namespace trellis {
namespace tools {
namespace cli {

using namespace trellis::core;

void RenderStreamUI(ftxui::ScreenInteractive& screen, UIStreamSubscriber& sub, UIStreamPublisher& pub) {
  std::vector<ftxui::Component> all_components{sub.GetComponents()};
  {
    auto publication_components = pub.GetComponents();
    all_components.insert(all_components.end(), publication_components.begin(), publication_components.end());
  }

  const auto renderer = ftxui::Renderer(ftxui::Container::Vertical(all_components), [&] {
    auto subscription_window = sub.GenerateUIWindow();
    auto publication_window = pub.GenerateUIWindow();

    return ftxui::vbox({
               subscription_window,
               publication_window,
               ftxui::separator(),
               pub.GetStats(),
               sub.GetStats(),
           }) |
           ftxui::border;
  });

  screen.Loop(renderer);
}

int topic_stream_main(int argc, char* argv[]) {
  cxxopts::Options options(topic_stream_command.data(), topic_stream_command_desc.data());
  options.add_options()("s,sub_topic",
                        "An optional subscription topic name. If not supplied, it can be added after startup.",
                        cxxopts::value<std::string>()->default_value(""));
  options.add_options()("p,pub_topic",
                        "An optional publication topic name. If not supplied, it can be added after startup.",
                        cxxopts::value<std::string>()->default_value(""));
  options.add_options()("b,body",
                        "An optional publication message body in JSON. If not supplied, it can be added after startup.",
                        cxxopts::value<std::string>()->default_value(""));
  options.add_options()("r,rate", "Publication rate hz.", cxxopts::value<size_t>()->default_value("1"));
  options.add_options()("d,display_rows", "The number of rows to display.",
                        cxxopts::value<size_t>()->default_value("6"));
  options.add_options()("h,help", "Print usage.");

  const auto result = options.parse(argc, argv);
  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  const UIStreamPublisher::PublicationInfo pub_info{.body = result["body"].as<std::string>(),
                                                    .topic = result["pub_topic"].as<std::string>(),
                                                    .rate = std::to_string(result["rate"].as<size_t>())};

  const std::string initial_subscription_topic{result["sub_topic"].as<std::string>()};

  auto screen = ftxui::ScreenInteractive::TerminalOutput();
  // screen.ForceHandleCtrlC(true);  // Node will handle ctrl c
  const std::function<void()> redraw_screen = [&screen]() { screen.PostEvent(ftxui::Event::Custom); };
  Node node(root_command.data(), {});

  // wait to ensure the discovery cache is populated if immediately sub or pub
  if (!initial_subscription_topic.empty() || (!pub_info.body.empty() && !pub_info.topic.empty())) {
    node.RunFor(std::chrono::milliseconds(monitor_delay_ms));  // ensure discovery is populated
  }

  {
    UIStreamSubscriber sub(node, initial_subscription_topic, result["display_rows"].as<size_t>(), redraw_screen);
    UIStreamPublisher pub(node, pub_info, redraw_screen);

    std::jthread ui_thread_{[&screen, &pub, &sub]() { RenderStreamUI(screen, sub, pub); }};

    node.Run();  // ctrl-c exits from the run loops

    screen.Post([&screen]() { screen.Exit(); });

    static constexpr std::chrono::milliseconds kExitResponseTimeMS{100};
    std::this_thread::sleep_for(kExitResponseTimeMS);
  }
  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
