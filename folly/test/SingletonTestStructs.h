/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <memory>
#include <stdexcept>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

// A simple class that tracks how often instances of the class and
// subclasses are created, and the ordering.  Also tracks a global
// unique counter for each object.
std::atomic<size_t> global_counter(19770326);

struct Watchdog {
  static std::vector<Watchdog*>& creation_order() {
    static std::vector<Watchdog*> ret;
    return ret;
  }

  Watchdog() : serial_number(++global_counter) {
    creation_order().push_back(this);
  }

  ~Watchdog() {
    if (creation_order().back() != this) {
      throw std::out_of_range("Watchdog destruction order mismatch");
    }
    creation_order().pop_back();
  }

  const size_t serial_number;
  size_t livingWatchdogCount() const { return creation_order().size(); }

  Watchdog(const Watchdog&) = delete;
  Watchdog& operator=(const Watchdog&) = delete;
  Watchdog(Watchdog&&) noexcept = default;
  Watchdog& operator=(Watchdog&&) noexcept = default;
};

// Some basic types we use for tracking.
struct ChildWatchdog : public Watchdog {};
struct GlobalWatchdog : public Watchdog {};
struct UnregisteredWatchdog : public Watchdog {};
