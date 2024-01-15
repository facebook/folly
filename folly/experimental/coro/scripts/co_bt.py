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

import abc
import enum
import re
import sys
import traceback
from dataclasses import dataclass
from typing import ClassVar, List, Optional, Tuple, Type


class DebuggerValue(abc.ABC):
    """
    Represents a value from the debugger. This could represent the value
    of a variable, register, or expression.
    """

    @staticmethod
    @abc.abstractmethod
    def nullptr() -> "DebuggerValue":
        """
        Returns a nullptr value.
        """
        pass

    @staticmethod
    @abc.abstractmethod
    def parse_and_eval(expr: str) -> "DebuggerValue":
        """
        Executes `expr` in the debugger and returns the value.
        """
        pass

    @staticmethod
    @abc.abstractmethod
    def execute(expr: str) -> str:
        """
        Executes `expr` and returns the debugger output.
        """
        pass

    @staticmethod
    @abc.abstractmethod
    def get_current_pthread_addr() -> "DebuggerValue":
        """
        Returns a pointer to the current pthread. Returns nullptr if not found
        """
        pass

    @staticmethod
    @abc.abstractmethod
    def get_register(register: str) -> "DebuggerValue":
        """
        Returns the value in the provided register.
        """
        pass

    @abc.abstractmethod
    def get_field(self, n: int) -> "DebuggerValue":
        """
        This assumes the value is a pointer to a struct that consists entirely
        of pointers. Returns the n-th pointer in the struct.
        """
        pass

    @abc.abstractmethod
    def is_nullptr(self) -> bool:
        """
        Returns True if the value is nullptr or 0.
        """
        pass

    @abc.abstractmethod
    def int_value(self) -> int:
        """
        Returns the int value of the debugger value
        """
        pass

    @abc.abstractmethod
    def to_hex(self) -> str:
        """
        Returns the value in hex padded with leading zeroes.
        """
        pass

    @abc.abstractmethod
    def get_file_name_and_line(self) -> Optional[Tuple[str, int]]:
        """
        Returns the file name and line number of the value.
        Assumes the value is a pointer to an instruction.
        Returns None if the name could not be found.
        """
        pass

    @abc.abstractmethod
    def get_func_name(self) -> Optional[str]:
        """
        Returns the function name of the value. Returns None if the name could
        not be found
        """
        pass


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


@dataclass
class AsyncStackRootHolder:
    value: DebuggerValue

    @staticmethod
    def from_addr(addr: DebuggerValue) -> "AsyncStackRootHolder":
        return AsyncStackRootHolder(
            value=addr.get_field(0),
        )


@dataclass
class AsyncStackRoot:
    top_frame: DebuggerValue
    next_root: DebuggerValue
    stack_frame_ptr: DebuggerValue
    stack_root: DebuggerValue

    @staticmethod
    def from_addr(addr: DebuggerValue) -> "AsyncStackRoot":
        return AsyncStackRoot(
            top_frame=addr.get_field(0),
            next_root=addr.get_field(1),
            stack_frame_ptr=addr.get_field(2),
            stack_root=addr.get_field(3),
        )


@dataclass
class AsyncStackFrame:
    parent_frame: DebuggerValue
    instruction_pointer: DebuggerValue
    stack_root: DebuggerValue

    @staticmethod
    def from_addr(addr: DebuggerValue) -> "AsyncStackFrame":
        return AsyncStackFrame(
            parent_frame=addr.get_field(0),
            instruction_pointer=addr.get_field(1),
            stack_root=addr.get_field(2),
        )


@dataclass
class StackFrame:
    stack_frame: DebuggerValue
    return_address: DebuggerValue

    @staticmethod
    def from_addr(addr: DebuggerValue) -> "StackFrame":
        return StackFrame(
            stack_frame=addr.get_field(0),
            return_address=addr.get_field(1),
        )


