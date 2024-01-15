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


import sys
import unittest

# mock lldb module
class Lldb:
    class Command:
        pass

    class SBExecutionContext:
        pass

    class SBValue:
        value: str

        def __init__(self, value: str) -> None:
            self.value = value

    def parse_and_eval(self, b):
        return self.SBValue(b)


class CoBt(unittest.TestCase):
    def setUp(self) -> None:
        sys.modules["lldb"] = Lldb()

    def test_null_eq(self) -> None:
        from .. import co_bt

        null1 = co_bt.LldbValue(Lldb.SBValue("0x0"))
        null2 = co_bt.LldbValue(Lldb.SBValue("0x0"))
        self.assertEquals(null1, null2)
