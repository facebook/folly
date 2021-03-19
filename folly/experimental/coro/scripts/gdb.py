#!/usr/bin/env python3
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

import re
import sys
import traceback
from dataclasses import dataclass
from typing import List, Tuple

import gdb

"""
These are the memory representations of the types folly uses for tracking
async stacks. See folly/tracing/AsyncStack.h

// Pointed to by thread local storage
struct AsyncStackRootHolder {
  AsyncStackRoot* value;
};

struct AsyncStackRoot {
  AsyncStackFrame* topFrame;
  AsyncStackRoot* nextRoot;
  void* stackFramePtr;
  void* returnAddress;
};

struct AsyncStackFrame {
  AsyncStackFrame* parentFrame;
  void* instructionPointer;
  AsyncStackRoot* stackRoot;
};

// Memory representation of how the compiler generates stack frames
struct StackFrame {
  StackFrame* stackFrame;
  void* returnAddress;
};
"""

# Key used in pthread thread local storage to hold a pointer to
# AsyncStackRootHolder
ASYNC_STACK_ROOT_TLS_KEY = "folly_async_stack_root_tls_key"


def get_field(addr: gdb.Value, n: int) -> gdb.Value:
    """
    This assumes addr is a pointer to a struct that consists entirely
    of pointers. Returns the n-th pointer in the struct.
    """
    return gdb.parse_and_eval(f"((uintptr_t*){addr})[{n}]")


@dataclass
class AsyncStackRootHolder:
    value: gdb.Value

    @staticmethod
    def from_addr(addr: gdb.Value) -> "AsyncStackRootHolder":
        return AsyncStackRootHolder(
            value=get_field(addr, 0),
        )


@dataclass
class AsyncStackRoot:
    top_frame: gdb.Value
    next_root: gdb.Value
    stack_frame_ptr: gdb.Value
    stack_root: gdb.Value

    @staticmethod
    def from_addr(addr: gdb.Value) -> "AsyncStackRoot":
        return AsyncStackRoot(
            top_frame=get_field(addr, 0),
            next_root=get_field(addr, 1),
            stack_frame_ptr=get_field(addr, 2),
            stack_root=get_field(addr, 3),
        )


@dataclass
class AsyncStackFrame:
    parent_frame: gdb.Value
    instruction_pointer: gdb.Value
    stack_root: gdb.Value

    @staticmethod
    def from_addr(addr: gdb.Value) -> "AsyncStackFrame":
        return AsyncStackFrame(
            parent_frame=get_field(addr, 0),
            instruction_pointer=get_field(addr, 1),
            stack_root=get_field(addr, 2),
        )


@dataclass
class StackFrame:
    stack_frame: gdb.Value
    return_address: gdb.Value

    @staticmethod
    def from_addr(addr: gdb.Value) -> "StackFrame":
        return StackFrame(
            stack_frame=get_field(addr, 0),
            return_address=get_field(addr, 1),
        )


def nullptr() -> gdb.Value:
    return gdb.parse_and_eval("0x0")


def to_hex(v: gdb.Value) -> str:
    """Returns v in hex padded with leading zeros"""
    return f"{int(v):#0{18}x}"


def get_file_name_and_line(addr: gdb.Value) -> Tuple[str, int]:
    regex = re.compile(r"Line (\d+) of (.*) starts at.*")
    output = gdb.execute(
        f"info line *{to_hex(addr)}",
        from_tty=False,
        to_string=True,
    ).split("\n")[0]
    groups = regex.match(output)
    return (groups.group(2).strip('"'), int(groups.group(1))) if groups else ("???", 0)


def get_func_name(addr: gdb.Value) -> str:
    regex = re.compile(r"(.*) \+ \d+ in section.* of .*")
    output = gdb.execute(
        f"info symbol {to_hex(addr)}",
        from_tty=False,
        to_string=True,
    ).split("\n")[0]
    groups = regex.match(output)
    return groups.group(1) if groups else "???"