def get_async_stack_root_addr(
    debugger_value_class: Type[DebuggerValue],
) -> DebuggerValue:
    """
    Returns a pointer to the top-most async stack root, or a nullptr if none
    exists.
    """
    pthread_addr = debugger_value_class.get_current_pthread_addr()
    if pthread_addr.is_nullptr():
        return debugger_value_class.nullptr()

    # Check if the tls key is initialized
    tls_key = debugger_value_class.parse_and_eval(
        f"(uint64_t){ASYNC_STACK_ROOT_TLS_KEY}"
    )
    if (tls_key.int_value() % (2**32)) == ((2**32) - 1):
        return debugger_value_class.nullptr()

    # get the stack root pointer from thread-local storage
    try:
        # Note: "struct pthread" is the implementation type for "pthread_t".
        # Its symbol information may not be available, depending if pthread
        # debug symbols are available.
        async_stack_root_holder_addr = debugger_value_class.parse_and_eval(
            f"((struct pthread*){pthread_addr.to_hex()})->specific"
            f"[(int){tls_key.to_hex()}/32]"
            f"[(int){tls_key.to_hex()}%32]"
            ".data"
        )
        if async_stack_root_holder_addr.is_nullptr():
            raise Exception("struct pthread info not found")
    except Exception:
        # If "struct pthread" isn't defined, use the precalculated offset.
        # Note: The offset is specific to linux x86_64.
        specific_offset = 1296
        specific_addr = debugger_value_class.parse_and_eval(
            f"{pthread_addr.to_hex()}+{specific_offset}"
        )
        specific_second_level_addr = specific_addr.get_field(tls_key.int_value() // 32)
        # pthread_key_data is equivalent to uintptr_t[2]
        # We want the N-th pthread_key_data, and the second pointer inside
        # pthread_key_data. So we want the uintptr_t at the 2 * N + 1 position
        async_stack_root_holder_addr = specific_second_level_addr.get_field(
            (2 * (tls_key.int_value() % 32)) + 1
        )

    if async_stack_root_holder_addr.is_nullptr():
        return debugger_value_class.nullptr()
    async_stack_root_holder = AsyncStackRootHolder.from_addr(
        async_stack_root_holder_addr
    )
    return async_stack_root_holder.value


def print_async_stack_addrs(addrs: List[DebuggerValue]) -> None:
    if len(addrs) == 0:
        print("No async operation detected")
        return
    num_digits = len(str(len(addrs)))
    for (i, addr) in enumerate(addrs):
        func_name = addr.get_func_name()
        if func_name is None:
            func_name = "???"
        file_name_line_pair = addr.get_file_name_and_line()
        if file_name_line_pair is None:
            file_name = "???"
            line = 0
        else:
            file_name, line = file_name_line_pair
        print(
            f"#{str(i).ljust(num_digits, ' ')}"
            f" {addr.to_hex()} in {func_name} () at {file_name}:{line}"
        )


def get_async_stack_addrs_from_initial_frame(
    async_stack_frame_addr: DebuggerValue,
) -> List[DebuggerValue]:
    """
    Gets the list of async stack frames rooted at the current frame
    """
    addrs: List[DebuggerValue] = []
    while not async_stack_frame_addr.is_nullptr():
        async_stack_frame = AsyncStackFrame.from_addr(async_stack_frame_addr)
        addrs.append(async_stack_frame.instruction_pointer)
        async_stack_frame_addr = async_stack_frame.parent_frame
    return addrs


def walk_normal_stack(
    normal_stack_frame_addr: DebuggerValue,
    normal_stack_frame_stop_addr: DebuggerValue,
) -> List[DebuggerValue]:
    """
    Returns the list of return addresses in the normal stack.
    Does not include stop_addr
    """
    addrs: List[DebuggerValue] = []
    while not normal_stack_frame_addr.is_nullptr():
        normal_stack_frame = StackFrame.from_addr(normal_stack_frame_addr)
        if (
            not normal_stack_frame_stop_addr.is_nullptr()
            and normal_stack_frame.stack_frame == normal_stack_frame_stop_addr
        ):
            # Reached end of normal stack, transition to the async stack
            # Do not include the return address in the stack trace that points
            # to the frame that registered the AsyncStackRoot.
            break
        addrs.append(normal_stack_frame.return_address)
        normal_stack_frame_addr = normal_stack_frame.stack_frame
    return addrs


@dataclass
class WalkAsyncStackResult:
    addrs: List[DebuggerValue]
    # Normal stack frame to start the next normal stack walk
    normal_stack_frame_addr: DebuggerValue
    normal_stack_frame_stop_addr: DebuggerValue
    # Async stack frame to start the next async stack walk after the next
    # normal stack walk
    async_stack_frame_addr: DebuggerValue


def walk_async_stack(
    debugger_value_class: Type[DebuggerValue],
    async_stack_frame_addr: DebuggerValue,
) -> WalkAsyncStackResult:
    """
    Walks the async stack and returns the next normal stack and async stack
    addresses to walk.
    """
    addrs: List[DebuggerValue] = []
    normal_stack_frame_addr = debugger_value_class.nullptr()
    normal_stack_frame_stop_addr = debugger_value_class.nullptr()
    async_stack_frame_next_addr = debugger_value_class.nullptr()
    while not async_stack_frame_addr.is_nullptr():
        async_stack_frame = AsyncStackFrame.from_addr(async_stack_frame_addr)
        addrs.append(async_stack_frame.instruction_pointer)

        if async_stack_frame.parent_frame.is_nullptr():
            # Reached end of async stack
            # Check if there is an AsyncStackRoot and if so, whether there
            # is an associated stack frame that indicates the normal stack
            # frame we should continue walking at.
            async_stack_root_addr = async_stack_frame.stack_root
            if async_stack_root_addr.is_nullptr():
                # This is a detached async stack. We are done
                break
            async_stack_root = AsyncStackRoot.from_addr(async_stack_root_addr)
            normal_stack_frame_addr = async_stack_root.stack_frame_ptr
            if normal_stack_frame_addr.is_nullptr():
                # No associated normal stack frame for this async stack root.
                # This means we should treat this as a top-level/detached
                # stack and not try to walk any further.
                break
            # Skip to the parent stack frame pointer
            normal_stack_frame = StackFrame.from_addr(normal_stack_frame_addr)
            normal_stack_frame_addr = normal_stack_frame.stack_frame

            # Check if there is a higher-level AsyncStackRoot that defines
            # the stop point we should stop walking normal stack frames at.
            # If there is no higher stack root then we will walk to the
            # top of the normal stack (normalStackFrameStop == nullptr).
            # Otherwise we record the frame pointer that we should stop
            # at and walk normal stack frames until we hit that frame.
            # Also get the async stack frame where the next async stack walk
            # should begin after the next normal stack walk finishes.
            async_stack_root_addr = async_stack_root.next_root
            if not async_stack_root_addr.is_nullptr():
                async_stack_root = AsyncStackRoot.from_addr(async_stack_root_addr)
                normal_stack_frame_stop_addr = async_stack_root.stack_frame_ptr
                async_stack_frame_next_addr = async_stack_root.top_frame

        async_stack_frame_addr = async_stack_frame.parent_frame

    return WalkAsyncStackResult(
        addrs=addrs,
        normal_stack_frame_addr=normal_stack_frame_addr,
        normal_stack_frame_stop_addr=normal_stack_frame_stop_addr,
        async_stack_frame_addr=async_stack_frame_next_addr,
    )


def get_async_stack_addrs(
    debugger_value_class: Type[DebuggerValue],
) -> List[DebuggerValue]:
    """
    Gets the async stack trace, including normal stack frames with async
    stack frames.

    See C++ implementation in `getAsyncStackTraceSafe` in
    folly/experimental/symbolizer/StackTrace.cpp
    """
    async_stack_root_addr = get_async_stack_root_addr(debugger_value_class)

    # If we have no async stack root, this should return no frames.
    # If we do have a stack root, also include the current return address.
    if async_stack_root_addr.is_nullptr():
        return []

    # Start the stack trace from the top
    debugger_value_class.execute("f 0")

    # Start by walking the normal stack until we get to the frame right before
    # the frame that holds the async root.
    async_stack_root = AsyncStackRoot.from_addr(async_stack_root_addr)
    normal_stack_frame_addr = debugger_value_class.get_register("rbp")
    normal_stack_frame_stop_addr = async_stack_root.stack_frame_ptr
    addrs: List[DebuggerValue] = []
    addrs.append(debugger_value_class.get_register("pc"))
    async_stack_frame_addr = async_stack_root.top_frame

    while (
        not normal_stack_frame_addr.is_nullptr()
        or not async_stack_frame_addr.is_nullptr()
    ):
        addrs += walk_normal_stack(
            normal_stack_frame_addr, normal_stack_frame_stop_addr
        )
        walk_async_stack_result = walk_async_stack(
            debugger_value_class, async_stack_frame_addr
        )
        addrs += walk_async_stack_result.addrs
        normal_stack_frame_addr = walk_async_stack_result.normal_stack_frame_addr
        normal_stack_frame_stop_addr = (
            walk_async_stack_result.normal_stack_frame_stop_addr
        )
        async_stack_frame_addr = walk_async_stack_result.async_stack_frame_addr
    return addrs


def print_async_stack_root_addrs(addrs: List[DebuggerValue]) -> None:
    if len(addrs) == 0:
        print("No async stack roots detected")
        return
    num_digits = len(str(len(addrs)))
    for (i, addr) in enumerate(addrs):
        async_stack_root = AsyncStackRoot.from_addr(addr)
        if not async_stack_root.stack_frame_ptr.is_nullptr():
            stack_frame = StackFrame.from_addr(async_stack_root.stack_frame_ptr)
            func_name = stack_frame.return_address.get_func_name()
            if func_name is None:
                func_name = "???"
            file_name_line_pair = stack_frame.return_address.get_file_name_and_line()
            if file_name_line_pair is not None:
                file_name, line = file_name_line_pair
            else:
                file_name = "???"
                line = 0
        else:
            func_name = "???"
            file_name = "???"
            line = 0
        print(
            f"#{str(i).ljust(num_digits, ' ')}"
            f" async stack root {addr.to_hex()}"
            f" located in normal stack frame {async_stack_root.stack_frame_ptr.to_hex()}"
            f" in {func_name} () at {file_name}:{line}"
        )


def get_async_stack_root_addrs(
    debugger_value_class: Type[DebuggerValue],
) -> List[DebuggerValue]:
    """
    Gets all the async stack roots that exist for the current thread.
    """
    addrs: List[DebuggerValue] = []
    async_stack_root_addr = get_async_stack_root_addr(debugger_value_class)
    while not async_stack_root_addr.is_nullptr():
        addrs.append(async_stack_root_addr)
        async_stack_root = AsyncStackRoot.from_addr(async_stack_root_addr)
        async_stack_root_addr = async_stack_root.next_root
    return addrs


def backtrace_command(
    debugger_value_class: Type[DebuggerValue],
    stack_root: Optional[str],
) -> None:
    try:
        addrs: List[DebuggerValue] = []
        if stack_root:
            async_stack_root_addr = debugger_value_class.parse_and_eval(stack_root)
            if not async_stack_root_addr.is_nullptr():
                async_stack_root = AsyncStackRoot.from_addr(async_stack_root_addr)
                addrs = get_async_stack_addrs_from_initial_frame(
                    async_stack_root.top_frame
                )
        else:
            addrs = get_async_stack_addrs(debugger_value_class)
        print_async_stack_addrs(addrs)
    except Exception:
        print("Error collecting async stack trace:")
        traceback.print_exception(*sys.exc_info())


def async_stack_roots_command(debugger_value_class: Type[DebuggerValue]) -> None:
    addrs = get_async_stack_root_addrs(debugger_value_class)
    print_async_stack_root_addrs(addrs)


def co_bt_info() -> str:
    return """Command: co_bt [async_stack_root_addr]

Prints async stack trace for the current thread.
If an async stack root address is provided,
prints the async stack starting from this root.
"""


def co_async_stack_root_info() -> str:
    return """Command: co_async_stack_roots

Prints all async stack roots.
"""


class DebuggerType(enum.Enum):
    GDB = 0
    LLDB = 1


debugger_type: Optional[DebuggerType] = None
if debugger_type is None:  # noqa: C901
    try:
        import gdb

        class GdbValue(DebuggerValue):
            """
            GDB implementation of a debugger value
            """

            value: gdb.Value

            def __init__(self, value: gdb.Value) -> None:
                self.value = value

            @staticmethod
            def nullptr() -> DebuggerValue:
                return GdbValue.parse_and_eval("0x0")

            @staticmethod
            def parse_and_eval(expr: str) -> DebuggerValue:
                return GdbValue(gdb.parse_and_eval(expr))

            @staticmethod
            def execute(expr: str) -> str:
                return gdb.execute(expr, from_tty=False, to_string=True)

            @staticmethod
            def get_current_pthread_addr() -> DebuggerValue:
                try:
                    # On Linux x86_64, the pthread struct pointer is stored
                    # in the fs_base virtual register. Try to read it from
                    # this register first.
                    fs_base = GdbValue.get_register("fs_base")
                    if not fs_base.is_nullptr():
                        return fs_base
                except Exception:
                    pass
                regex = re.compile(r"\[Current thread is.*\(Thread (.*) \(LWP .*\)\)")
                output = GdbValue.execute("thread").split("\n")[0]
                groups = regex.match(output)
                return (
                    GdbValue.parse_and_eval(groups.group(1))
                    if groups
                    else GdbValue.nullptr()
                )

            @staticmethod
            def get_register(register: str) -> DebuggerValue:
                return GdbValue.parse_and_eval(f"${register}")

            def get_field(self, n: int) -> DebuggerValue:
                return GdbValue.parse_and_eval(f"((uintptr_t*){self.value})[{n}]")

            def is_nullptr(self) -> bool:
                return int(self.value) == 0

            def int_value(self) -> int:
                return int(self.value)

            def to_hex(self) -> str:
                return f"{int(self.value):#0{18}x}"

            def get_file_name_and_line(self) -> Optional[Tuple[str, int]]:
                regex = re.compile(r"Line (\d+) of (.*) starts at.*")
                output = GdbValue.execute(f"info line *{self.to_hex()}",).split(
                    "\n"
                )[0]
                groups = regex.match(output)
                return (
                    (groups.group(2).strip('"'), int(groups.group(1)))
                    if groups
                    else None
                )

            def get_func_name(self) -> Optional[str]:
                regex = re.compile(r"(.*) \+ \d+ in section.* of .*")
                output = GdbValue.execute(f"info symbol {self.to_hex()}",).split(
                    "\n"
                )[0]
                groups = regex.match(output)
                return groups.group(1) if groups else None

            def __eq__(self, other) -> bool:
                return self.int_value() == other.int_value()

        class GdbCoroBacktraceCommand(gdb.Command):
            def __init__(self):
                print(co_bt_info())
                super(GdbCoroBacktraceCommand, self).__init__("co_bt", gdb.COMMAND_USER)

            def invoke(self, arg: str, from_tty: bool):
                backtrace_command(GdbValue, arg)

        class GdbCoroAsyncStackRootsCommand(gdb.Command):
            def __init__(self):
                print(co_async_stack_root_info())
                super(GdbCoroAsyncStackRootsCommand, self).__init__(
                    "co_async_stack_roots", gdb.COMMAND_USER
                )

            def invoke(self, arg: str, from_tty: bool):
                async_stack_roots_command(GdbValue)

        debugger_type = DebuggerType.GDB
    except Exception:
        pass

if debugger_type is None:  # noqa: C901
    try:
        import lldb

        class LldbValue(DebuggerValue):
            """
            LLDB implementation of a debugger value
            """

            exe_ctx: ClassVar[Optional[lldb.SBExecutionContext]] = None
            next_name_num: ClassVar[int] = 0
            value: lldb.SBValue

            def __init__(self, value: lldb.SBValue) -> None:
                self.value = value

            @staticmethod
            def nullptr() -> DebuggerValue:
                return LldbValue.parse_and_eval("nullptr")

            @staticmethod
            def parse_and_eval(expr: str) -> DebuggerValue:
                value = LldbValue(
                    LldbValue.exe_ctx.GetTarget().CreateValueFromExpression(
                        f"{LldbValue.next_name_num}", expr
                    )
                )
                LldbValue.next_name_num += 1
                return value

            @staticmethod
            def execute(expr: str) -> str:
                return_obj = lldb.SBCommandReturnObject()
                LldbValue.exe_ctx.GetTarget().GetDebugger().GetCommandInterpreter().HandleCommand(
                    expr, LldbValue.exe_ctx, return_obj, False
                )
                if return_obj.Succeeded():
                    return return_obj.GetOutput()
                return return_obj.GetError()

            @staticmethod
            def get_current_pthread_addr() -> DebuggerValue:
                try:
                    # On Linux x86_64, the pthread struct pointer is stored
                    # in the fs_base virtual register.
                    # LLDB upstream currently does not provide support for
                    # the lldb fs_base virtual register, see:
                    # https://discourse.llvm.org/t/how-to-get-pthread-pointer-from-lldb/70542
                    # Internally, we have a patch to add support for fs_base on
                    # Linux x86_64 that we will upstream soon.
                    # In this case, try to use the fs_base register if it
                    # exists, otherwise fall back to other ways:
                    fs_base = LldbValue.get_register("fs_base")
                    if not fs_base.is_nullptr():
                        return fs_base
                except Exception:
                    pass

                # If we are a live process, try to call this helper to get
                # the pthread struct pointer. Note that this may not work
                # on core dumps or optimized builds.
                result = LldbValue.exe_ctx.GetFrame().EvaluateExpression(
                    "(struct pthread*)pthread_self()"
                )
                if result.GetError().Success():
                    return LldbValue(result)
                return LldbValue.nullptr()

            @staticmethod
            def get_register(register: str) -> DebuggerValue:
                return LldbValue(LldbValue.exe_ctx.GetFrame().FindRegister(register))

            def get_field(self, n: int) -> DebuggerValue:
                # Linux x86_64 size of a pointer
                ptr_size = 8
                ptr_type = (
                    LldbValue.exe_ctx.GetTarget()
                    .FindFirstType("uintptr_t")
                    .GetPointerType()
                )
                address = lldb.SBAddress(
                    self.int_value() + ptr_size * n,
                    LldbValue.exe_ctx.GetTarget(),
                )
                value = LldbValue(
                    LldbValue.exe_ctx.GetTarget().CreateValueFromAddress(
                        f"{LldbValue.next_name_num}", address, ptr_type
                    )
                )
                LldbValue.next_name_num += 1
                return value

            def is_nullptr(self) -> bool:
                return int(self.value.value, 0) == 0

            def int_value(self) -> int:
                return int(self.value.value, 0)

            def to_hex(self) -> str:
                return f"{int(self.value.value, 0):#0{18}x}"

            # Type must be in quotes because it breaks parsing
            # with conditional imports
            def _get_symbol_context(self) -> "lldb.SBSymbolContext":
                address = lldb.SBAddress(
                    self.int_value(), LldbValue.exe_ctx.GetTarget()
                )
                return LldbValue.exe_ctx.GetTarget().ResolveSymbolContextForAddress(
                    address, lldb.eSymbolContextEverything
                )

            def get_file_name_and_line(self) -> Optional[Tuple[str, int]]:
                symbol_context = self._get_symbol_context()
                line_entry = symbol_context.GetLineEntry()
                path = line_entry.GetFileSpec().fullpath
                if path:
                    return (path, line_entry.GetLine())
                return None

            def get_func_name(self) -> Optional[str]:
                symbol_context = self._get_symbol_context()
                if symbol_context.GetFunction().IsValid():
                    return symbol_context.GetFunction().GetDisplayName()
                return symbol_context.GetSymbol().GetDisplayName()

            def __eq__(self, other) -> bool:
                return self.int_value() == other.int_value()

        class LldbCoroBacktraceCommand:
            program: ClassVar[str] = "co_bt"

            def __init__(self, debugger, internal_dict):
                pass

            @classmethod
            def register_lldb_command(cls, debugger, module_name):
                command = (
                    f"command script add -c {module_name}.{cls.__name__} {cls.program}"
                )
                debugger.HandleCommand(command)

            def get_short_help(self):
                return co_bt_info()

            def get_long_help(self):
                return co_bt_info()

            def __call__(self, debugger, command, exe_ctx, result):
                LldbValue.exe_ctx = exe_ctx
                backtrace_command(LldbValue, command)

        class LldbCoroAsyncStackRootsCommand:
            program = "co_async_stack_roots"

            def __init__(self, debugger, internal_dict):
                pass

            @classmethod
            def register_lldb_command(cls, debugger, module_name):
                command = (
                    f"command script add -c {module_name}.{cls.__name__} {cls.program}"
                )
                debugger.HandleCommand(command)

            def get_short_help(self):
                return co_async_stack_root_info()

            def get_long_help(self):
                return co_async_stack_root_info()

            def __call__(self, debugger, command, exe_ctx, result):
                LldbValue.exe_ctx = exe_ctx
                async_stack_roots_command(LldbValue)

        debugger_type = DebuggerType.LLDB
    except Exception:
        pass


def info():
    return f"""Pretty printers for folly::coro. Available commands:
{co_bt_info()}

{co_async_stack_root_info()}
"""


def load(debugger=None) -> None:
    """
    This debugger script is meant to work with both lldb and gdb. Use
    conditional imports, as one will not be defined when we use the other.
    """
    if debugger_type == DebuggerType.GDB:
        GdbCoroBacktraceCommand()
        GdbCoroAsyncStackRootsCommand()
    elif debugger_type == DebuggerType.LLDB:
        LldbCoroBacktraceCommand.register_lldb_command(debugger, __name__)
        LldbCoroAsyncStackRootsCommand.register_lldb_command(debugger, __name__)
    else:
        pass


def __lldb_init_module(debugger, internal_dict) -> None:
    """
    This function will be invoked automatically by lldb when we run:

    command script import <path>

    debugger will be of type lldb.SBDebugger
    """
    load(debugger)


if __name__ == "__main__":
    load()
