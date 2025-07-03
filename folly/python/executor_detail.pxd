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

# distutils: language = c++

from folly.executor cimport cAsyncioExecutor

cdef extern from "folly/python/executor.h" namespace "folly::python::executor_detail":
    # This is what a function ptr looks like in pxd language. 
    cdef cAsyncioExecutor*(*get_running_executor)(bint running)
    cdef int(*set_executor_for_loop)(loop, cAsyncioExecutor* executor)
