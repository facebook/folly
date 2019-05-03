#!/usr/bin/env python
# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.


from __future__ import absolute_import, division, print_function, unicode_literals

import argparse
import os
import subprocess
import sys

from getdeps.buildopts import setup_build_options
from getdeps.load import resolve_manifest_path
from getdeps.manifest import ManifestParser
from getdeps.platform import HostType
from getdeps.subcmd import SubCmd, add_subcommands, cmd


sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "getdeps"))


@cmd("validate-manifest", "parse a manifest and validate that it is correct")
class ValidateManifest(SubCmd):
    def run(self, args):
        try:
            ManifestParser(file_name=args.file_name)
            print("OK", file=sys.stderr)
            return 0
        except Exception as exc:
            print("ERROR: %s" % str(exc), file=sys.stderr)
            return 1

    def setup_parser(self, parser):
        parser.add_argument("file_name", help="path to the manifest file")


@cmd("show-host-type", "outputs the host type tuple for the host machine")
class ShowHostType(SubCmd):
    def run(self, args):
        host = HostType()
        print("%s" % host.as_tuple_string())
        return 0


@cmd("fetch", "fetch the code for a given project")
class FetchCmd(SubCmd):
    def setup_parser(self, parser):
        parser.add_argument(
            "project",
            help=(
                "name of the project or path to a manifest "
                "file describing the project"
            ),
        )

    def run(self, args):
        opts = setup_build_options(args)
        manifest_path = resolve_manifest_path(opts, args.project)
        manifest = ManifestParser(manifest_path)
        fetcher = manifest.create_fetcher(opts, ctx={})
        fetcher.update()


def build_argparser():
    common_args = argparse.ArgumentParser(add_help=False)
    common_args.add_argument(
        "--scratch-path", help="Where to maintain checkouts and build dirs"
    )
    common_args.add_argument(
        "--install-prefix",
        help=(
            "Where the final build products will be installed "
            "(default is [scratch-path]/installed)"
        ),
    )
    common_args.add_argument(
        "--num-jobs",
        type=int,
        help=(
            "Number of concurrent jobs to use while building. "
            "(default=number of cpu cores)"
        ),
    )

    ap = argparse.ArgumentParser(
        description="Get and build dependencies and projects", parents=[common_args]
    )
    sub = ap.add_subparsers(
        # metavar suppresses the long and ugly default list of subcommands on a
        # single line.  We still render the nicer list below where we would
        # have shown the nasty one.
        metavar="",
        title="Available commands",
        help="",
    )

    add_subcommands(sub, common_args)

    return ap


def main():
    ap = build_argparser()
    args = ap.parse_args()
    if getattr(args, "func", None) is None:
        ap.print_help()
        return 0
    try:
        return args.func(args)
    except subprocess.CalledProcessError:
        print("!! Failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
