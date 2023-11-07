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

#include <folly/init/Init.h>

#include <cstdlib>

#include <glog/logging.h>

#include <folly/Singleton.h>
#include <folly/init/Phase.h>
#include <folly/logging/Init.h>
#include <folly/portability/Config.h>
#include <folly/synchronization/HazptrThreadPoolExecutor.h>

#if !defined(_WIN32)
#include <folly/experimental/symbolizer/SignalHandler.h> // @manual
#endif
#include <folly/portability/GFlags.h>

FOLLY_GFLAGS_DEFINE_string(logging, "", "Logging configuration");

namespace folly {
const unsigned long kAllFatalSignals =
#if !defined(_WIN32)
    symbolizer::kAllFatalSignals;
#else
    0;
#endif

InitOptions::InitOptions() noexcept : fatal_signals(kAllFatalSignals) {}

namespace {

#if FOLLY_USE_SYMBOLIZER
// Newer versions of glog require the function passed to InstallFailureFunction
// to be noreturn. But glog spells that in multiple possible ways, depending on
// platform. But glog's choice of spelling does not match how the
// noreturn-ability of std::abort is spelled. Some compilers consider this a
// type mismatch on the function-ptr type. To fix the type mismatch, we wrap
// std::abort and mimic the condition and the spellings from glog here.
#if defined(__GNUC__)
__attribute__((noreturn))
#else
[[noreturn]]
#endif
static void
wrapped_abort() {
  abort();
}
#endif

void initImpl(int* argc, char*** argv, InitOptions options) {
#if !defined(_WIN32)
  // Install the handler now, to trap errors received during startup.
  // The callbacks, if any, can be installed later
  folly::symbolizer::installFatalSignalHandler(options.fatal_signals);
#endif

  // Indicate ProcessPhase::Regular and register handler to
  // indicate ProcessPhase::Exit.
  folly::set_process_phases();

  // Move from the registration phase to the "you can actually instantiate
  // things now" phase.
  folly::SingletonVault::singleton()->registrationComplete();

#if !FOLLY_HAVE_LIBGFLAGS
  (void)options;
#else
  if (options.use_gflags) {
    gflags::ParseCommandLineFlags(argc, argv, options.remove_flags);
  }
#endif

  auto const follyLoggingEnv = std::getenv(kLoggingEnvVarName);
  auto const follyLoggingEnvOr = follyLoggingEnv ? follyLoggingEnv : "";
  folly::initLoggingOrDie({follyLoggingEnvOr, FLAGS_logging});
  auto programName = argc && argv && *argc > 0 ? (*argv)[0] : "unknown";
  google::InitGoogleLogging(programName);

#if FOLLY_USE_SYMBOLIZER
  // Don't use glog's DumpStackTraceAndExit; rely on our signal handler.
  google::InstallFailureFunction(wrapped_abort);

  if (options.install_fatal_signal_callbacks) {
    // Actually install the callbacks into the handler.
    folly::symbolizer::installFatalSignalCallbacks();
  }
#endif
  // Set the default hazard pointer domain to use a thread pool executor
  // for asynchronous reclamation
  folly::enable_hazptr_thread_pool_executor();
}

} // namespace

Init::Init(int* argc, char*** argv, bool removeFlags) {
  initImpl(argc, argv, InitOptions{}.removeFlags(removeFlags));
}

Init::Init(int* argc, char*** argv, InitOptions options) {
  initImpl(argc, argv, options);
}

Init::~Init() {
  SingletonVault::singleton()->destroyInstancesFinal();
}

void init(int* argc, char*** argv, bool removeFlags) {
  initImpl(argc, argv, InitOptions{}.removeFlags(removeFlags));
}

void init(int* argc, char*** argv, InitOptions options) {
  initImpl(argc, argv, options);
}

} // namespace folly
