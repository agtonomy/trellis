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

#include "node.hpp"

#include <thread>

using namespace trellis::core;

Node::Node(std::string name) : name_{name}, ev_loop_{CreateEventLoop()} {
  // XXX(bsirang) eCAL can take argv/argc to parse options for overriding the config filepath and/or specific config
  // options. We won't make use of that for now. We'll just call Initialize with default arguments.
  eCAL::Initialize();

  // Instead of passing the unit name as part of Initialize above, we'll set it here explicitly. This ensures the unit
  // name gets set in cases where we may have called Initialize already, such as from logging APIs.
  eCAL::SetUnitName(name_.c_str());
}

Node::~Node() { Stop(); }

int Node::Run() {
  Log::Info("{} node running...", name_);
  auto word_guard = asio::make_work_guard(*ev_loop_);
  while (ShouldRun()) {
    ev_loop_->run_for(std::chrono::milliseconds(500));
    // If the event loop was stopped, run_for will return immediately, so
    // we should sleep in that case.
    if (ev_loop_->stopped()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }
  Stop();
  return 0;
}

bool Node::RunN(unsigned n) {
  unsigned count = 0;
  bool ok;
  while ((ok = ShouldRun()) && count++ < n) {
    ev_loop_->run_one();
  }
  return ok;
}

Timer Node::CreateTimer(unsigned interval_ms, TimerImpl::Callback callback, unsigned initial_delay_ms) const {
  return std::make_shared<TimerImpl>(GetEventLoop(), TimerImpl::Type::kPeriodic, callback, interval_ms,
                                     initial_delay_ms);
}

Timer Node::CreateOneShotTimer(unsigned initial_delay_ms, TimerImpl::Callback callback) const {
  return std::make_shared<TimerImpl>(GetEventLoop(), TimerImpl::Type::kOneShot, callback, 0, initial_delay_ms);
}

void Node::Stop() {
  should_run_ = false;
  ev_loop_->stop();
  eCAL::Finalize();
}

bool Node::ShouldRun() const { return should_run_ && eCAL::Ok(); }
