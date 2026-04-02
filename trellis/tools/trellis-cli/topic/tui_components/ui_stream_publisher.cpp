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

#include "trellis/tools/trellis-cli/topic/tui_components/ui_stream_publisher.hpp"

#include <google/protobuf/util/json_util.h>

#include <charconv>                   //  from_chars
#include <ftxui/component/event.hpp>  //  for Event

#include "trellis/core/discovery/discovery.hpp"

namespace trellis {
namespace tools {
namespace cli {
using namespace trellis::core;

UIStreamPublisher::UIStreamPublisher(Node& node, const PublicationInfo& init_pub_info,
                                     const std::function<void()>& redraw_screen)
    : node_{node},
      publication_info_{init_pub_info},
      redraw_screen_{redraw_screen},
      is_paused_{false},
      has_changes_since_last_send_start_{false},
      topic_component_{
          ftxui::Input(&publication_info_.topic, "publication topic", ftxui::InputOption{.multiline = false})},
      body_component_{Input(&publication_info_.body, "publication body", ftxui::InputOption{.multiline = false})},
      rate_component_{Input(&publication_info_.rate, "", ftxui::InputOption{.multiline = false})},
      update_send_button_{ftxui::Button(
          "Update Sending",
          [this] {
            asio::post(*node_.GetEventLoop(), [this]() {
              if (has_changes_since_last_send_start_) {
                publication_error_msg_ = UpdatePublisher();
                has_changes_since_last_send_start_ = false;
              } else {
                publication_error_msg_ = "No changes since last update";
              }
            });
          },
          StyleButton())},
      pause_resume_send_button_{ftxui::Button(
          "Stop/Start Sending",
          [this] {
            asio::post(*node_.GetEventLoop(), [this]() {
              if (is_paused_) {
                publication_error_msg_ = UpdatePublisher();
                is_paused_ = false;
              } else {
                ClearPublisher();
                is_paused_ = true;
              }
            });
          },
          StyleButton())} {
  if (!publication_info_.body.empty() && !publication_info_.topic.empty()) {
    publication_error_msg_ = UpdatePublisher();
  }

  // the expectation is that the UI components are constructed here but their activity will be processed on a dedicated
  // UI thread. Updates to the data representations underlying the UI are posted back to the node thread.

  topic_component_ |= ftxui::CatchEvent([&](const ftxui::Event& event) {
    if (event == ftxui::Event::Delete || event == ftxui::Event::Backspace || event.is_character()) {
      has_changes_since_last_send_start_ = true;
    }
    return false;
  });

  body_component_ |= ftxui::CatchEvent([&](const ftxui::Event& event) {
    if (event == ftxui::Event::Delete || event == ftxui::Event::Backspace || event.is_character()) {
      has_changes_since_last_send_start_ = true;
    }
    return false;
  });

  rate_component_ |= ftxui::CatchEvent([&](const ftxui::Event& event) {  // ensures only numbers are allowed here
    if (event == ftxui::Event::Delete || event == ftxui::Event::Backspace || event.is_character()) {
      has_changes_since_last_send_start_ = true;
    }
    return event.is_character() && !std::isdigit(event.character()[0]);
  });
}

UIStreamPublisher::~UIStreamPublisher() { ClearPublisher(); }

std::vector<ftxui::Component> UIStreamPublisher::GetComponents() const {
  return {topic_component_, body_component_, rate_component_, update_send_button_, pause_resume_send_button_};
}

ftxui::Element UIStreamPublisher::GenerateUIWindow() const {
  return ftxui::window(
             ftxui::text("publication") | ftxui::hcenter | ftxui::bold,
             ftxui::vbox(
                 {hbox(ftxui::text("topic : "), topic_component_->Render()),
                  ftxui::hbox(ftxui::text("body : "), body_component_->Render()),
                  ftxui::hbox(ftxui::text("rate (hz) : "), rate_component_->Render()), ftxui::separator(),
                  publication_error_msg_.empty() ? ftxui::hbox() : ftxui::hbox(ftxui::text(publication_error_msg_)),
                  hbox(update_send_button_->Render(), ftxui::separator(), pause_resume_send_button_->Render())}) |
                 ftxui::dim) |
         ftxui::flex;
}

ftxui::Element UIStreamPublisher::GetStats() const {
  return ftxui::text(fmt::format("Published {} messages on topic \"{}\" at {}hz", publication_count_.load(),
                                 publication_info_.topic, publication_info_.rate));
}

ftxui::ButtonOption UIStreamPublisher::StyleButton() {
  auto option = ftxui::ButtonOption::Animated();
  option.transform = [](const ftxui::EntryState& s) {
    auto element = ftxui::text(s.label);
    if (s.focused) {
      element |= ftxui::bold;
    }
    return element | ftxui::center | ftxui::borderEmpty | ftxui::flex;
  };
  return option;
};

void UIStreamPublisher::ClearPublisher() {
  timer_.reset();
  pub_.reset();
  message_.reset();
  cache_.reset();
}

std::string UIStreamPublisher::UpdatePublisher() {
  unsigned rate_ms{};
  if (std::from_chars(publication_info_.rate.data(), publication_info_.rate.data() + publication_info_.rate.size(),
                      rate_ms)
              .ec != std::errc() ||
      rate_ms == 0) {
    return fmt::format("Rate of {} is not a number", publication_info_.rate);
  }

  // note : the above check is for both parsability and if the resulting rate value is 0, avoiding divide by zero
  const unsigned publication_interval_ms{static_cast<unsigned>(std::ceil(1000.0 / rate_ms))};  // no zero interval

  ClearPublisher();
  pub_ = node_.CreateDynamicPublisher(publication_info_.topic);

  const auto pubsub_samples = node_.GetDiscovery()->GetPubSubSamples();

  const auto it = std::find_if(pubsub_samples.begin(), pubsub_samples.end(),
                               [&topic = publication_info_.topic](const auto& sample) {
                                 // Find the first pub/sub msg that matches our topic of interest, but also has
                                 // non-empty metadata. Dynamic subscribers will not have the `tdatatype` metadata
                                 // populated, so we want to skip those samples
                                 return sample.topic().tname() == topic && !sample.topic().tdatatype().name().empty() &&
                                        !sample.topic().tdatatype().desc().empty();
                               });

  if (it == pubsub_samples.end()) {
    return fmt::format("Failed to discover topic {}", publication_info_.topic);
  }

  cache_ = std::make_shared<ipc::proto::DynamicMessageCache>(it->topic().tdatatype().desc());
  message_ = cache_->Create(it->topic().tdatatype().name());

  google::protobuf::util::JsonParseOptions json_options;
  json_options.ignore_unknown_fields = false;
  json_options.case_insensitive_enum_parsing = false;

  auto r = google::protobuf::util::JsonStringToMessage(publication_info_.body, message_.get(), json_options);
  if (!r.ok()) {
    return fmt::format("Could not convert JSON string to message (status = {})", r.message());
  }

  publication_count_ = 0;

  timer_ = node_.CreatePeriodicTimer(publication_interval_ms, [this](const time::TimePoint&) {
    // the captured variables are all shared_ptrs so their lifetime exists for the
    // duration of the timer
    pub_->Send(*message_);
    ++publication_count_;
    redraw_screen_();
  });
  return "";
}
}  // namespace cli
}  // namespace tools
}  // namespace trellis
