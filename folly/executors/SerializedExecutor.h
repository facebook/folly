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

#include <folly/executors/SequencedExecutor.h>

namespace folly {

// SerializedExecutor is a SequencedExecutor with the additional guarantee that
// no tasks added to the executor are run concurrently, even if their add()s
// were not sequenced.
class SerializedExecutor : public SequencedExecutor {
 public:
  virtual ~SerializedExecutor() override {}
};

} // namespace folly
