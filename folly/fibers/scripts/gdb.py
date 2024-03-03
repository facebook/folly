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

import collections
import functools
import itertools

import gdb
import gdb.printing
import gdb.types
import gdb.unwinder
import gdb.xmethod


#
# Pretty Printers
#


class FiberPrinter:
    """PrettyPrint a folly::fibers::Fiber"""

    def __init__(self, val):
        self.val = val

        state = self.val["state_"]
        d = gdb.types.make_enum_dict(state.type)
        d = dict((v, k) for k, v in d.items())
        self.state = d[int(state)]

    def state_to_string(self):
        if self.state == "folly::fibers::Fiber::INVALID":
            return "Invalid"
        if self.state == "folly::fibers::Fiber::NOT_STARTED":
            return "Not started"
        if self.state == "folly::fibers::Fiber::READY_TO_RUN":
            return "Ready to run"
        if self.state == "folly::fibers::Fiber::RUNNING":
            return "Running"
        if self.state == "folly::fibers::Fiber::AWAITING":
            return "Awaiting"
        if self.state == "folly::fibers::Fiber::AWAITING_IMMEDIATE":
            return "Awaiting immediate"
        if self.state == "folly::fibers::Fiber::YIELDED":
            return "Yielded"
        return "Unknown"

    def backtrace_available(self):
        return (
            self.state != "folly::fibers::Fiber::INVALID"
            and self.state != "folly::fibers::Fiber::NOT_STARTED"
            and self.state != "folly::fibers::Fiber::RUNNING"
        )

    def to_string(self):
        return 'folly::fibers::Fiber(state="{state}", backtrace={bt})'.format(
            state=self.state_to_string(), bt=self.backtrace_available()
        )


class FiberManagerPrinter:
    """PrettyPrint a folly::fibers::FiberManager"""

    def __init__(self, fm):
        self.fm = fm

    def children(self):
        def limit_with_dots(fibers_iterator):
            num_items = 0
            fiber_print_limit = gdb.parameter("fiber manager-print-limit")
            for fiber in fibers_iterator:
                if fiber_print_limit and num_items >= fiber_print_limit:
                    yield ("address", "...")
                    yield ("fiber", "...")
                    return
                yield ("address", str(fiber.address))
                yield ("fiber", fiber)
                num_items += 1

        return limit_with_dots(fiber_manager_active_fibers(self.fm))

    def to_string(self):
        return "folly::fibers::FiberManager"

    def display_hint(self):
        return "map"


#
# XMethods
#


class GetFiberXMethodWorker(gdb.xmethod.XMethodWorker):
    def get_arg_types(self):
        return gdb.lookup_type("int")

    def get_result_type(self):
        return gdb.lookup_type("int")

    def __call__(self, *args):
        fm = args[0]
        index = int(args[1])
        fiber = next(itertools.islice(fiber_manager_active_fibers(fm), index, None))
        if fiber is None:
            raise gdb.GdbError("Index out of range")
        else:
            return fiber


class GetFiberXMethodMatcher(gdb.xmethod.XMethodMatcher):
    def __init__(self):
        super(GetFiberXMethodMatcher, self).__init__("Fiber address method matcher")
        self.worker = GetFiberXMethodWorker()

    def match(self, class_type, method_name):
        if (
            class_type.name == "folly::fibers::FiberManager"
            and method_name == "get_fiber"
        ):
            return self.worker
        return None


class FiberXMethodWorker(gdb.xmethod.XMethodWorker):
    def get_arg_types(self):
        return None

    def get_result_type(self):
        return None

    def __call__(self, *args):
        return fiber_activate(args[0])


class FiberXMethodMatcher(gdb.xmethod.XMethodMatcher):
    def __init__(self):
        super(FiberXMethodMatcher, self).__init__("Fiber method matcher")
        self.worker = FiberXMethodWorker()

    def match(self, class_type, method_name):
        if class_type.name == "folly::fibers::Fiber" and method_name == "activate":
            return self.worker
        return None


#
# Unwinder
#


class FrameId:
    def __init__(self, sp, pc):
        self.sp = sp
        self.pc = pc


