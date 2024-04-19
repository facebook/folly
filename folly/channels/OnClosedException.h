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

#pragma once

#include <exception>

namespace folly {
namespace channels {

/**
 * An OnClosedException passed to a transform or multiplex callback indicates
 * that the input channel was closed. An OnClosedException can also be thrown by
 * a transform or multiplex callback, which will close the output channel.
 */
struct OnClosedException : public std::exception {
  const char* what() const noexcept override {
    return "The channel has been closed.";
  }
};
} // namespace channels
} // namespace folly
