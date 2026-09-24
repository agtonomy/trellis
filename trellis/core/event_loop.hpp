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

#include "trellis/core/timer_options.hpp"

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
 * A loop additionally carries an opaque owner tag, recorded on each of its timers' registry entries. Copies of a loop
 * share one io_context and one registry, so when several components run on the same loop the tag is the only thing that
 * says whose timer is whose -- see WithOwner() and Node::GetTimerOverrunCount().
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
   * @brief Construct a loop that tracks its timers in the given registry and applies the given timer policy
   *
   * @param registry the registry each timer constructed against this loop adds itself to; null means untracked
   * @param options the policy applied to every timer constructed against this loop
   */
  explicit EventLoop(std::shared_ptr<TimerRegistry> registry, TimerOptions options = {})
      : registry_{std::move(registry)}, options_{options} {}

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

  /**
   * @brief Return the timer policy applying to timers constructed against this loop
   *
   * Returned by value for the same reason as the registry above.
   *
   * @return the timer policy, defaults if none was supplied when this loop was constructed
   */
  TimerOptions GetTimerOptions() const { return options_; }

  /**
   * @brief Return a handle to the same loop, tagging the timers built against it with a different owner
   *
   * The result drives the same io_context, tracks timers in the same registry and applies the same timer policy; only
   * the tag differs. This is how two components sharing one loop keep their timers apart in the registry they also
   * share, which per-component metrics need and loop identity cannot provide.
   *
   * @param owner an opaque tag, only ever compared for equality and never dereferenced, so an object's own address
   *        while it is still under construction is a valid choice
   *
   * @return a handle to this loop whose timers register under the given owner
   */
  EventLoop WithOwner(const void* owner) const { return EventLoop{*this, owner}; }

  /**
   * @brief Return the tag that timers constructed against this loop register under
   *
   * @return the owner tag, or nullptr for a loop that was never tagged
   */
  const void* GetOwner() const { return owner_; }

 private:
  /**
   * @brief Construct a handle to another loop's io_context, registry and policy under a different owner tag
   *
   * Private because retagging is the only reason to build a loop from another one; WithOwner() is the way in. The work
   * guard is rebuilt rather than copied so that this handle holds the io_context open in its own right.
   */
  EventLoop(const EventLoop& other, const void* owner)
      : state_{other.state_},
        io_context_{other.io_context_},
        work_guard_{asio::make_work_guard(*other.io_context_)},
        registry_{other.registry_},
        options_{other.options_},
        owner_{owner} {}

  std::shared_ptr<std::atomic<State>> state_ = std::make_shared<std::atomic<State>>(State::kStopped);
  IOContextPointer io_context_ = std::make_shared<IOContext>();
  asio::executor_work_guard<typename IOContext::executor_type> work_guard_{asio::make_work_guard(*io_context_)};
  // All three are set once at construction and never mutated, so plain copying is correct. Const costs nothing here:
  // work_guard_ already leaves this class copy-constructible but not assignable, which it has to be -- an
  // assignment would leave the guard holding the io_context the loop no longer points at.
  const std::shared_ptr<TimerRegistry> registry_{nullptr};
  const TimerOptions options_{};
  // Opaque and never dereferenced, so it may point at an object that is still being constructed
  const void* owner_{nullptr};
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_EVENT_LOOP_HPP_