class FiberUnwinder(gdb.unwinder.Unwinder):
    instance = None

    @classmethod
    def init(cls):
        if cls.instance is None:
            cls.instance = FiberUnwinder()
            gdb.unwinder.register_unwinder(None, cls.instance)

    @classmethod
    def get_fiber(cls):
        cls.init()
        return cls.instance.fiber

    @classmethod
    def set_fiber(cls, fiber):
        cls.init()

        if not fiber:
            if not cls.instance.fiber:
                return "No active fiber."

        # get the relevant fiber's info, new one if specified, old one if not
        info = FiberInfo(fiber or cls.instance.fiber)

        if fiber:
            if not FiberPrinter(fiber).backtrace_available():
                return "Can not activate a non-waiting fiber."
            # set the unwinder to do the right thing
            cls.instance.fiber_context_ptr = fiber["fiberImpl_"]["fiberContext_"]
        else:
            # disable the unwinder
            cls.instance.fiber_context_ptr = None

        # track the active fiber (or lack thereof)
        cls.instance.fiber = fiber

        # Clear frame cache to make sure we actually use the right unwinder
        gdb.invalidate_cached_frames()

        return "[{action} fiber {id} ({info})]".format(
            action="Switching to" if fiber else "Deactivating", id=info.id, info=info
        )

    def __init__(self):
        super(FiberUnwinder, self).__init__("Fiber unwinder")
        self.fiber_context_ptr = None
        self.fiber = None

    def __call__(self, pending_frame):
        # We only unwind the first frame, bail if already done
        if not self.fiber_context_ptr:
            return None

        orig_rsp = pending_frame.read_register("rsp")
        orig_rip = pending_frame.read_register("rip")

        void_star_star = gdb.lookup_type("uint64_t").pointer()
        ptr = self.fiber_context_ptr.cast(void_star_star)

        # This code may need to be adjusted to newer versions of boost::context.
        #
        # The easiest way to get these offsets is to first attach to any
        # program which uses folly::fibers and add a break point in
        # boost::context::jump_fcontext. You then need to save information about
        # frame 1 via 'info frame 1' command.
        #
        # After that you need to resume program until fiber switch is complete
        # and expore the contents of saved fiber context via
        # 'x/16gx {fiber pointer}->fiberImpl_.fiberContext_' command.
        # You then need to match those to the following values you've previously
        # observed in the output of 'info frame 1'  command.
        #
        # Value found at "rbp at X" of 'info frame 1' output:
        rbp = (ptr + 6).dereference()
        # Value found at "rip = X" of 'info frame 1' output:
        rip = (ptr + 7).dereference()
        # Value found at "caller of frame at X" of 'info frame 1' output:
        rsp = rbp - 96

        # we play a horrible trick on gdb here, to make it unwind the fiber rather
        # than the stack of the currently selected frame.  Essentially, we take
        # whatever frame is currently newest, and lie to gdb saying that we are
        # the "next older" frame by creating fake unwind info that points to our
        # fiber's frame.  This means that frame #0 is actually from the currently
        # selected thread, so we filter that out when we inspect it elsewhere.
        frame_id = FrameId(orig_rsp, orig_rip)
        unwind_info = pending_frame.create_unwind_info(frame_id)
        unwind_info.add_saved_register("rbp", rbp)
        unwind_info.add_saved_register("rsp", rsp)
        unwind_info.add_saved_register("rip", rip)

        # "unregister" ourselves from further frame processing; we only need to
        # handle the top-level sentinel frame
        self.fiber_context_ptr = None

        return unwind_info


class FiberFrameFilter:
    """Frame filter for fiber stacks

    This class is used to "skip" past the innermost frame when parsing backtraces,
    which is actually from the currently selected thread.
    """

    def __init__(self):
        self.name = "fibers-skip-first-frame"
        self.priority = 100
        self.enabled = True
        gdb.frame_filters[self.name] = self

    def filter(self, frames):
        # skip first frame filter if a fiber is active
        if FiberUnwinder.get_fiber():
            next(frames)
        return frames


#
# Commands
#


