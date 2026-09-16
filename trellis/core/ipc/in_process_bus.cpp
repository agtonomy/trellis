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

#include <asio.hpp>
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

InProcessBus::Handle InProcessBus::Subscribe(const std::string& topic, trellis::core::EventLoop loop, ReceiveFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Handle handle = next_handle_++;
  routes_[topic].AddRoute(handle, Route{std::move(loop), std::move(fn)});
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

void InProcessBus::Publish(const std::string& topic, const shm::ShmFile::SMemFileHeader& header,
                           std::shared_ptr<const std::vector<uint8_t>> payload) {
  std::lock_guard<std::mutex> lock(mutex_);
  const RouteTable::const_iterator it = routes_.find(topic);
  if (it == routes_.end()) {
    return;
  }
  // Each handler copies the shared_ptr, so the payload outlives this call.
  for (const TopicRoutes::value_type& entry : it->second.Routes()) {
    const Route& route = entry.second;
    asio::post(*route.loop, [fn = route.fn, header, payload]() { fn(header, payload->data(), payload->size()); });
  }
}

}  // namespace trellis::core::ipc
