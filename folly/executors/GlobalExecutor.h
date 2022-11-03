/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

/**
 * Routines for managing executors global to a process
 *
 * @file executors/GlobalExecutor.h
 */

#pragma once

#include <memory>

#include <folly/Executor.h>
#include <folly/executors/IOExecutor.h>
#include <folly/portability/GFlags.h>

FOLLY_GFLAGS_DECLARE_uint32(folly_global_cpu_executor_threads);
FOLLY_GFLAGS_DECLARE_uint32(folly_global_io_executor_threads);

namespace folly {

namespace detail {
std::shared_ptr<Executor> tryGetImmutableCPUPtr();
} // namespace detail

/**
 * @methodset Executors
 *
 * Retrieve the global immutable executor.
 * This executor is a CPU thread pool of appropriate machine core size.
 *
 * Use to run CPU workloads.
 *
 * @return       KeepAlive wrapped global immutable CPU executor. May throw on
 * shutdown. If no throw, returned KeepAlive is valid.
 */
folly::Executor::KeepAlive<> getGlobalCPUExecutor();

/**
 * @methodset Executors
 *
 * Retrieve the global immutable IO executor.
 * This executor is an IO thread pool of appropriate machine core size.
 *
 * Use to run IO workloads that require an event base.
 *
 * @return       KeepAlive wrapped global immutable IO executor. May throw
 * on shutdown. If no throw, returned KeepAlive is valid.
 */
folly::Executor::KeepAlive<IOExecutor> getGlobalIOExecutor();

/**
 * @methodset Deprecated
 *
 * To use the global mutable executor use getUnsafeMutableGlobalCPUExecutor.
 * For a better solution use getGlobalCPUExecutor.
 */
[[deprecated(
    "getCPUExecutor is deprecated. "
    "To use the global mutable executor use getUnsafeMutableGlobalCPUExecutor. "
    "For a better solution use getGlobalCPUExecutor.")]] std::
    shared_ptr<folly::Executor>
    getCPUExecutor();
/**
 * @methodset Executors
 *
 * Retrieve the global mutable Executor. If there is none, a default
 * InlineExecutor will be constructed and returned. This is named CPUExecutor to
 * distinguish it from IOExecutor below and to hint that it's intended for
 * CPU-bound tasks.
 *
 * For a better solution use getGlobalCPUExecutor.
 *
 * @return       Global mutable executor. Can return nullptr on shutdown.
 */
std::shared_ptr<folly::Executor> getUnsafeMutableGlobalCPUExecutor();

/**
 * @methodset Deprecated
 *
 * To use the global mutable executor use setUnsafeMutableGlobalCPUExecutor.
 * For a better solution use getGlobalCPUExecutor and avoid calling set.
 */
[[deprecated(
    "setCPUExecutor is deprecated. "
    "To use the global mutable executor use setUnsafeMutableGlobalCPUExecutor. "
    "For a better solution use getGlobalCPUExecutor and avoid calling set.")]] void
setCPUExecutor(std::weak_ptr<folly::Executor> executor);
/**
 * @methodset Executors
 *
 * Set an Executor to be the global mutable Executor which will be returned by
 * subsequent calls to getUnsafeMutableGlobalCPUExecutor()
 *
 * For a better solution use getGlobalCPUExecutor and avoid calling set.
 *
 * @param  executor       Executor to set
 */
void setUnsafeMutableGlobalCPUExecutor(std::weak_ptr<folly::Executor> executor);

/**
 * @methodset Deprecated
 *
 * Switch to setUnsafeMutableGlobalCPUExecutorToGlobalCPUExecutor.
 */
[[deprecated(
    "setCPUExecutorToGlobalCPUExecutor is deprecated. "
    "Switch to setUnsafeMutableGlobalCPUExecutorToGlobalCPUExecutor. ")]] void
setCPUExecutorToGlobalCPUExecutor();
/**
 * @methodset Executors
 *
 * Set the mutable Executor to be the immutable default returned by
 * getGlobalCPUExecutor()
 *
 */
void setUnsafeMutableGlobalCPUExecutorToGlobalCPUExecutor();

/**
 * @methodset Deprecated
 *
 * To use the global mutable executor use getUnsafeMutableGlobalIOExecutor.
 * For a better solution use getGlobalIOExecutor.
 */
[[deprecated(
    "getIOExecutor is deprecated. "
    "To use the global mutable executor use getUnsafeMutableGlobalIOExecutor. "
    "For a better solution use getGlobalIOExecutor.")]] std::
    shared_ptr<IOExecutor>
    getIOExecutor();
/**
 * @methodset Executors
 *
 * Retrieve the global mutable IO Executor. If there is none, a default
 * IOThreadPoolExecutor will be constructed and returned.
 *
 * For a better solution use getGlobalIOExecutor.
 *
 * @return       Global mutable IO executor
 */
std::shared_ptr<IOExecutor> getUnsafeMutableGlobalIOExecutor();

/**
 * @methodset Deprecated
 *
 * To use the global mutable executor use setUnsafeMutableGlobalIOExecutor.
 * For a better solution use getGlobalIOExecutor and avoid calling set.
 */
[[deprecated(
    "setIOExecutor is deprecated. "
    "To use the global mutable executor use setUnsafeMutableGlobalIOExecutor. "
    "For a better solution use getGlobalIOExecutor and avoid calling set.")]] void
setIOExecutor(std::weak_ptr<IOExecutor> executor);
/**
 * @methodset Executors
 *
 * Set an IO Executor to be the global IOExecutor which will be returned by
 * subsequent calls to getUnsafeMutableGlobalIOExecutor()
 *
 * For a better solution use getGlobalIOExecutor and avoid calling set.
 *
 * @param  executor       IO Executor to set
 */
void setUnsafeMutableGlobalIOExecutor(std::weak_ptr<IOExecutor> executor);

/**
 * @methodset Deprecated
 *
 * To use the global mutable executor use getUnsafeMutableGlobalEventBase.
 * For a better solution use getGlobalIOExecutor and request the EventBase from
 * there.
 *
 */
[[deprecated(
    "getEventBase is deprecated. "
    "To use the global mutable executor use getUnsafeMutableGlobalEventBase. "
    "For a better solution use getGlobalIOExecutor and request the EventBase "
    "from there.")]] folly::EventBase*
getEventBase();
/**
 * @methodset Executors
 *
 * Retrieve an event base from the global mutable IO Executor
 *
 * NOTE: This is not shutdown-safe, the returned pointer may be
 * invalid during shutdown.
 *
 * For a better solution use getGlobalIOExecutor and request the EventBase from
 * there.
 *
 * @return       Event base used by global mutable IO executor
 */
folly::EventBase* getUnsafeMutableGlobalEventBase();

} // namespace folly
