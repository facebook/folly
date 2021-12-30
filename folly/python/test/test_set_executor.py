#!/usr/bin/env python3
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

import unittest

from . import test_set_executor_cython


class TestSetExecutor(unittest.TestCase):
    def test_set_custom_executor(self):
        test_set_executor_cython.test_set_custom_executor(self)

    def test_cannot_override_existing_loop(self):
        test_set_executor_cython.test_cannot_override_existing_loop(self)

    def test_clear_existing_loop(self):
        test_set_executor_cython.test_clear_existing_loop(self)