# small utility to make sure a method always runs in a particular language mode
# and sets it back when donex
def use_language(lang):
    def wrapper(fn):
        @functools.wraps(fn)
        def wrapped(*args, **kwds):
            orig = gdb.parameter("language")
            if lang == orig:
                return fn(*args, **kwds)
            try:
                gdb.execute("set language " + lang)
                return fn(*args, **kwds)
            finally:
                gdb.execute("set language " + orig)

        return wrapped

    return wrapper


class FiberCommand(gdb.Command):
    """Use this command to switch between fibers.

    Due to gdb limitations, this will *override* the current thread, and you will
    need to run "fiber deactivate" in order to get normal thread processing back.
    """

    def __init__(self):
        super(FiberCommand, self).__init__("fiber", gdb.COMMAND_USER, prefix=True)

    @use_language("c++")
    def invoke(self, arg, from_tty):
        self.dont_repeat()
        argv = gdb.string_to_argv(arg)

        if not argv:
            if not FiberUnwinder.instance or not FiberUnwinder.instance.fiber:
                print("No fiber selected")
            else:
                info = FiberInfo(FiberUnwinder.instance.fiber)
                print("[Current fiber is {id} ({info})]".format(id=info.id, info=info))
            return

        # look up fiber
        fiber_ptr = get_fiber(arg)
        if not fiber_ptr:
            print("Invalid fiber id or address: " + arg)
            return

        # activate
        print(fiber_activate(fiber_ptr))


