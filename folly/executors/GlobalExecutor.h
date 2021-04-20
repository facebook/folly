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

#include <memory>

#include <folly/Executor.h>
#include <folly/executors/IOExecutor.h>

namespace folly {

namespace detail {
std::shared_ptr<Executor> tryGetImmutableCPUPtr();
} // namespace detail

/**
 * Return the global executor.
 * The global executor is a CPU thread pool and is immutable.
 *
 * May return an invalid KeepAlive on shutdown.
 */
folly::Executor::KeepAlive<> getGlobalCPUExecutor();

/**
 * Return the global IO executor.
 * The global executor is an IO thread pool and is immutable.
 *
 * May return an invalid KeepAlive on shutdown.
 */
folly::Executor::KeepAlive<IOExecutor> getGlobalIOExecutor();

/**
 * Retrieve the global Executor. If there is none, a default InlineExecutor
 * will be constructed and returned. This is named CPUExecutor to distinguish
 * it from IOExecutor below and to hint that it's intended for CPU-bound tasks.
 *
 * Can return nullptr on shutdown.
 */
[[deprecated(
    "getCPUExecutor is deprecated. "
    "To use the global mutable executor use getUnsafeMutableGlobalCPUExecutor. "
    "For a better solution use getGlobalCPUExecutor.")]] std::
    shared_ptr<folly::Executor>
    getCPUExecutor();
std::shared_ptr<folly::Executor> getUnsafeMutableGlobalCPUExecutor();

/**
 * Set an Executor to be the global Executor which will be returned by
 * subsequent calls to getCPUExecutor().
 */
[[deprecated(
    "setCPUExecutor is deprecated. "
    "To use the global mutable executor use setUnsafeMutableGlobalCPUExecutor. "
    "For a better solution use getGlobalCPUExecutor and avoid calling set.")]] void
setCPUExecutor(std::weak_ptr<folly::Executor> executor);
void setUnsafeMutableGlobalCPUExecutor(std::weak_ptr<folly::Executor> executor);

/**
 * Set the CPU executor to the immutable default returned by
 * getGlobalCPUExecutor.
 */
[[deprecated(
    "setCPUExecutorToGlobalCPUExecutor is deprecated. "
    "Switch to setUnsafeMutableGlobalCPUExecutorToGlobalCPUExecutor. ")]] void
setCPUExecutorToGlobalCPUExecutor();
void setUnsafeMutableGlobalCPUExecutorToGlobalCPUExecutor();

/**
 * Retrieve the global IOExecutor. If there is none, a default
 * IOThreadPoolExecutor will be constructed and returned.
 *
 * IOExecutors differ from Executors in that they drive and provide access to
 * one or more EventBases.
 *
 * Can return nullptr on shutdown.
 */
[[deprecated(
    "getIOExecutor is deprecated. "
    "To use the global mutable executor use getUnsafeMutableGlobalIOExecutor. "
    "For a better solution use getGlobalIOExecutor.")]] std::
    shared_ptr<IOExecutor>
    getIOExecutor();
std::shared_ptr<IOExecutor> getUnsafeMutableGlobalIOExecutor();

/**
 * Set an IOExecutor to be the global IOExecutor which will be returned by
 * subsequent calls to getIOExecutor().
 */
[[deprecated(
    "setIOExecutor is deprecated. "
    "To use the global mutable executor use setUnsafeMutableGlobalIOExecutor. "
    "For a better solution use getGlobalIOExecutor and avoid calling set.")]] void
setIOExecutor(std::weak_ptr<IOExecutor> executor);
void setUnsafeMutableGlobalIOExecutor(std::weak_ptr<IOExecutor> executor);

/**
 * Retrieve an event base from the global IOExecutor
 *
 * NOTE: This is not shutdown-safe, the returned pointer may be
 * invalid during shutdown.
 */
[[deprecated(
    "getEventBase is deprecated. "
    "To use the global mutable executor use getUnsafeMutableGlobalEventBase. "
    "For a better solution use getGlobalIOExecutor and request the EventBase "
    "from there.")]] folly::EventBase*
getEventBase();
folly::EventBase* getUnsafeMutableGlobalEventBase();

} // namespace folly
