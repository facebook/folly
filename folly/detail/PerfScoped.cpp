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

#include <folly/detail/PerfScoped.h>

#include <folly/Conv.h>

#if FOLLY_PERF_IS_SUPPORTED
#include <folly/Subprocess.h> // @manual
#include <folly/system/Pid.h>
#include <folly/testing/TestUtil.h>
#endif

#include <filesystem>
#include <stdexcept>
#include <thread>

#include <boost/regex.hpp>

namespace folly {
namespace detail {

#if FOLLY_PERF_IS_SUPPORTED

namespace {

constexpr std::chrono::milliseconds kTerminateTimeout{500};

std::vector<std::string> prependCommonArgs(
    const std::vector<std::string>& passed, const test::TemporaryFile* output) {
  std::vector<std::string> res{std::string(kPerfBinaryPath)};
  res.insert(res.end(), passed.begin(), passed.end());

  res.emplace_back("-p");
  res.push_back(folly::to<std::string>(get_cached_pid()));
  if (output) {
    res.emplace_back("--output");
    res.push_back(output->path().string());
  }
  return res;
}

Subprocess::Options subprocessOptions() {
  Subprocess::Options res;
  res.terminateChildOnDestruction(kTerminateTimeout);
  return res;
}

} // namespace

class PerfScoped::PerfScopedImpl {
 public:
  PerfScopedImpl(const std::vector<std::string>& args, std::string* output)
      : proc_(
            prependCommonArgs(args, output != nullptr ? &outputFile_ : nullptr),
            subprocessOptions()),
        output_(output) {}

  PerfScopedImpl(const PerfScopedImpl&) = delete;
  PerfScopedImpl(PerfScopedImpl&&) = delete;
  PerfScopedImpl& operator=(const PerfScopedImpl&) = delete;
  PerfScopedImpl& operator=(PerfScopedImpl&&) = delete;

  ~PerfScopedImpl() noexcept {
    waitUntilAttached();

    proc_.sendSignal(SIGINT);
    proc_.wait();

    if (output_) {
      readFile(outputFile_.fd(), *output_);
    }
  }

 private:
  void waitUntilAttached() {
    const boost::regex regex{R"(anon_inode:\[perf_event(:\w+)?\])"};
    const auto slashproc = std::filesystem::path("/proc");
    const auto fddir = slashproc / folly::to<std::string>(proc_.pid()) / "fd";
    while (true) {
      for (const auto& entry : std::filesystem::directory_iterator(fddir)) {
        std::error_code ec;
        const auto target = std::filesystem::read_symlink(entry.path(), ec);
        if (boost::regex_match(target.string(), regex)) {
          return;
        }
      }
      std::this_thread::yield();
    }
  }

  test::TemporaryFile outputFile_;
  Subprocess proc_;
  std::string* output_;
};

PerfScoped::PerfScoped(
    const std::vector<std::string>& args, std::string* output)
    : pimpl_(std::make_unique<PerfScopedImpl>(args, output)) {}

#else // FOLLY_PERF_IS_SUPPORTED

class PerfScoped::PerfScopedImpl {};

[[noreturn]] PerfScoped::PerfScoped(
    const std::vector<std::string>& args, std::string* output) {
  (void)args;
  (void)output;
  throw std::runtime_error("Perf is not supported on Windows.");
}

#endif

PerfScoped::PerfScoped() = default;
PerfScoped::PerfScoped(PerfScoped&&) noexcept = default;
PerfScoped& PerfScoped::operator=(PerfScoped&&) noexcept = default;
PerfScoped::~PerfScoped() noexcept = default;

} // namespace detail
} // namespace folly
