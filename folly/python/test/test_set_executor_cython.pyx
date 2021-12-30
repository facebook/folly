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

import asyncio

from libcpp.memory cimport unique_ptr

from folly.executor cimport set_executor_for_loop, get_executor, cAsyncioExecutor


cdef extern from "folly/python/test/test_set_executor.h" namespace "folly::python::test":
    cdef cppclass cTestAsyncioExecutor "folly::python::test::TestAsyncioExecutor"(cAsyncioExecutor):
        pass

    cdef unique_ptr[cTestAsyncioExecutor] makeTestAsyncioExecutor()


def test_set_custom_executor(test):
   loop = asyncio.new_event_loop()
   try:
      asyncio.set_event_loop(loop)
      executor = makeTestAsyncioExecutor()
      test.assertEqual(set_executor_for_loop(loop, executor.get()), 0)
      test.assertTrue(get_executor() == executor.get())
   finally:
      loop.close()


def test_cannot_override_existing_loop(test):
   loop = asyncio.new_event_loop()
   try:
      asyncio.set_event_loop(loop)
      # Creates a default executor
      executor = get_executor()
      test.assertEqual(set_executor_for_loop(loop, executor), -1)
   finally:
      loop.close()


def test_clear_existing_loop(test):
   loop = asyncio.new_event_loop()
   try:
      asyncio.set_event_loop(loop)
      # Creates a default executor
      executor = get_executor()
      test.assertEqual(set_executor_for_loop(loop, NULL), 0)
      # This would return -1 if an executor were set
      # (see test_cannot_override_existing_loop)
      test.assertEqual(set_executor_for_loop(loop, executor), 0)
   finally:
      loop.close()