class FiberNameCommand(gdb.Command):
    """Set the current fiber's name.

    Usage: fiber name [NAME]
    If NAME is not given, then any existing name is removed"""

    def __init__(self):
        super(FiberNameCommand, self).__init__("fiber name", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        fiber = FiberUnwinder.get_fiber()
        if not fiber:
            print("No fiber activated.")
            return
        FiberInfo(fiber).name = arg or None


class FiberInfoCommand(gdb.Command):
    """info on fibers

    Can filter output with "M.F" or "M" format, e.g. "2.3" for manager 2 fiber 3
    or "1" for all fibers from manager 1. Can specify multiple times.

    See `help set fiber` for ways to alter the information printed.
    """

    def __init__(self):
        super(FiberInfoCommand, self).__init__(
            "info fibers", gdb.COMMAND_STATUS, prefix=True
        )

    def trace_info(self, fiber, skip=()):
        # get the fiber symbol info if we can
        if not FiberPrinter(fiber).backtrace_available():
            return "[backtrace unavailable]"

        # save original fiber state
        orig = FiberUnwinder.get_fiber()
        try:
            fiber_activate(fiber)
            # first frame is always bogus
            frame = gdb.selected_frame().older()
            frame_id = 1

            # find last "useful" frame for display
            while any(word in frame.name() for word in skip):
                # check for unwinding failures and if so use selected frame
                if frame.unwind_stop_reason() != gdb.FRAME_UNWIND_NO_REASON:
                    frame = gdb.selected_frame()
                    break
                frame = frame.older()
                frame_id += 1

            if not frame.is_valid():
                return "[invalid frame]"

            sal = frame.find_sal()
            return "#{frame_id} 0x{pc:016x} in {function} at {filename}:{line}".format(
                frame_id=frame_id,
                pc=frame.pc(),
                function=frame.function(),
                filename=sal.symtab.filename,
                line=sal.line,
            )
        finally:
            # restore original fiber state
            FiberUnwinder.set_fiber(orig)

    @use_language("c++")
    def invoke(self, arg, to_tty):
        only = gdb.string_to_argv(arg)
        orig = FiberUnwinder.get_fiber()
        frame_skip_words = gdb.parameter("fiber info-frame-skip-words").split(" ")

        seen = set()
        for (mid, fid), info in get_fiber_info(only, managers=True):
            # track that we've seen this fiber/manager
            if fid:
                seen.add("{}.{}".format(mid, fid))
                seen.add(str(mid))

            # If it's a manager, print the header and continue
            if not fid:
                manager = info
                print(
                    "  {mid:<4} {fmtype} {address}".format(
                        mid=mid, fmtype=manager.type, address=manager.address
                    )
                )
                continue

            # print the fiber info
            print(
                "{active}   {id:<6} {info} {trace_info}".format(
                    active="*" if info.fiber == orig else " ",
                    id=info.id,
                    info=info,
                    trace_info=self.trace_info(info.fiber, frame_skip_words),
                )
            )

        # print warning if there are no fibers
        if not seen:
            print("No fibers.")

        # print warnings for any filters we didn't find
        for match in only or []:
            if match not in seen:
                if "." in match:
                    print("Invalid fiber id: " + match)
                else:
                    print("Invalid fiber manager id: " + match)


class FiberApplyCommand(gdb.Command):
    """Apply a command to a list of fibers.

    Usage: fiber apply ( ( FIBER_ID | MANAGER_ID ) [ ... ] | all ) COMMAND

    This will cause each specified FIBER_ID (or all fibers for a specified MANAGER_ID)
    to be activated in turn, running the specified gdb COMMAND after each activation.
    """

    def __init__(self):
        super(FiberApplyCommand, self).__init__("fiber apply", gdb.COMMAND_USER)

    def usage(self):
        raise gdb.Error("usage: fiber apply [ FIBER_FILTER [ ... ] | apply ] COMMAND")

    @use_language("c++")
    def invoke(self, arg, from_tty):
        self.dont_repeat()
        argv = gdb.string_to_argv(arg)

        if not argv:
            return self.usage()

        # parse fiber ids and command
        targets = []
        if argv[0] == "all":
            argv.pop(0)  # use empty filter for all fibers
        else:
            while argv and argv[0][0].isdigit():
                targets.append(argv.pop(0))
        command = " ".join(argv)
        if not argv:
            return self.usage()

        # save original fiber state
        orig = FiberUnwinder.get_fiber()
        try:
            # loop over, activating and running command in turn
            for _, info in get_fiber_info(targets):
                print("Fiber {id} ({info})".format(id=info.id, info=info))
                fiber_activate(info.fiber)
                gdb.execute(command, from_tty=from_tty)
        finally:
            # restore original fiber state
            FiberUnwinder.set_fiber(orig)


class FiberDeactivateCommand(gdb.Command):
    """Deactivates fiber processing, returning to normal thread processing"""

    def __init__(self):
        super(FiberDeactivateCommand, self).__init__(
            "fiber deactivate", gdb.COMMAND_USER
        )

    def invoke(self, arg, from_tty):
        if arg:
            print("fiber deactivate takes no arguments.")
            return
        print(fiber_deactivate())


#
# Parameters
#


class SetFiberCommand(gdb.Command):
    """Generic command for setting how fibers are handled"""

    def __init__(self):
        super(SetFiberCommand, self).__init__(
            "set fiber", gdb.COMMAND_DATA, prefix=True
        )

    def invoke(self, arg, from_tty):
        print('"set fiber" must be followed by the name of a fiber subcommand')
        gdb.execute("help set fiber")


class ShowFiberCommand(gdb.Command):
    """Generic command for showing fiber settings"""

    REGISTRY = set()

    def __init__(self):
        super(ShowFiberCommand, self).__init__(
            "show fiber", gdb.COMMAND_DATA, prefix=True
        )

    def invoke(self, arg, from_tty):
        for name in sorted(self.REGISTRY):
            print(
                "{name}: {value}".format(
                    name=name, value=gdb.parameter("fiber " + name)
                )
            )


class FiberParameter(gdb.Parameter):
    """track fiber parameters to print all on "show fiber" command"""

    def __init__(self, name, *args, **kwds):
        super(FiberParameter, self).__init__(name, *args, **kwds)
        (category, _, parameter) = name.partition(" ")
        assert category == "fiber"
        ShowFiberCommand.REGISTRY.add(parameter)


class FiberManagerPrintLimitParameter(FiberParameter):
    """Manage the limit of fibers displayed when printing a FiberManager"""

    show_doc = "Show limit of fibers of a fiber manager to print"
    set_doc = "Set limit of fibers of a fiber manager to print"

    def __init__(self):
        super(FiberManagerPrintLimitParameter, self).__init__(
            "fiber manager-print-limit", gdb.COMMAND_DATA, gdb.PARAM_UINTEGER
        )
        self.value = 100


class FiberInfoFrameSkipWordsParameter(FiberParameter):
    """Manage which frames are skipped when showing fiber info.

    Usage: set fiber info-frame-skip-words [ WORD [ ... ] ]

    Space-separated list of strings that, if present in the function name, will
    cause "info fibers" to skip that frame when printing current location. Set
    to no words to disable behavior and always pring the topmost frame.

    Default: folly::fibers wait
    """

    show_doc = "Show words to skip frames of in fiber info printing"
    set_doc = "Set words to skip frames of in fiber info printing"

    def __init__(self):
        super(FiberInfoFrameSkipWordsParameter, self).__init__(
            "fiber info-frame-skip-words", gdb.COMMAND_DATA, gdb.PARAM_STRING
        )
        self.value = "folly::fibers wait"


class Shortcut(gdb.Function):
    def __init__(self, function_name, value_lambda):
        super(Shortcut, self).__init__(function_name)
        self.value_lambda = value_lambda

    def invoke(self, *args):
        return self.value_lambda(*args)


#
# Internal
#


# This class is responsible for maintaining the name/address:fiberinfo mapping.
# Creating a FiberInfo object adds it to the cache, and trying to create one
# for a cached fiber will just return the same cached FiberInfo.
class FiberInfo:

    NAMES = {}

    # Lookup of fiber address/name to (mid, fid)
    def __new__(cls, fiber):
        # return cached info object if it exists
        cached = cls.NAMES.get(str(fiber.address), None)
        if cached:
            return cached

        # otherwise create a new one
        obj = super(FiberInfo, cls).__new__(cls)
        obj.fiber = fiber
        obj._name = ""
        obj.mid = None
        obj.fid = None

        # make sure it's in the cache
        obj.NAMES[str(fiber.address)] = obj
        return obj

    @property
    def id(self):
        return "{}.{}".format(self.mid or "?", self.fid or "?")

    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, name):
        if self._name:
            # clear any existing name in cache
            del self.NAMES[self._name]
        if name:
            # clear any old holder of the name
            if name in self.NAMES:
                self.NAMES[name]._name = ""
            # update cache to point to us
            self.NAMES[name] = self
        # update the name
        self._name = name or ""

    @classmethod
    def by_name(cls, name):
        return cls.NAMES.get(name, None)

    def __repr__(self):
        return 'FiberInfo(address={}, id={}, name="{}")'.format(
            self.fiber.address, self.id, self.name
        )

    def __str__(self):
        return "Fiber {address} ({state}){name}".format(
            address=self.fiber.address,
            state=FiberPrinter(self.fiber).state_to_string(),
            name=' "{}"'.format(self._name) if self._name else "",
        )

    def __eq__(self, other):
        return self.fiber.address == other.fiber.address


