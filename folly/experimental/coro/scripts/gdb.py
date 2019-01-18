#!/usr/bin/env python3


import gdb


class CoroBacktraceCommand(gdb.Command):
    def __init__(self):
        super(CoroBacktraceCommand, self).__init__("co_bt", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        if not arg:
            print("coroutine_handle has to be passed to 'co_bt' command")
            return
        coroutine_handle = gdb.parse_and_eval(arg)
        void_star_star = gdb.lookup_type("void").pointer().pointer()
        coroutine_frame = (
            coroutine_handle.cast(void_star_star).dereference().cast(void_star_star)
        )
        while coroutine_frame < 0xFFFFFFFFFFFF:
            print(coroutine_frame.dereference())
            coroutine_frame = (coroutine_frame + 2).dereference().cast(void_star_star)


def load():
    CoroBacktraceCommand()


def info():
    return "Pretty printers for folly::coro"
