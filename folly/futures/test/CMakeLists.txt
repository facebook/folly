# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Test definitions for folly/futures/test/

folly_define_tests(
  DIRECTORY futures/test/
      TEST futures_barrier_test SOURCES BarrierTest.cpp
      TEST futures_callback_lifetime_test SOURCES CallbackLifetimeTest.cpp
      TEST futures_collect_test SOURCES CollectTest.cpp
      TEST futures_context_test SOURCES ContextTest.cpp
      TEST futures_core_test SOURCES CoreTest.cpp
      TEST futures_ensure_test SOURCES EnsureTest.cpp
      TEST futures_filter_test SOURCES FilterTest.cpp
      TEST futures_future_splitter_test SOURCES FutureSplitterTest.cpp
      BENCHMARK futures_benchmark WINDOWS_DISABLED
        SOURCES Benchmark.cpp
      TEST futures_future_test WINDOWS_DISABLED
        SOURCES FutureTest.cpp
      TEST futures_header_compile_test SOURCES HeaderCompileTest.cpp
      TEST futures_interrupt_test SOURCES InterruptTest.cpp
      TEST futures_map_test SOURCES MapTest.cpp
      TEST futures_non_copyable_lambda_test SOURCES NonCopyableLambdaTest.cpp
      TEST futures_poll_test SOURCES PollTest.cpp
      TEST futures_promise_test SOURCES PromiseTest.cpp
      TEST futures_reduce_test SOURCES ReduceTest.cpp
      TEST futures_retrying_test SOURCES RetryingTest.cpp
      TEST futures_self_destruct_test SOURCES SelfDestructTest.cpp
      TEST futures_shared_promise_test SOURCES SharedPromiseTest.cpp
      TEST futures_test_executor_test SOURCES TestExecutorTest.cpp
      TEST futures_then_compile_test
        HEADERS
          ThenCompileTest.h
        SOURCES
          ThenCompileTest.cpp
      TEST futures_then_test SOURCES ThenTest.cpp
      TEST futures_timekeeper_test WINDOWS_DISABLED SOURCES TimekeeperTest.cpp
      TEST futures_times_test SOURCES TimesTest.cpp
      TEST futures_unwrap_test SOURCES UnwrapTest.cpp
      TEST futures_via_test SOURCES ViaTest.cpp
      TEST futures_wait_test SOURCES WaitTest.cpp
      TEST futures_when_test SOURCES WhenTest.cpp
      TEST futures_while_do_test SOURCES WhileDoTest.cpp
      TEST futures_will_equal_test SOURCES WillEqualTest.cpp
      TEST futures_window_test WINDOWS_DISABLED
        SOURCES WindowTest.cpp
)