def get_current_pthread_addr() -> gdb.Value:
    """
    Returns a pointer to the current pthread
    """
    regex = re.compile(r"\[Current thread is.*\(Thread (.*) \(LWP .*\)\)")
    output = gdb.execute("thread", from_tty=False, to_string=True).split("\n")[0]
    groups = regex.match(output)
    return gdb.parse_and_eval(groups.group(1)) if groups else nullptr()


def get_async_stack_root_addr() -> gdb.Value:
    """
    Returns a pointer to the top-most async stack root, or a nullptr if none
    exists.
    """
    pthread_addr = get_current_pthread_addr()
    if int(pthread_addr) == 0:
        return nullptr()

    # Check if the tls key is initialized
    tls_key = gdb.parse_and_eval(f"(int){ASYNC_STACK_ROOT_TLS_KEY}")
    if (int(tls_key) % (2 ** 32)) == ((2 ** 32) - 1):
        return nullptr()

    # get the stack root pointer from thread-local storage
    try:
        # Note: "struct pthread" is the implementation type for "pthread_t".
        # Its symbol information may not be available, depending if pthread
        # debug symbols are available.
        async_stack_root_holder_addr = gdb.parse_and_eval(
            f"((struct pthread*){to_hex(pthread_addr)})->specific"
            f"[(int){to_hex(tls_key)}/32]"
            f"[(int){to_hex(tls_key)}%32]"
            ".data"
        )
    except gdb.error as e:
        if "No struct type named pthread" not in str(e):
            raise e
        # If "struct pthread" isn't defined, use the precalculated offset.
        # Note: The offset is specific to linux x86_64.
        specific_offset = 1296
        pthread_key_data_addr = gdb.parse_and_eval(
            f"&(((uintptr_t[2]**)({to_hex(pthread_addr)}+{specific_offset}))"
            f"[(int){to_hex(tls_key)}/32]"
            f"[(int){to_hex(tls_key)}%32])"
        )

        # Extract the "data" field from pthread_key_data, which has the
        # following definition:
        # struct pthread_key_data {
        #   uintptr_t seq;
        #   void *data;
        # }
        async_stack_root_holder_addr = get_field(pthread_key_data_addr, 1)

    if int(async_stack_root_holder_addr) == 0:
        return nullptr()
    async_stack_root_holder = AsyncStackRootHolder.from_addr(
        async_stack_root_holder_addr
    )
    return async_stack_root_holder.value


def print_async_stack_addrs(addrs: List[gdb.Value]) -> None:
    if len(addrs) == 0:
        print("No async operation detected")
        return
    num_digits = len(str(len(addrs)))
    for (i, addr) in enumerate(addrs):
        func_name = get_func_name(addr)
        file_name, line = get_file_name_and_line(addr)
        print(
            f"#{str(i).ljust(num_digits, ' ')}"
            f" {to_hex(addr)} in {func_name} () at {file_name}:{line}"
        )


def get_async_stack_addrs_from_initial_frame(
    async_stack_frame_addr: gdb.Value,
) -> List[gdb.Value]:
    """
    Gets the list of async stack frames rooted at the current frame
    """
    addrs: List[gdb.Value] = []
    while int(async_stack_frame_addr) != 0:
        async_stack_frame = AsyncStackFrame.from_addr(async_stack_frame_addr)
        addrs.append(async_stack_frame.instruction_pointer)
        async_stack_frame_addr = async_stack_frame.parent_frame
    return addrs


