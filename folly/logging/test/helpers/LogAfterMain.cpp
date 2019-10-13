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

#include <folly/init/Init.h>
#include <folly/logging/test/helpers/LogOnShutdownLib.h>
#include <folly/logging/test/helpers/helpers.h>
#include <folly/logging/xlog.h>

// Logging after main() returns is safe, but there isn't any guarantee the
// messages will actually be visible: order of destruction is undefined, and the
// logging handlers may have already been flushed and cleaned up by the time the
// log messages are processed.
LogOnDestruction d1("1");
LogOnDestruction d2("2");

int main(int argc, char* argv[]) {
  folly::Init init(&argc, &argv);
  XLOG(INFO) << "main running";
  use_log_on_shutdown();
  return 0;
}
