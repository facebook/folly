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

class FloatFormat:
    value: int
    name: str
    SHORTEST: FloatFormat
    SHORTEST_TRAILING_DOT_ZERO: FloatFormat
    SHORTEST_SINGLE: FloatFormat
    SHORTEST_SINGLE_TRAILING_DOT_ZERO: FloatFormat
    FIXED: FloatFormat
    GENERAL: FloatFormat
    def __init__(self, value: int) -> None: ...

def serialize_double(
    v: float,
    fmt: FloatFormat,
    num_digits: int = ...,
) -> str: ...
