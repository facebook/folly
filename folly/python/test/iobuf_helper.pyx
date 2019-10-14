# Copyright (c) Facebook, Inc. and its affiliates.
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

# distutils: language=c++

from folly.iobuf cimport from_unique_ptr, createChain, IOBuf

def get_empty_chain():
    return from_unique_ptr(createChain(1024, 128))

def make_chain(data):
    cdef IOBuf head = data.pop(0)
    # Make a new chain
    head = from_unique_ptr(head.c_clone())
    cdef IOBuf tbuf
    cdef IOBuf last = head
    for tbuf in data:
        last._this.appendChain(tbuf.c_clone())
        last = last.next
    return head
