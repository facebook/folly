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

# Test definitions for folly/logging/test/

folly_define_tests(
  DIRECTORY logging/test/
      TEST logging_async_file_writer_test WINDOWS_DISABLED
        SOURCES AsyncFileWriterTest.cpp
      TEST logging_autotimer_test SOURCES AutoTimerTest.cpp
      TEST logging_config_parser_test SOURCES ConfigParserTest.cpp
      TEST logging_config_update_test SOURCES ConfigUpdateTest.cpp
      TEST logging_file_handler_factory_test WINDOWS_DISABLED
        SOURCES FileHandlerFactoryTest.cpp
      TEST logging_glog_formatter_test SOURCES GlogFormatterTest.cpp
      TEST logging_immediate_file_writer_test
        SOURCES ImmediateFileWriterTest.cpp
      TEST logging_log_category_test SOURCES LogCategoryTest.cpp
      TEST logging_logger_db_test SOURCES LoggerDBTest.cpp
      TEST logging_logger_test WINDOWS_DISABLED SOURCES LoggerTest.cpp
      TEST logging_log_level_test SOURCES LogLevelTest.cpp
      TEST logging_log_message_test SOURCES LogMessageTest.cpp
      TEST logging_log_name_test SOURCES LogNameTest.cpp
      TEST logging_log_stream_test SOURCES LogStreamTest.cpp
      TEST logging_rate_limiter_test SOURCES RateLimiterTest.cpp
      TEST logging_standard_log_handler_test SOURCES StandardLogHandlerTest.cpp
      TEST logging_xlog_test WINDOWS_DISABLED
        HEADERS
          XlogHeader1.h
          XlogHeader2.h
        SOURCES
          XlogFile1.cpp
          XlogFile2.cpp
          XlogTest.cpp
)
