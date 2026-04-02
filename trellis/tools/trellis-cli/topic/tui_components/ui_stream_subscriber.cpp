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

#include "trellis/tools/trellis-cli/topic/tui_components/ui_stream_subscriber.hpp"

#include <fmt/chrono.h>  // Required for chrono formatting
#include <google/protobuf/descriptor.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

#include <charconv>                   //from_chars
#include <ftxui/component/event.hpp>  // for Event
#include <ranges>                     // views::drop

namespace trellis {
namespace tools {
namespace cli {

UIStreamSubscriber::UIStreamSubscriber(Node& node, const std::string& initial_topic, size_t number_of_display_rows,
                                       const std::function<void()>& redraw_screen)
    : node_{node},
      number_of_display_rows_{number_of_display_rows},
      is_subscribing_{!initial_topic.empty()},
      subscription_topic_{initial_topic},
      number_of_display_rows_str_{fmt::format("{}", number_of_display_rows_)},
      scroll_x_{0.1},
      scroll_y_{0.1},
      subscription_topic_component_{
          Input(&subscription_topic_, "subscription topic", ftxui::InputOption{.multiline = false})},
      receiving_checkbox_component_{
          ftxui::Checkbox("Receiving", &is_subscribing_,
                          ftxui::CheckboxOption{.on_change =
                                                    [this, &redraw_screen]() {
                                                      asio::post(*node_.GetEventLoop(), [this, &redraw_screen]() {
                                                        if (is_subscribing_) {
                                                          UpdateSubscription(subscription_topic_, redraw_screen);
                                                        } else {
                                                          StopSubscription();
                                                          redraw_screen();
                                                        }
                                                      });
                                                    }})},
      display_row_count_component_{Input(&number_of_display_rows_str_, ftxui::InputOption{.multiline = false})},
      scrollbar_y_{ftxui::Slider(ftxui::SliderOption<float>{.value = &scroll_y_,
                                                            .min = 0.f,
                                                            .max = 1.f,
                                                            .increment = 0.1f,
                                                            .direction = ftxui::Direction::Down,
                                                            .color_active = ftxui::Color::Yellow,
                                                            .color_inactive = ftxui::Color::YellowLight})},
      scrollbar_x_{ftxui::Slider(ftxui::SliderOption<float>{.value = &scroll_x_,
                                                            .min = 0.f,
                                                            .max = 1.f,
                                                            .increment = 0.1f,
                                                            .direction = ftxui::Direction::Right,
                                                            .color_active = ftxui::Color::Blue,
                                                            .color_inactive = ftxui::Color::BlueLight})} {
  if (!initial_topic.empty()) {
    UpdateSubscription(initial_topic, redraw_screen);
  }

  display_row_count_component_ |= ftxui::CatchEvent([this](const ftxui::Event& event) {
    if (event.is_character() && !std::isdigit(event.character()[0])) {
      return true;  // this event has invalid chars so it has been handled and requires no further processing
    }

    std::lock_guard<std::mutex> guard{data_update_mutex_};
    std::from_chars(number_of_display_rows_str_.data(),
                    number_of_display_rows_str_.data() + number_of_display_rows_str_.size(), number_of_display_rows_);
    return false;  // event's data is valid and further processing is allowed
  });
  ;

  if (is_subscribing_) {
    asio::post(*node_.GetEventLoop(),
               [this, &redraw_screen]() { UpdateSubscription(subscription_topic_, redraw_screen); });
  }

  subscription_topic_component_ |= ftxui::CatchEvent([&](const ftxui::Event& event) {
    if (is_subscribing_ &&
        (event == ftxui::Event::Delete || event == ftxui::Event::Backspace || event.is_character())) {
      // when user changes topic, stop receiving messages. User must restart sub after change is complete.
      is_subscribing_ = false;
      asio::post(*node.GetEventLoop(), [this, &redraw_screen]() {
        StopSubscription();
        redraw_screen();
      });
    }
    return false;
  });
}

std::vector<ftxui::Component> UIStreamSubscriber::GetComponents() {
  return {subscription_topic_component_, receiving_checkbox_component_, display_row_count_component_, scrollbar_y_,
          scrollbar_x_};
}

ftxui::Table UIStreamSubscriber::GenerateTable() {
  std::vector<std::vector<std::string>> data;
  if (header_needs_initialization_) {
    data.push_back({"Waiting for first message..."});
  } else {
    std::lock_guard<std::mutex> guard{data_update_mutex_};
    data.push_back(headers_);
    for (const auto& row : values_) {
      data.push_back(row);
    }
  }

  ftxui::Table table(data);

  table.SelectAll().Border(ftxui::LIGHT);

  if (!data.empty() && !data[0].empty()) {
    // Make header row bold with a double border
    table.SelectRow(0).Decorate(ftxui::bold);
    table.SelectRow(0).SeparatorVertical(ftxui::LIGHT);
    table.SelectRow(0).Border(ftxui::DOUBLE);

    // "Timestamp" column is aligned right with a border
    table.SelectColumn(0).DecorateCells(ftxui::align_right);
    table.SelectColumn(0).Border(ftxui::LIGHT);

    // non-header rows should alternate color for easier reading
    auto content = table.SelectRows(1, -1);
    content.DecorateCellsAlternateRow(ftxui::color(ftxui::Color::Blue), 3, 0);
    content.DecorateCellsAlternateRow(ftxui::color(ftxui::Color::White), 3, 2);
  }

  return table;
}

ftxui::Element UIStreamSubscriber::GenerateUIWindow() {
  return ftxui::window(
             ftxui::text("subscription") | ftxui::hcenter | ftxui::bold,
             ftxui::vbox({
                 ftxui::hbox({ftxui::text("topic : "), subscription_topic_component_->Render(), ftxui::separator(),
                              receiving_checkbox_component_->Render(), ftxui::separator(),
                              ftxui::text("number of rows displayed : "), display_row_count_component_->Render()}) |
                     ftxui::dim,
                 ftxui::vbox(ftxui::hbox({
                                 GenerateTable().Render() | ftxui::focusPositionRelative(scroll_x_, scroll_y_) |
                                     ftxui::frame | ftxui::flex,
                                 scrollbar_y_->Render(),
                             }) |
                             ftxui::flex),
                 scrollbar_x_->Render(),
             })) |
         ftxui::flex;
}

ftxui::Element UIStreamSubscriber::GetStats() const {
  return ftxui::text(fmt::format("Received {} messages on topic \"{}\" displaying {} rows", subscription_count_.load(),
                                 subscription_topic_, number_of_display_rows_));
}

void UIStreamSubscriber::StopSubscription() const {
  if (subscription_) subscription_->Stop();
}

void UIStreamSubscriber::UpdateSubscription(const std::string& sub_topic,
                                            const std::function<void()>& msg_received_complete) {
  if (sub_topic.empty()) {
    StopSubscription();
    return;
  }

  subscription_count_ = 0;
  header_needs_initialization_ = true;

  {
    std::lock_guard<std::mutex> guard{data_update_mutex_};
    // remove stale data because the new topic may not publish in a timely way and old data should not linger
    headers_.clear();
    values_.clear();
  }

  subscription_ = node_.CreateDynamicSubscriber(
      sub_topic, [this, &msg_received_complete](
                     const trellis::core::time::TimePoint& now, const trellis::core::time::TimePoint& msgtime,
                     trellis::core::SubscriberImpl<google::protobuf::Message>::MsgTypePtr msg) {
        ++subscription_count_;
        const google::protobuf::Descriptor* desc = msg->GetDescriptor();
        const int field_count = desc->field_count();

        if (header_needs_initialization_) {
          std::vector<std::string> msg_headers;
          msg_headers.reserve(field_count + 1);  // we add the timestamp to the normal field count
          msg_headers.emplace_back("Timestamp");

          for (int i = 0; i < field_count; i++) {
            const google::protobuf::FieldDescriptor* field = desc->field(i);
            msg_headers.push_back(field->name());
          }
          {
            std::lock_guard<std::mutex> guard{data_update_mutex_};
            headers_ = msg_headers;
          }
          header_needs_initialization_ = false;
        }

        std::vector<std::string> msg_values;
        msg_values.reserve(field_count + 1);  // we add the timestamp to the normal field count
        msg_values.push_back(fmt::format("{}", time::TimePointToSystemTime(msgtime)));

        std::string value_str;
        // note : no need to lock the mutex before this since we are just reading headers_ and the only place it can
        //        be updated is in the preceding codeblock.
        for (const auto& header_name : headers_ | std::views::drop(1)) {  // skip timestamp, it was added previously
          const google::protobuf::FieldDescriptor* field = desc->FindFieldByName(header_name);

          if (field == nullptr) {
            msg_values.push_back("");
            continue;
          }

          google::protobuf::TextFormat::PrintFieldValueToString(*msg, field, -1, &value_str);
          msg_values.push_back(value_str);
        }

        {
          std::lock_guard<std::mutex> guard{data_update_mutex_};
          values_.push_back(std::move(msg_values));

          while (values_.size() > number_of_display_rows_) {
            // generally pops just one but if the number of display rows has been decreased then will pop more
            values_.pop_front();
          }
        }

        msg_received_complete();
      });
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
