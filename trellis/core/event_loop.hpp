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

#ifndef TRELLIS_CORE_EVENT_LOOP_HPP_
#define TRELLIS_CORE_EVENT_LOOP_HPP_

#include <asio.hpp>
#include <memory>

namespace trellis {
namespace core {

class TimerRegistry;

/**
 * @brief A proxy class for asio::io_context
 *
 * The motivation for this class is to add a little bit more statefulness on top of the asio::io_context. The loop will
 * default to the stopped state, and transition to the running state if any of the Run methods are called. The loop will
 * stay in the running state indefinitely or until Stop() is explicitly called.
 *
 * The loop also carries the timer registry that every timer constructed against it registers with, so a component that
 * only ever receives an event loop still gets its timers tracked. A default constructed loop carries no registry, which
 * means its timers still fire but go untracked.
 *
 * Notes on thread-safety: The various Run() methods should only be called on the same thread as one another. The Stop()
 * method may be called from other threads.
 */
class EventLoop {
 public:
  enum class State { kStopped = 0, kRunning };
  using IOContext = asio::io_context;
  using IOContextPointer = std::shared_ptr<IOContext>;

  EventLoop() = default;

  /**
   * @brief Construct a loop that tracks its timers in the given registry
   *
   * @param registry the registry each timer constructed against this loop adds itself to; null means untracked
   */
  explicit EventLoop(std::shared_ptr<TimerRegistry> registry) : registry_{std::move(registry)} {}

  /**
   * @brief Proxy for asio::io_context::run()
   */
  void Run() {
    state_->store(State::kRunning);
    io_context_->run();

    // To avoid a potential race condition with `Stop()`, we'll update state_ here again
    if (io_context_->stopped()) {
      state_->store(State::kStopped);
    }
  }

  /**
   * @brief Proxy for asio::io_context::run_one()
   */
  void RunOne() {
    state_->store(State::kRunning);
    io_context_->run_one();

    // To avoid a potential race condition with `Stop()`, we'll update state_ here again
    if (io_context_->stopped()) {
      state_->store(State::kStopped);
    }
  }

  /**
   * @brief Proxy for asio::io_context::run_for()
   */
  template <typename Rep, typename Period>
  std::size_t RunFor(const std::chrono::duration<Rep, Period>& rel_time) {
    state_->store(State::kRunning);
    const auto retval = io_context_->run_for(std::forward<const std::chrono::duration<Rep, Period>>(rel_time));

    // To avoid a potential race condition with `Stop()`, we'll update state_ here again
    if (io_context_->stopped()) {
      state_->store(State::kStopped);
    }
    return retval;
  }

  /**
   * @brief Proxy for asio::io_context::poll_one()
   */
  asio::io_context::count_type PollOne() {
    state_->store(State::kRunning);
    return io_context_->poll_one();
  }

  /**
   * @brief Proxy for asio::io_context::stop()
   */
  void Stop() {
    io_context_->stop();
    // Updating `state_` after the stop call to mitigate race conditions with run calls
    state_->store(State::kStopped);
  }

  /**
   * @brief Check if the event loop is in the stopped state
   *
   * Note: The call to asio::io_context::stopped() is intentionally unused here
   *
   * @return true if the event loop is in the stopped state, false otherwise
   */
  bool Stopped() const { return state_->load() == State::kStopped; }

  /**
   * @brief Proxy for asio::io_context::restart()
   */
  void Restart() { io_context_->restart(); }

  /**
   * @brief Return a reference to the underlying io_context
   *
   * This overload exists to pass the underlying asio::io_context into asio APIs such as asio::post(). This should not
   * be used to access methods on the io_context directly.
   *
   * @return IOContext& the underlying io context object
   */
  IOContext& operator*() const { return *io_context_; }

  /**
   * @brief Return the registry that timers on this loop register themselves with
   *
   * Returned by value rather than by reference because loops are commonly obtained as a temporary (see
   * Node::GetEventLoop()), and a reference into one of those would dangle.
   *
   * @return the registry, or nullptr if timers on this loop are untracked
   */
  std::shared_ptr<TimerRegistry> GetTimerRegistry() const { return registry_; }

 private:
  std::shared_ptr<std::atomic<State>> state_ = std::make_shared<std::atomic<State>>(State::kStopped);
  IOContextPointer io_context_ = std::make_shared<IOContext>();
  asio::executor_work_guard<typename IOContext::executor_type> work_guard_{asio::make_work_guard(*io_context_)};
  // Shared across copies of this loop, just like the members above
  std::shared_ptr<TimerRegistry> registry_{nullptr};
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_EVENT_LOOP_HPP_
