# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# pyre-unsafe


class SubCmd(object):
    NAME = None
    HELP = None

    def run(self, args) -> int:
        """perform the command"""
        return 0

    def setup_parser(self, parser) -> None:
        # Subclasses should override setup_parser() if they have any
        # command line options or arguments.
        pass


CmdTable = []


def add_subcommands(parser, common_args, cmd_table=CmdTable) -> None:
    """Register parsers for the defined commands with the provided parser"""
    for cls in cmd_table:
        command = cls()
        command_parser = parser.add_parser(
            command.NAME, help=command.HELP, parents=[common_args]
        )
        command.setup_parser(command_parser)
        command_parser.set_defaults(func=command.run)


def cmd(name, help=None, cmd_table=CmdTable):
    """
    @cmd() is a decorator that can be used to help define Subcmd instances

    Example usage:

        @subcmd('list', 'Show the result list')
        class ListCmd(Subcmd):
            def run(self, args):
                # Perform the command actions here...
                pass
    """

    def wrapper(cls):
        class SubclassedCmd(cls):
            NAME = name
            HELP = help

        cmd_table.append(SubclassedCmd)
        return SubclassedCmd

    return wrapper
