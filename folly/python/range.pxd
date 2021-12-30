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

cdef extern from "folly/Range.h" namespace "folly":
    cdef cppclass Range[T]:
        Range()
        Range(T, int)
        T data()
        T begin()
        T end()
        int size()

ctypedef Range[const char*] StringPiece
ctypedef Range[const unsigned char*] ByteRange

ctypedef fused R:
    StringPiece
    ByteRange

# Conversion Helpers
cdef inline bytes to_bytes(R range):
    return <bytes>range.data()[:range.size()]
