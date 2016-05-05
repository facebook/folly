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

#include <folly/init/Init.h>

#include <glog/logging.h>

#include <folly/Singleton.h>
#include <folly/experimental/symbolizer/SignalHandler.h>
#include <folly/portability/GFlags.h>

namespace folly {

void init(int* argc, char*** argv, bool removeFlags) {
  // Install the handler now, to trap errors received during startup.
  // The callbacks, if any, can be installed later
  folly::symbolizer::installFatalSignalHandler();

  google::ParseCommandLineFlags(argc, argv, removeFlags);

  auto programName = argc && argv && *argc > 0 ? (*argv)[0] : "unknown";
  google::InitGoogleLogging(programName);

  // Don't use glog's DumpStackTraceAndExit; rely on our signal handler.
  google::InstallFailureFunction(abort);

  // Move from the registration phase to the "you can actually instantiate
  // things now" phase.
  folly::SingletonVault::singleton()->registrationComplete();

  // Actually install the callbacks into the handler.
  folly::symbolizer::installFatalSignalCallbacks();
}
} //!folly
