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
#include <folly/io/async/test/EventBaseTestLib.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace test {

REGISTER_TYPED_TEST_SUITE_P(
    EventBaseTest,
    ReadEvent,
    ReadPersist,
    ReadImmediate,
    WriteEvent,
    WritePersist,
    WriteImmediate,
    ReadWrite,
    WriteRead,
    ReadWriteSimultaneous,
    ReadWritePersist,
    ReadPartial,
    WritePartial,
    DestroyingHandler,
    RunAfterDelay,
    RunAfterDelayDestruction,
    BasicTimeouts,
    ReuseTimeout,
    RescheduleTimeout,
    CancelTimeout,
    DestroyingTimeout,
    ScheduledFn,
    ScheduledFnAt,
    RunInThread,
    RunInEventBaseThreadAndWait,
    RunImmediatelyOrRunInEventBaseThreadAndWaitCross,
    RunImmediatelyOrRunInEventBaseThreadAndWaitWithin,
    RunImmediatelyOrRunInEventBaseThreadAndWaitNotLooping,
    RunImmediatelyOrRunInEventBaseThreadCross,
    RunImmediatelyOrRunInEventBaseThreadNotLooping,
    RepeatedRunInLoop,
    RunInLoopNoTimeMeasurement,
    RunInLoopStopLoop,
    RunPoolLoop,
    messageAvailableException,
    TryRunningAfterTerminate,
    CancelRunInLoop,
    LoopTermination,
    CallbackOrderTest,
    AlwaysEnqueueCallbackOrderTest,
    IdleTime,
    MaxLatencyUndamped,
    UnsetMaxLatencyUndamped,
    ThisLoop,
    EventBaseThreadLoop,
    EventBaseThreadName,
    RunBeforeLoop,
    RunBeforeLoopWait,
    StopBeforeLoop,
    RunCallbacksOnDestruction,
    LoopKeepAlive,
    LoopKeepAliveInLoop,
    LoopKeepAliveWithLoopForever,
    LoopKeepAliveShutdown,
    LoopKeepAliveAtomic,
    LoopKeepAliveCast);

REGISTER_TYPED_TEST_SUITE_P(
    EventBaseTest1,
    DrivableExecutorTest,
    IOExecutorTest,
    RequestContextTest,
    CancelLoopCallbackRequestContextTest,
    TestStarvation,
    RunOnDestructionBasic,
    RunOnDestructionCancelled,
    RunOnDestructionAfterHandleDestroyed,
    RunOnDestructionAddCallbackWithinCallback,
    InternalExternalCallbackOrderTest,
    pidCheck,
    EventBaseExecutionObserver);

struct DefaultBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    return folly::EventBase::getDefaultBackend();
  }
};
INSTANTIATE_TYPED_TEST_SUITE_P(
    EventBaseTest, EventBaseTest, DefaultBackendProvider);
INSTANTIATE_TYPED_TEST_SUITE_P(
    EventBaseTest1, EventBaseTest1, DefaultBackendProvider);
} // namespace test
} // namespace folly
