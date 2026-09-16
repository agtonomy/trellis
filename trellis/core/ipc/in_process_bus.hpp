/*
 * Copyright (C) 2026 Agtonomy
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

#ifndef TRELLIS_CORE_IPC_IN_PROCESS_BUS_HPP_
#define TRELLIS_CORE_IPC_IN_PROCESS_BUS_HPP_

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "trellis/core/event_loop.hpp"
#include "trellis/core/ipc/shm/shm_file.hpp"

namespace trellis::core::ipc {

/**
 * @brief Singleton topic bus that routes messages between publishers and subscribers in the same process.
 *
 * Carries messages between a publisher and a subscriber that share this address space, in place of shared memory.
 * Routes are keyed by topic, so a message reaches every subscriber on that topic. The bus never interprets the payload
 * -- it forwards the bytes and a synthesized header to the subscriber's usual delivery path.
 *
 * @note Delivery is posted onto the subscriber's loop, never called inline, matching the shm path's async wake. This
 * keeps handling order deterministic and avoids re-entering a callback from inside Send.
 */
class InProcessBus {
 public:
  /**
   * @brief Signature of SubscriberImpl::ReceiveData.
   *
   * @param header The header synthesized by the publisher.
   * @param data Pointer to the serialized message.
   * @param size Size of the data in bytes.
   */
  using ReceiveFn = std::function<void(const shm::ShmFile::SMemFileHeader&, const void*, size_t)>;

  /// The subscription handle that is used for bookkeeping active subscriptions.
  using Handle = int;

  /// Sentinel for "no active subscription". Never returned by Subscribe().
  static constexpr Handle kNoHandle = -1;

  /// A live count of this process's subscribers on one topic. Shared rather than returned by value so a publisher can
  /// hold it and read it on its send path without taking a lock; see GetRouteCount().
  using RouteCount = std::shared_ptr<const std::atomic<size_t>>;

  /**
   * @brief Get a handle to the process-wide bus, creating it if nobody holds one.
   *
   * @note Shared rather than a bare reference so lifetime does not depend on static destruction order: subscribers
   * unsubscribe from their destructors, and one owned by a static Node would otherwise find the bus already gone.
   * Hold the handle for as long as you use the bus; the last handle dropped destroys it.
   *
   * @return Shared handle to the process-wide bus.
   */
  [[nodiscard]] static std::shared_ptr<InProcessBus> Instance();

  /**
   * @brief Register a subscriber's delivery callback for a topic.
   *
   * @param topic The topic to receive messages for.
   * @param loop The loop to post deliveries onto.
   * @param fn The callback to invoke per message.
   * @return An opaque handle, for Unsubscribe().
   */
  [[nodiscard]] Handle Subscribe(const std::string& topic, trellis::core::EventLoop loop, ReceiveFn fn);

  /**
   * @brief Remove a route. Does nothing if it is already gone.
   *
   * @param topic The topic the route was registered for.
   * @param handle The handle returned by Subscribe().
   */
  void Unsubscribe(const std::string& topic, Handle handle);

  /**
   * @brief Deliver a serialized message to every subscriber on the topic.
   *
   * @param topic The topic to publish to.
   * @param header The header to deliver alongside the payload.
   * @param payload The serialized message, kept alive until every delivery completes.
   */
  void Publish(const std::string& topic, const shm::ShmFile::SMemFileHeader& header,
               std::shared_ptr<const std::vector<uint8_t>> payload);

  /**
   * @brief Observe how many subscribers this process currently has on a topic.
   *
   * Lets a publisher decide whether to serialize a payload for the bus at all, without asking discovery whether a
   * same-process subscriber exists. That question has an exact local answer, and routing a send on the remote,
   * eventually-consistent one is unsound: the two directions of discovery are independent UDP flows that expire
   * independently, so a publisher can stop seeing a subscriber that is still perfectly reachable over the bus.
   *
   * The counter keeps the topic's entry alive, so the count a caller holds stays the count this bus updates.
   *
   * @param topic The topic to observe.
   * @return A counter tracking the live route count, readable for as long as the caller holds it.
   */
  [[nodiscard]] RouteCount GetRouteCount(const std::string& topic);

 private:
  InProcessBus() = default;

  /// @brief One subscriber's registration for a topic.
  struct Route {
    trellis::core::EventLoop loop;
    ReceiveFn fn;
  };

  /// Routes for one topic, keyed by handle. Node-based because `EventLoop` is copy-constructible but not
  /// move-assignable (const members), which erasing from a vector would need.
  using TopicRoutes = std::unordered_map<Handle, Route>;

  /**
   * @brief Everything the bus holds for one topic.
   *
   * The count and the route table are two representations of one fact, so routes are only ever added and removed
   * through the members below. A count left behind after a route changes does not misreport a statistic -- it makes
   * every publisher on the topic stop using the bus, silently and for good.
   */
  class TopicEntry {
   public:
    void AddRoute(Handle handle, Route route) {
      routes_.emplace(handle, std::move(route));
      route_count_->store(routes_.size(), std::memory_order_relaxed);
    }

    void RemoveRoute(Handle handle) {
      routes_.erase(handle);
      route_count_->store(routes_.size(), std::memory_order_relaxed);
    }

    const TopicRoutes& Routes() const { return routes_; }

    /// @return The counter, for a publisher to hold for as long as it lives.
    RouteCount Count() const { return route_count_; }

    /**
     * @return True when this entry can be dropped: no route remains and no publisher holds its counter.
     *
     * @note The use count is only ever raised under the bus mutex, by `GetRouteCount`, so reading it here can
     * understate nothing. A publisher releasing its counter concurrently can only make this return false when it
     * could have returned true, which costs an empty entry until the next unsubscribe, never a dropped counter.
     */
    bool Unreferenced() const { return routes_.empty() && route_count_.use_count() == 1; }

   private:
    TopicRoutes routes_;
    /// Separate and atomic so a publisher can read it without taking `mutex_`, and shared so the value it reads
    /// survives a period during which the topic has no routes at all.
    std::shared_ptr<std::atomic<size_t>> route_count_{std::make_shared<std::atomic<size_t>>(0)};
  };

  using RouteTable = std::unordered_map<std::string, TopicEntry>;

  std::mutex mutex_;  ///< Guards routes_ and next_handle_.
  RouteTable routes_;
  Handle next_handle_{0};
};

}  // namespace trellis::core::ipc

#endif  // TRELLIS_CORE_IPC_IN_PROCESS_BUS_HPP_