def fiber_activate(fiber):
    return FiberUnwinder.set_fiber(fiber)


def fiber_deactivate():
    return FiberUnwinder.set_fiber(None)


def get_fiber(fid):
    """return a fiber for the given id, name, or address, or None"""
    # check if it's a named fiber
    info = FiberInfo.by_name(fid)
    if info:
        return info.fiber

    # next check if it's an assigned id
    if "." in fid:
        try:
            # Just grab the first result and return
            return next(get_fiber_info([fid]))[1].fiber
        except IndexError:
            return None

    # finally assume it's an address to a fiber
    try:
        return (
            gdb.parse_and_eval(fid)
            .cast(gdb.lookup_type("folly::fibers::Fiber").pointer())
            .dereference()
        )
    except gdb.error:
        return None


def get_fiber_info(only=None, managers=False):
    """iterator of tuples of ((mid, fid), info)

    Can filter output with "M.F" or "M" format, e.g. "2.3" for manager 2 fiber 3
    or "1" for all fibers from manager 1.  If "fid" is None, then the value is
    a gdb.Value of a fiber manager, and if it's non-None, then it's of a FiberInfo.
    Guaranteed to return values in ascending order, managers before their fibers.
    Manager output only occurs when `managers` is set to True.

    Implemented as iterators for interactive speed purposes. A binary can have
    very many fibers, so we don't want to process everything before printing
    some data to the user.  Results of full fiber managers are cached.
    """
    for mid, manager in get_fiber_managers(only):
        if managers:
            yield ((mid, None), manager)

        # first check if pre-cached
        if mid in get_fiber_info.cache:
            for fid, info in get_fiber_info.cache[mid].items():
                fiber_id = "{}.{}".format(mid, fid)
                if not only or str(mid) in only or fiber_id in only:
                    yield ((mid, fid), info)
            continue

        # list fibers from the manager
        fibers = collections.OrderedDict()
        fid = 1
        for fiber in fiber_manager_active_fibers(manager):
            # look up/create cached info
            info = FiberInfo(fiber)
            # associate mid.fid with info
            info.mid = mid
            info.fid = fid
            fibers[fid] = info
            # output only if matching the filter
            fiber_id = "{}.{}".format(mid, fid)
            if not only or str(mid) in only or fiber_id in only:
                yield ((mid, fid), info)
            fid += 1
        get_fiber_info.cache[mid] = fibers


