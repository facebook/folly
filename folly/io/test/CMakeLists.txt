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

# Test definitions for folly/io/test/

folly_define_tests(
  DIRECTORY io/test/
      TEST io_fs_util_test SOURCES FsUtilTest.cpp
      BENCHMARK io_iobuf_benchmark WINDOWS_DISABLED SOURCES IOBufBenchmark.cpp
      TEST io_iobuf_test WINDOWS_DISABLED SOURCES IOBufTest.cpp
      TEST io_iobuf_cursor_test SOURCES IOBufCursorTest.cpp
      TEST io_iobuf_queue_test SOURCES IOBufQueueTest.cpp
      TEST io_record_io_test WINDOWS_DISABLED SOURCES RecordIOTest.cpp
      TEST io_shutdown_socket_set_test HANGING
        SOURCES ShutdownSocketSetTest.cpp
      TEST io_socket_option_value_test HANGING
        SOURCES SocketOptionValueTest.cpp
)
