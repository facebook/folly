/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/experimental/pushmi/properties.h>
#include <folly/experimental/pushmi/receiver/concepts.h>
#include <folly/experimental/pushmi/sender/tags.h>
#include <folly/experimental/pushmi/sender/properties.h>
#include <folly/experimental/pushmi/executor/properties.h>

#include <folly/Executor.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

namespace folly {
namespace pushmi {

class pool;

class pool_executor {
  struct task;
  Executor::KeepAlive<CPUThreadPoolExecutor> exec_ {};

public:
  using properties = property_set<is_concurrent_sequence<>>;

  pool_executor() = default;
  explicit pool_executor(pool &e);
  task schedule();
};

struct pool_executor::task
: single_sender_tag::with_values<pool_executor&>::no_error {
  using properties = property_set<is_never_blocking<>>;

  explicit task(pool_executor e)
    : pool_ex_(std::move(e))
  {}

  PUSHMI_TEMPLATE(class Out)
    (requires ReceiveValue<Out, pool_executor&>)
  void submit(Out out) && {
    pool_ex_.exec_->add([e = pool_ex_, out = std::move(out)]() mutable {
      set_value(out, e);
      set_done(out);
    });
  }
private:
  pool_executor pool_ex_;
};

class pool {
  friend pool_executor;
  CPUThreadPoolExecutor pool_;

public:
  explicit pool(std::size_t threads) : pool_(threads) {}

  auto executor() {
    return pool_executor{*this};
  }

  void stop() {
    pool_.stop();
  }

  void wait() {
    pool_.join();
  }
};

inline pool_executor::pool_executor(pool &e)
: exec_(Executor::getKeepAliveToken(e.pool_))
{}

inline pool_executor::task pool_executor::schedule() {
  return task{*this};
}

} // namespace pushmi
} // namespace folly
