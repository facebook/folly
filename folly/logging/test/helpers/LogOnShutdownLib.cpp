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

#include <folly/logging/test/helpers/LogOnShutdownLib.h>

#include <folly/logging/test/helpers/helpers.h>

void use_log_on_shutdown() {
  // This function doesn't do anything.
  // It's only purpose is to make sure main() uses a symbol from this file,
  // forcing it to be linked into the program when building statically.
}

static LogOnDestruction log_on_shutdown("log_on_shutdown");
