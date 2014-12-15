/*
 * Copyright 2014 Facebook, Inc.
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

namespace folly { namespace wangle {

class IOExecutor;

// Retrieve the global IOExecutor. If there is none, a default
// IOThreadPoolExecutor will be constructed and returned.
IOExecutor* getIOExecutor();

// Set an IOExecutor to be the global IOExecutor which will be returned by
// subsequent calls to getIOExecutor(). IOExecutors will uninstall themselves
// as global when they are destructed.
void setIOExecutor(IOExecutor* executor);

}}
