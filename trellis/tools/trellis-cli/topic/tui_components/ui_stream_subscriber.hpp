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
#include <ftxui/dom/table.hpp>
#include <string>
#include <vector>

#include "trellis/core/node.hpp"
#include "trellis/core/subscriber.hpp"

namespace trellis {
namespace tools {
namespace cli {

using namespace trellis::core;

class UIStreamSubscriber {
 public:
  explicit UIStreamSubscriber(Node& node, const std::string& initial_topic, size_t number_of_display_rows,
                              const std::function<void()>& redraw_screen);

  /** Gets the tab-ordered collection of components so they can be added to a ftxui Renderer
   *
   * @return ordered container of components
   */
  std::vector<ftxui::Component> GetComponents();

  /** Since the table component cannot have its data refreshed, it must be regenerated whenever there are data changes.
   * The expectation is that this function is called from a UI thread, which is why accessing the members must be
   * protected with a mutex.
   *
   * @return a table containing the current data
   */
  ftxui::Table GenerateTable();

  /** Creates the layout of the various components. The intention is that this will be called in a ftxui render loop.
   *
   * @return a window element
   */
  ftxui::Element GenerateUIWindow();

  /** Gets an element that displays the current subscription stats.
   * The expectation is that this function will be called from the UI thread and that the topic and row count will
   * only be updated via the UI thread, which is why they don't need to be guarded. The subscription count will be
   * updated by a non-ui thread so it is an atomic.
   *
   * @return a text component that summarizes the current subscription state
   */
  ftxui::Element GetStats() const;

 private:
  Node& node_;

  /// subscription related fields
  DynamicSubscriber subscription_;
  std::mutex data_update_mutex_;
  std::vector<std::string> headers_;
  std::list<std::vector<std::string>> values_;
  size_t number_of_display_rows_;

  /// UI referenced controls
  bool is_subscribing_;
  std::atomic<size_t> subscription_count_;
  std::atomic<bool> header_needs_initialization_;
  std::string subscription_topic_;
  std::string number_of_display_rows_str_;
  float scroll_x_;
  float scroll_y_;

  /// UI components
  ftxui::Component subscription_topic_component_;
  ftxui::Component receiving_checkbox_component_;
  ftxui::Component display_row_count_component_;
  ftxui::Component scrollbar_y_;
  ftxui::Component scrollbar_x_;

  // this should only be called from the node thread
  void StopSubscription() const;

  // this should only be called from the node thread
  void UpdateSubscription(const std::string& sub_topic, const std::function<void()>& msg_received_complete);
};

}  // namespace cli
}  // namespace tools
}  // namespace trellis
