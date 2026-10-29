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

#include "trellis/core/ipc/in_process_bus.hpp"

#include <algorithm>
#include <asio.hpp>
#include <iterator>
#include <utility>

namespace trellis::core::ipc {

std::shared_ptr<InProcessBus> InProcessBus::Instance() {
  static std::mutex instance_mutex;
  static std::weak_ptr<InProcessBus> weak_instance;

  const std::lock_guard<std::mutex> lock(instance_mutex);
  std::shared_ptr<InProcessBus> instance = weak_instance.lock();
  if (instance == nullptr) {
    // make_shared cannot reach the private constructor.
    instance = std::shared_ptr<InProcessBus>(new InProcessBus());
    weak_instance = instance;
  }
  return instance;
}

uint64_t InProcessBus::NextWriterId() {
  static std::atomic<uint64_t> next_writer_id{1};
  return next_writer_id.fetch_add(1, std::memory_order_relaxed);
}

InProcessBus::Handle InProcessBus::Subscribe(const std::string& topic, trellis::core::EventLoop loop, ReceiveFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Handle handle = next_handle_++;
  routes_[topic].AddRoute(handle, std::make_shared<Subscription>(std::move(loop), std::move(fn)));
  return handle;
}

void InProcessBus::Unsubscribe(const std::string& topic, Handle handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  const RouteTable::iterator it = routes_.find(topic);
  if (it == routes_.end()) {
    return;
  }
  it->second.RemoveRoute(handle);
  // Erasing while a publisher still held the counter would leave it reading one this bus no longer updates, so that
  // publisher would never see a later subscriber.
  if (it->second.Unreferenced()) {
    routes_.erase(it);
  }
}

InProcessBus::RouteCount InProcessBus::GetRouteCount(const std::string& topic) {
  std::lock_guard<std::mutex> lock(mutex_);
  return routes_[topic].Count();
}

void InProcessBus::Publish(const std::string& topic, const Message& message, size_t depth) {
  depth = std::max<size_t>(depth, 1);
  const auto from_writer = [writer_id = message.header.writer_id](const Message& pending) {
    return pending.header.writer_id == writer_id;
  };
  std::lock_guard<std::mutex> lock(mutex_);
  const RouteTable::const_iterator it = routes_.find(topic);
  if (it == routes_.end()) {
    return;
  }
  // Each queued copy holds the payload, so it outlives this call.
  for (const TopicRoutes::value_type& entry : it->second.Routes()) {
    const Route& subscription = entry.second;
    bool post = false;
    {
      std::lock_guard<std::mutex> queue_lock(subscription->mutex);
      std::deque<Message>& pending = subscription->pending;
      pending.push_back(message);
      // The writer's oldest go: those are what a shared memory reader would have found overwritten. More than one only
      // after a throwing delivery put its batch's rest back.
      while (static_cast<size_t>(std::ranges::count_if(pending, from_writer)) > depth) {
        pending.erase(std::ranges::find_if(pending, from_writer));
      }
      post = !std::exchange(subscription->drain_posted, true);
    }
    if (post) {
      asio::post(*subscription->loop, [subscription]() { Drain(subscription); });
    }
  }
}

void InProcessBus::Drain(const std::shared_ptr<Subscription>& subscription) {
  std::deque<Message> batch;
  {
    std::lock_guard<std::mutex> lock(subscription->mutex);
    batch.swap(subscription->pending);
  }
  // Runs even when a delivery throws, so the queue is never left waiting on a drain that won't come.
  const auto finish = [&subscription]() {
    {
      std::lock_guard<std::mutex> lock(subscription->mutex);
      if (subscription->pending.empty()) {
        subscription->drain_posted = false;
        return;
      }
    }
    asio::post(*subscription->loop, [subscription]() { Drain(subscription); });
  };
  auto next = batch.begin();
  try {
    // A borrow is move-only, so each delivery builds its own.
    for (; next != batch.end(); ++next) {
      subscription->fn(next->header, BorrowedPayload::Immutable({next->payload, next->payload->data()},
                                                                next->payload->size(), next->header.sequence));
    }
  } catch (...) {
    // Those behind the one that threw go back ahead of anything newer, for the next drain, as their handlers stayed
    // queued when each message had its own.
    {
      std::lock_guard<std::mutex> lock(subscription->mutex);
      subscription->pending.insert(subscription->pending.begin(), std::make_move_iterator(std::next(next)),
                                   std::make_move_iterator(batch.end()));
    }
    finish();
    throw;
  }
  finish();
}

}  // namespace trellis::core::ipc
