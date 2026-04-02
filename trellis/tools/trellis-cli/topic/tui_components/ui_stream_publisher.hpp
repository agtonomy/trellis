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

#include <ftxui/component/component.hpp>
#include <string>
#include <vector>

#include "trellis/core/node.hpp"

namespace trellis {
namespace tools {
namespace cli {

using namespace trellis::core;

class UIStreamPublisher {
 public:
  struct PublicationInfo {
    std::string body;
    std::string topic;
    std::string rate;
  };

  explicit UIStreamPublisher(Node& node, const PublicationInfo& init_pub_info,
                             const std::function<void()>& redraw_screen);

  ~UIStreamPublisher();

  /** Gets the tab-ordered collection of components so they can be added to a ftxui Renderer
   *
   * @return ordered container of components
   */
  std::vector<ftxui::Component> GetComponents() const;

  /** Creates the layout of the various components. The intention is that this will be called in a ftxui render loop.
   *
   * @return a window element
   */
  ftxui::Element GenerateUIWindow() const;

  /** Gets an element that displays the current publication stats.
   * The expectation is that this function will be called from the UI thread and that the topic and rate members will
   * only be updated via the UI thread, which is why they don't need to be guarded. The publication count will be
   * updated by a non-ui thread so it is an atomic.
   *
   * @return a text component that summarizes the current publication state
   */
  ftxui::Element GetStats() const;

 private:
  Node& node_;
  PublicationInfo publication_info_;
  const std::function<void()>& redraw_screen_;

  /// UI referenced controls
  bool is_paused_;
  std::string publication_error_msg_;
  bool has_changes_since_last_send_start_;
  std::atomic<size_t> publication_count_;

  /// UI components
  ftxui::Component topic_component_;
  ftxui::Component body_component_;
  ftxui::Component rate_component_;
  ftxui::Component update_send_button_;
  ftxui::Component pause_resume_send_button_;

  /// publication related fields
  PeriodicTimer timer_;
  DynamicPublisher pub_;
  std::shared_ptr<google::protobuf::Message> message_;
  std::shared_ptr<ipc::proto::DynamicMessageCache> cache_;  // the cache must exist for as long as the message

  static ftxui::ButtonOption StyleButton();

  void ClearPublisher();

  std::string UpdatePublisher();
};

}  // namespace cli
}  // namespace tools
}  // namespace trellis
