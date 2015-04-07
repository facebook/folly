/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/Executor.h>
#include <folly/wangle/concurrent/IOExecutor.h>

namespace folly { namespace wangle {

// Retrieve the global Executor. If there is none, a default InlineExecutor
// will be constructed and returned. This is named CPUExecutor to distinguish
// it from IOExecutor below and to hint that it's intended for CPU-bound tasks.
std::shared_ptr<Executor> getCPUExecutor();

// Set an Executor to be the global Executor which will be returned by
// subsequent calls to getCPUExecutor(). Takes a non-owning (weak) reference.
void setCPUExecutor(std::shared_ptr<Executor> executor);

// Retrieve the global IOExecutor. If there is none, a default
// IOThreadPoolExecutor will be constructed and returned.
//
// IOExecutors differ from Executors in that they drive and provide access to
// one or more EventBases.
std::shared_ptr<IOExecutor> getIOExecutor();

// Set an IOExecutor to be the global IOExecutor which will be returned by
// subsequent calls to getIOExecutor(). Takes a non-owning (weak) reference.
void setIOExecutor(std::shared_ptr<IOExecutor> executor);

}}