def get_async_stack_addrs() -> List[gdb.Value]:
    """
    Gets the async stack trace, including normal stack frames with async
    stack frames.
    """
    async_stack_root_addr = get_async_stack_root_addr()

    # If we have no async stack root, this should return no frames.
    # If we do have a stack root, also include the current return address.
    if int(async_stack_root_addr) == 0:
        return []

    # start the stack trace from the top
    gdb.execute("f 0", from_tty=False, to_string=True)

    addrs: List[gdb.Value] = []
    addrs.append(gdb.parse_and_eval("$pc"))
    normal_stack_frame_addr = gdb.parse_and_eval("$rbp")
    while int(normal_stack_frame_addr) != 0 and int(async_stack_root_addr) != 0:
        normal_stack_frame = StackFrame.from_addr(normal_stack_frame_addr)
        async_stack_root = AsyncStackRoot.from_addr(async_stack_root_addr)

        # Walk the normal stack to find the caller of the frame that holds the
        # AsyncStackRoot. If the caller holds the AsyncStackRoot, then the
        # current frame is part of an async operation, and we should get the
        # async stack trace before adding the current frame.
        if int(normal_stack_frame.stack_frame) == int(async_stack_root.stack_frame_ptr):
            addrs += get_async_stack_addrs_from_initial_frame(
                async_stack_root.top_frame
            )
            # There could be more related work at the next async stack root.
            # Anything after the stack frame containing the last async stack root
            # is potentially unrelated to the current async stack.
            async_stack_root_addr = async_stack_root.next_root
            if int(async_stack_root_addr) == 0:
                break

        addrs.append(normal_stack_frame.return_address)
        normal_stack_frame_addr = normal_stack_frame.stack_frame
    return addrs


def print_async_stack_root_addrs(addrs: List[gdb.Value]) -> None:
    if len(addrs) == 0:
        print("No async stack roots detected")
        return
    num_digits = len(str(len(addrs)))
    for (i, addr) in enumerate(addrs):
        async_stack_root = AsyncStackRoot.from_addr(addr)
        if int(async_stack_root.stack_frame_ptr) != 0:
            stack_frame = StackFrame.from_addr(async_stack_root.stack_frame_ptr)
            func_name = get_func_name(stack_frame.return_address)
            file_name, line = get_file_name_and_line(stack_frame.return_address)
        else:
            func_name = "???"
            file_name = "???"
            line = 0
        print(
            f"#{str(i).ljust(num_digits, ' ')}"
            f" async stack root {to_hex(addr)}"
            f" located in normal stack frame {to_hex(async_stack_root.stack_frame_ptr)}"
            f" in {func_name} () at {file_name}:{line}"
        )


def get_async_stack_root_addrs() -> List[gdb.Value]:
    """
    Gets all the async stack roots that exist for the current thread.
    """
    addrs: List[gdb.Value] = []
    async_stack_root_addr = get_async_stack_root_addr()
    while int(async_stack_root_addr) != 0:
        addrs.append(async_stack_root_addr)
        async_stack_root = AsyncStackRoot.from_addr(async_stack_root_addr)
        async_stack_root_addr = async_stack_root.next_root
    return addrs


class CoroBacktraceCommand(gdb.Command):
    def __init__(self):
        super(CoroBacktraceCommand, self).__init__("co_bt", gdb.COMMAND_USER)

    def invoke(self, arg: str, from_tty: bool):
        try:
            addrs: List[gdb.Value] = []
            if arg:
                async_stack_root_addr = gdb.parse_and_eval(arg)
                if int(async_stack_root_addr) != 0:
                    async_stack_root = AsyncStackRoot.from_addr(async_stack_root_addr)
                    addrs = get_async_stack_addrs_from_initial_frame(
                        async_stack_root.top_frame
                    )
            else:
                addrs = get_async_stack_addrs()
            print_async_stack_addrs(addrs)
        except Exception:
            print("Error collecting async stack trace:")
            traceback.print_exception(*sys.exc_info())


class CoroAsyncStackRootsCommand(gdb.Command):
    def __init__(self):
        super(CoroAsyncStackRootsCommand, self).__init__(
            "co_async_stack_roots", gdb.COMMAND_USER
        )

    def invoke(self, arg: str, from_tty: bool):
        addrs = get_async_stack_root_addrs()
        print_async_stack_root_addrs(addrs)


def info() -> str:
    return """Pretty printers for folly::coro. Available commands:

co_bt [async_stack_root_addr]  Prints async stack trace for the current thread.
                               If an async stack root address is provided,
                               prints the async stack starting from this root.
co_async_stack_roots           Prints all async stack roots.
"""


def load() -> None:
    CoroBacktraceCommand()
    CoroAsyncStackRootsCommand()
    print(info())


if __name__ == "__main__":
    load()
