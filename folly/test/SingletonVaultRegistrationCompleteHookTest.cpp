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

#include <folly/Singleton.h>

#include <array>
#include <atomic>
#include <stdexcept>
#include <thread>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <folly/portability/GTest.h>
#include <folly/portability/Unistd.h>

namespace {

std::atomic<int> hookCalls{0};

void lockCheckingHook() {
  auto* vault = folly::SingletonVault::singleton();
  (void)vault->registeredSingletonCount();
  (void)vault->livingSingletonCount();
  hookCalls.fetch_add(1, std::memory_order_relaxed);
}

std::atomic<int> otherHookCalls{0};

void otherHook() {
  otherHookCalls.fetch_add(1, std::memory_order_relaxed);
}

[[noreturn]] void throwingHook() {
  hookCalls.fetch_add(1, std::memory_order_relaxed);
  throw std::runtime_error("test hook");
}

[[noreturn]] void exitFor(bool success) {
  _exit(success ? 0 : 1);
}

TEST(SingletonVaultRegistrationCompleteHookTest, NoHookIsInert) {
  EXPECT_EXIT(
      ([] {
        folly::SingletonVault::singleton()->registrationComplete();
        folly::SingletonVault::singleton()->registrationComplete();
        exitFor(true);
      }()),
      ::testing::ExitedWithCode(0),
      "");
}

TEST(
    SingletonVaultRegistrationCompleteHookTest,
    OnlyDefaultVaultInvokesAfterLocksAreReleased) {
  EXPECT_EXIT(
      ([] {
        hookCalls.store(0, std::memory_order_relaxed);
        if (!folly::detail::addSingletonVaultRegistrationCompleteHook(
                &lockCheckingHook)) {
          _exit(2);
        }
        folly::SingletonVault custom{folly::SingletonVault::Type::Relaxed};
        custom.registrationComplete();
        struct TaggedVault;
        folly::SingletonVault::singleton<TaggedVault>()->registrationComplete();
        if (hookCalls.load(std::memory_order_relaxed) != 0) {
          _exit(3);
        }
        folly::SingletonVault::singleton()->registrationComplete();
        exitFor(hookCalls.load(std::memory_order_relaxed) == 1);
      }()),
      ::testing::ExitedWithCode(0),
      "");
}

TEST(SingletonVaultRegistrationCompleteHookTest, RepeatsAndRacesInvokeOnce) {
  EXPECT_EXIT(
      ([] {
        hookCalls.store(0, std::memory_order_relaxed);
        std::atomic<bool> go{false};
        std::array<std::thread, 16> threads;
        for (std::size_t i = 0; i < threads.size(); ++i) {
          threads[i] = std::thread([&go, i] {
            while (!go.load(std::memory_order_acquire)) {
            }
            if (i % 2 == 0) {
              folly::detail::addSingletonVaultRegistrationCompleteHook(
                  &lockCheckingHook);
            } else {
              folly::SingletonVault::singleton()->registrationComplete();
            }
          });
        }
        go.store(true, std::memory_order_release);
        for (auto& thread : threads) {
          thread.join();
        }
        folly::SingletonVault::singleton()->registrationComplete();
        exitFor(hookCalls.load(std::memory_order_relaxed) == 1);
      }()),
      ::testing::ExitedWithCode(0),
      "");
}

TEST(SingletonVaultRegistrationCompleteHookTest, LateHookStillInvokesOnce) {
  EXPECT_EXIT(
      ([] {
        hookCalls.store(0, std::memory_order_relaxed);
        folly::SingletonVault::singleton()->registrationComplete();
        if (!folly::detail::addSingletonVaultRegistrationCompleteHook(
                &lockCheckingHook)) {
          _exit(2);
        }
        folly::SingletonVault::singleton()->registrationComplete();
        exitFor(hookCalls.load(std::memory_order_relaxed) == 1);
      }()),
      ::testing::ExitedWithCode(0),
      "");
}

TEST(SingletonVaultRegistrationCompleteHookTest, HookExceptionIsContained) {
  EXPECT_EXIT(
      ([] {
        hookCalls.store(0, std::memory_order_relaxed);
        if (!folly::detail::addSingletonVaultRegistrationCompleteHook(
                &throwingHook)) {
          _exit(2);
        }
        folly::SingletonVault::singleton()->registrationComplete();
        folly::SingletonVault::singleton()->registrationComplete();
        exitFor(hookCalls.load(std::memory_order_relaxed) == 1);
      }()),
      ::testing::ExitedWithCode(0),
      "");
}

TEST(SingletonVaultRegistrationCompleteHookTest, EveryHookRunsOnce) {
  EXPECT_EXIT(
      ([] {
        hookCalls.store(0, std::memory_order_relaxed);
        otherHookCalls.store(0, std::memory_order_relaxed);
        if (!folly::detail::addSingletonVaultRegistrationCompleteHook(
                &lockCheckingHook) ||
            !folly::detail::addSingletonVaultRegistrationCompleteHook(
                &otherHook) ||
            !folly::detail::addSingletonVaultRegistrationCompleteHook(
                &lockCheckingHook)) {
          _exit(2);
        }
        folly::SingletonVault::singleton()->registrationComplete();
        folly::SingletonVault::singleton()->registrationComplete();
        exitFor(
            hookCalls.load(std::memory_order_relaxed) == 1 &&
            otherHookCalls.load(std::memory_order_relaxed) == 1);
      }()),
      ::testing::ExitedWithCode(0),
      "");
}

#ifndef _WIN32
TEST(SingletonVaultRegistrationCompleteHookTest, ForkDoesNotInvokeAgain) {
  EXPECT_EXIT(
      ([] {
        hookCalls.store(0, std::memory_order_relaxed);
        folly::detail::addSingletonVaultRegistrationCompleteHook(
            &lockCheckingHook);
        folly::SingletonVault::singleton()->registrationComplete();
        const pid_t child = fork();
        if (child < 0) {
          _exit(2);
        }
        if (child == 0) {
          folly::SingletonVault::singleton()->registrationComplete();
          exitFor(hookCalls.load(std::memory_order_relaxed) == 1);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
          _exit(3);
        }
        exitFor(hookCalls.load(std::memory_order_relaxed) == 1);
      }()),
      ::testing::ExitedWithCode(0),
      "");
}

#endif

} // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
