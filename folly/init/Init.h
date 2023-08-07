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
#include <bitset>

#include <folly/Portability.h>

namespace folly {
class InitOptions {
 public:
  InitOptions() noexcept;

  bool remove_flags{true};
  bool use_gflags{true};
  // Controls whether folly::symbolizer::installFatalSignalCallbacks() is called
  // during init.
  bool install_fatal_signal_callbacks{true};

  // mask of all fatal (default handler of terminating the process) signals for
  // which `init()` will install handler that print stack traces and invokes
  // previously established handler  (or terminate if there were none).
  // Signals that are not in `symbolizer::kAllFatalSignals` will be ignored
  // if passed here
  // Defaults to all signal in `symbolizer::kAllFatalSignals`
  std::bitset<64> fatal_signals;

  InitOptions& removeFlags(bool remove) {
    remove_flags = remove;
    return *this;
  }

  InitOptions& fatalSignals(unsigned long val) {
    fatal_signals = val;
    return *this;
  }

  InitOptions& useGFlags(bool useGFlags) {
    use_gflags = useGFlags;
    return *this;
  }

  InitOptions& installFatalSignalCallbacks(bool installFatalSignalCallbacks) {
    install_fatal_signal_callbacks = installFatalSignalCallbacks;
    return *this;
  }
};

/*
 * An RAII object to be constructed at the beginning of main() and destructed
 * implicitly at the end of main().
 *
 * The constructor calls common init functions in the necessary order
 * Among other things, this ensures that folly::Singletons are initialized
 * correctly and installs signal handlers for a superior debugging experience.
 * It also initializes gflags and glog.
 *
 * The destructor destroys all singletons managed by folly::Singleton, yielding
 * better shutdown behavior when performed at the end of main(). In particular,
 * this guarantees that all singletons managed by folly::Singleton are destroyed
 * before all Meyers singletons are destroyed.
 *
 * @param argc, argv   arguments to your main
 * @param removeFlags  if true, will update argc,argv to remove recognized
 *                     gflags passed on the command line
 * @param options      options
 */
class FOLLY_NODISCARD Init {
 public:
  // Force ctor & dtor out of line for better stack traces even with LTO.
  FOLLY_NOINLINE Init(int* argc, char*** argv, bool removeFlags = true);

  FOLLY_NOINLINE Init(int* argc, char*** argv, InitOptions options);

  FOLLY_NOINLINE ~Init();

  Init(Init const&) = delete;
  Init(Init&&) = delete;
  Init& operator=(Init const&) = delete;
  Init& operator=(Init&&) = delete;
};

[[deprecated("Use the RAII version Init")]] void init(
    int* argc, char*** argv, bool removeFlags = true);
[[deprecated("Use the RAII version Init")]] void init(
    int* argc, char*** argv, InitOptions options);

} // namespace folly