# Cache of {mid: {fid: FiberInfo}}
# If the mid is there, then all of the fibers for it are there
get_fiber_info.cache = collections.OrderedDict()


def get_fiber_managers(only=None):
    """iterator of (mid, manager) tuples

    Can filter output with "M.*" or "M" format, e.g. "2.3" for manager 2 (fiber is
    ignored) or "1" for manager 1.
    """
    # first check if pre-cached
    if get_fiber_managers.cache:
        for mid, manager in get_fiber_managers.cache.items():
            # output only if matching filter
            if not only or str(mid) in only:
                yield (mid, manager)
        return

    # extract the unique managers from the fiber filters
    only = {i.partition(".")[0] for i in only or []}
    managers = collections.OrderedDict()
    mgr_map = None
    for evb_type in ("folly::EventBase", "folly::VirtualEventBase"):
        # this can possibly return an empty map, even if it exists
        try:
            mgr_map = get_fiber_manager_map(evb_type)
        except gdb.GdbError:
            continue
        # the map pretty printer knows how to extract map entries
        map_pp = gdb.default_visualizer(mgr_map)

        # The children are alternating pairs of (_, (evb, int))/(_, uptr<FiberManager>)
        # the first entry is irrelevant, just an internal name for the pretty printer
        mid = 0

        for _, entry in map_pp.children():
            # The "key" in this map is std::pair<EventBaseT, long>, and the long is
            # almost always 0 (used for frozen options).  We'll ignore it and just use
            # our own internal id.  We unwrap the unique_ptr, though.
            if "unique_ptr" not in entry.type.tag:
                mid += 1
                continue

            # we have a value, make sure we have a unique key
            assert mid not in managers
            value = gdb.default_visualizer(entry)

            # Before GCC9, the stl gdb libs don't expose the unique_ptr target
            # address except through the pretty printer, as the last
            # space-delimited word. From GCC9 forward, the unique_ptr visualizer
            # exposes a children iterator with the target address. We extract
            # that address whicever way we can, then create a new gdb.Value of
            # that address cast to the fibermanager type.
            address = None
            if callable(getattr(value, "children", None)):
                for _, pointer in value.children():
                    address = pointer
            else:
                address = int(value.to_string().split(" ")[-1], 16)

            if address is not None:
                manager = (
                    gdb.Value(address)
                    .cast(gdb.lookup_type("folly::fibers::FiberManager").pointer())
                    .dereference()
                )

                # output only if matching filter
                if not only or str(mid) in only:
                    yield (mid, manager)

        # set cache
        get_fiber_managers.cache = managers


# Cache of {mid: manager}
get_fiber_managers.cache = collections.OrderedDict()


