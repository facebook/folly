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
import sys


from typing import Callable, Dict, List
from folly.executor cimport cProactorExecutor

_RaiseKeyError = object()

cdef class IocpQueue(dict):
    """
    Extends ProactorEventLoop's queue (a bare dictionary) to invoke AsyncioExecutor::drive()
    when notified via the proactor's IOCP
    """
    def __init__(IocpQueue self, ProactorExecutor executor):
        self._executor = executor

    def swap(self, loop: asyncio.AbstractEventLoop):
        """
        Replace ProactorEventLoop's queue with self
        """
        if isinstance(loop, asyncio.ProactorEventLoop):
            self.update(loop._proactor._cache)
            loop._proactor._cache = self
        else:
            raise NotImplementedError("IocpQueue can only be used with ProactorExecutor")


    def pop(self, k, default = _RaiseKeyError):
        if self._executor.pop(k):
            self._executor.drive()
            f = asyncio.Future()
            f.set_result(None)
            return (f, None, None, None)
        if default == _RaiseKeyError:
            return super().pop(k)
        return super().pop(k, default)

    def notify(self) -> None:
        self._executor.notify()
