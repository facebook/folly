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

# Test definitions for folly/io/async/test/

folly_define_tests(
  DIRECTORY io/async/test/
      # A number of tests in the async_test binary are unfortunately flaky.
      # When run under Travis CI a number of the tests also hang (it looks
      # like they do not get expected socket accept events, causing them
      # to never break out of their event loops).
      TEST io_async_async_test BROKEN
        CONTENT_DIR certs/
        HEADERS
          AsyncSocketTest.h
          AsyncSSLSocketTest.h
        SOURCES
          AsyncPipeTest.cpp
          AsyncSocketExceptionTest.cpp
          AsyncSocketTest.cpp
          AsyncSocketTest2.cpp
          AsyncSSLSocketTest.cpp
          AsyncSSLSocketTest2.cpp
          AsyncSSLSocketWriteTest.cpp
          AsyncTransportTest.cpp
      # This is disabled because it depends on things that don't exist
      # on Windows.
      # TODO: Refactor EventHandlerTest to not use eventfd so it can work on Mac OS X.
      #TEST io_async_event_handler_test WINDOWS_DISABLED SOURCES EventHandlerTest.cpp
      TEST io_async_async_timeout_test SOURCES AsyncTimeoutTest.cpp
      TEST io_async_async_udp_socket_test APPLE_DISABLED WINDOWS_DISABLED
        SOURCES AsyncUDPSocketTest.cpp
      TEST io_async_delayed_destruction_test SOURCES DelayedDestructionTest.cpp
      TEST io_async_delayed_destruction_base_test
        SOURCES DelayedDestructionBaseTest.cpp
      TEST io_async_destructor_check_test SOURCES DestructorCheckTest.cpp
      # Fails with gtest macro error
      BENCHMARK io_async_event_base_benchmark SOURCES EventBaseBenchmark.cpp
      TEST io_async_event_base_test BROKEN SOURCES EventBaseTest.cpp
      TEST io_async_event_base_local_test WINDOWS_DISABLED
        SOURCES EventBaseLocalTest.cpp
      TEST io_async_hh_wheel_timer_test SOURCES HHWheelTimerTest.cpp
      TEST io_async_hh_wheel_timer_slow_tests SLOW
        SOURCES HHWheelTimerSlowTests.cpp
      TEST io_async_notification_queue_test WINDOWS_DISABLED
        SOURCES NotificationQueueTest.cpp
      BENCHMARK io_async_request_context_benchmark WINDOWS_DISABLED
        SOURCES RequestContextBenchmark.cpp
        HEADERS RequestContextHelper.h
      TEST io_async_request_context_test WINDOWS_DISABLED
        SOURCES RequestContextTest.cpp
      TEST io_async_scoped_event_base_thread_test WINDOWS_DISABLED
        SOURCES ScopedEventBaseThreadTest.cpp
      TEST io_async_ssl_session_test
        CONTENT_DIR certs/
        SOURCES SSLSessionTest.cpp
      TEST io_async_write_chain_async_transport_wrapper_test
        SOURCES WriteChainAsyncTransportWrapperTest.cpp
)

if (${LIBAIO_FOUND})
  folly_define_tests(
    DIRECTORY io/async/test/
      TEST io_async_async_io_test
        SOURCES
          AsyncIOTest.cpp
          AsyncBaseTestLib.cpp
          IoTestTempFileUtil.cpp
  )
endif()