def fiber_manager_active_fibers(fm):
    """iterator of Fiber values for a given fiber manager"""
    all_fibers = fm["allFibers_"]["data_"]["root_plus_size_"]["m_header"]
    fiber_hook = all_fibers["next_"]

    while fiber_hook != all_fibers.address:
        fiber = fiber_hook.cast(gdb.lookup_type("int64_t"))
        fiber = fiber - gdb.parse_and_eval(
            "(int64_t)&'folly::fibers::Fiber'::globalListHook_"
        )
        fiber = fiber.cast(
            gdb.lookup_type("folly::fibers::Fiber").pointer()
        ).dereference()

        if FiberPrinter(fiber).state != "folly::fibers::Fiber::INVALID":
            yield fiber

        fiber_hook = fiber_hook.dereference()["next_"]


def get_fiber_manager_map(evb_type):
    # global cache was moved from anonymous namespace to "detail", we want to
    # work in both cases.
    for ns in ("detail", "(anonymous namespace)"):
        try:
            # Exception thrown if unable to find type
            global_cache_type = gdb.lookup_type(
                "folly::fibers::{ns}::GlobalCache<{evb_type}>".format(
                    ns=ns, evb_type=evb_type
                )
            )
            break
        except gdb.error:
            pass
    else:
        raise gdb.GdbError(
            "Unable to find types. "
            "Please make sure debug info is available for this binary.\n"
            "Have you run 'fbload debuginfo_fbpkg'?"
        )

    global_cache_instance_ptr_ptr = gdb.parse_and_eval(
        "&'" + global_cache_type.name + "::instance()::ret'"
    )
    global_cache_instance_ptr = global_cache_instance_ptr_ptr.cast(
        global_cache_type.pointer().pointer()
    ).dereference()
    if global_cache_instance_ptr == 0x0:
        raise gdb.GdbError("FiberManager map is empty.")

    global_cache_instance = global_cache_instance_ptr.dereference()
    return global_cache_instance["map_"]


def get_fiber_manager_map_evb():
    return get_fiber_manager_map("folly::EventBase")


def get_fiber_manager_map_vevb():
    return get_fiber_manager_map("folly::VirtualEventBase")


# reset the caches when we continue; the current fibers will almost certainly change
def clear_fiber_caches(*args):
    # default to only clearing manager and info caches
    caches = {arg.string() for arg in args} or {"managers", "info"}
    cleared = set()
    if "managers" in caches:
        cleared.add("manager")
        get_fiber_managers.cache.clear()
    if "info" in caches:
        cleared.add("info")
        get_fiber_info.cache.clear()
    # This one we may want to keep, so we can reference known fibers by name/address
    # even though their assigned ids will almost certainly change
    if "names" in caches:
        cleared.add("names")
        FiberInfo.NAMES.clear()

    result = "Cleared {}.".format(", ".join(cleared))
    unknown = caches - cleared
    if unknown:
        # print a warning if we asked for any unknown caches to clear
        result += " Skipped unknown " + ", ".join(unknown)
    return result


def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("folly_fibers")
    pp.add_printer("fibers::Fiber", "^folly::fibers::Fiber$", FiberPrinter)
    pp.add_printer(
        "fibers::FiberManager", "^folly::fibers::FiberManager$", FiberManagerPrinter
    )
    return pp


def load():
    gdb.printing.register_pretty_printer(gdb, build_pretty_printer())
    gdb.xmethod.register_xmethod_matcher(gdb, FiberXMethodMatcher())
    gdb.xmethod.register_xmethod_matcher(gdb, GetFiberXMethodMatcher())
    FiberFrameFilter()
    SetFiberCommand()
    ShowFiberCommand()
    FiberManagerPrintLimitParameter()
    FiberInfoFrameSkipWordsParameter()
    FiberCommand()
    FiberDeactivateCommand()
    FiberInfoCommand()
    FiberApplyCommand()
    FiberNameCommand()
    Shortcut("get_fiber_manager_map_evb", get_fiber_manager_map_evb)
    Shortcut("get_fiber_manager_map_vevb", get_fiber_manager_map_vevb)
    Shortcut("clear_fiber_caches", lambda _: clear_fiber_caches())
    gdb.events.cont.connect(lambda _: clear_fiber_caches())


def info():
    return "Pretty printers for folly::fibers"
