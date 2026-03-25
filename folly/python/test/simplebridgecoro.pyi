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
from typing import Optional

class ExecutorStats:
    @property
    def drive_count(self) -> int: ...

def set_drive_time_slice_ms(time_slice_ms: int) -> None: ...
def get_value_x5_coro(val: int) -> asyncio.Future[int]: ...
async def return_five_after_cancelled() -> int: ...
def sleep_then_echo(sleep_ms: int, echo_val: int) -> asyncio.Future[int]: ...
def blocking_task(block_ms: int, echo_val: int) -> asyncio.Future[int]: ...
def get_executor_stats() -> Optional[ExecutorStats]: ...
