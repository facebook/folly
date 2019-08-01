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
import shutil
import subprocess
import sys

from getdeps.buildopts import setup_build_options
from getdeps.dyndeps import create_dyn_dep_munger
from getdeps.errors import TransientFailure
from getdeps.load import load_project, manifests_in_dependency_order
from getdeps.manifest import ManifestParser
from getdeps.platform import HostType
from getdeps.subcmd import SubCmd, add_subcommands, cmd


try:
    import getdeps.facebook  # noqa: F401
except ImportError:
    # we don't ship the facebook specific subdir,
    # so allow that to fail silently
    pass


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
        parser.add_argument(
            "--recursive",
            help="fetch the transitive deps also",
            action="store_true",
            default=False,
        )
        parser.add_argument(
            "--host-type",
            help=(
                "When recursively fetching, fetch deps for "
                "this host type rather than the current system"
            ),
        )

    def run(self, args):
        opts = setup_build_options(args)
        ctx_gen = opts.get_context_generator()
        manifest = load_project(opts, args.project)
        if args.recursive:
            projects = manifests_in_dependency_order(opts, manifest, ctx_gen)
        else:
            projects = [manifest]
        for m in projects:
            fetcher = m.create_fetcher(opts, ctx_gen.get_context(m.name))
            fetcher.update()


@cmd("list-deps", "lists the transitive deps for a given project")
class ListDepsCmd(SubCmd):
    def run(self, args):
        opts = setup_build_options(args)
        ctx_gen = opts.get_context_generator()
        ctx_gen.set_value_for_project(args.project, "test", "on")
        manifest = load_project(opts, args.project)
        for m in manifests_in_dependency_order(opts, manifest, ctx_gen):
            print(m.name)
        return 0

    def setup_parser(self, parser):
        parser.add_argument(
            "--host-type",
            help=(
                "Produce the list for the specified host type, "
                "rather than that of the current system"
            ),
        )
        parser.add_argument(
            "project",
            help=(
                "name of the project or path to a manifest "
                "file describing the project"
            ),
        )


def clean_dirs(opts):
    for d in ["build", "installed", "extracted", "shipit"]:
        d = os.path.join(opts.scratch_dir, d)
        print("Cleaning %s..." % d)
        if os.path.exists(d):
            shutil.rmtree(d)


@cmd("clean", "clean up the scratch dir")
class CleanCmd(SubCmd):
    def run(self, args):
        opts = setup_build_options(args)
        clean_dirs(opts)


@cmd("show-inst-dir", "print the installation dir for a given project")
class ShowInstDirCmd(SubCmd):
    def run(self, args):
        opts = setup_build_options(args)
        ctx_gen = opts.get_context_generator()
        ctx_gen.set_value_for_project(args.project, "test", "on")
        manifest = load_project(opts, args.project)
        projects = manifests_in_dependency_order(opts, manifest, ctx_gen)
        manifests_by_name = {m.name: m for m in projects}

        if args.recursive:
            manifests = projects
        else:
            manifests = [manifest]

        for m in manifests:
            ctx = ctx_gen.get_context(m.name)
            fetcher = m.create_fetcher(opts, ctx)
            dirs = opts.compute_dirs(m, fetcher, manifests_by_name, ctx)
            inst_dir = dirs["inst_dir"]
            print(inst_dir)

    def setup_parser(self, parser):
        parser.add_argument(
            "project",
            help=(
                "name of the project or path to a manifest "
                "file describing the project"
            ),
        )
        parser.add_argument(
            "--recursive",
            help="print the transitive deps also",
            action="store_true",
            default=False,
        )


@cmd("show-source-dir", "print the source dir for a given project")
class ShowSourceDirCmd(SubCmd):
    def run(self, args):
        opts = setup_build_options(args)
        ctx_gen = opts.get_context_generator()
        ctx_gen.set_value_for_project(args.project, "test", "on")
        manifest = load_project(opts, args.project)

        if args.recursive:
            manifests = manifests_in_dependency_order(opts, manifest, ctx_gen)
        else:
            manifests = [manifest]

        for m in manifests:
            fetcher = m.create_fetcher(opts, ctx_gen.get_context(m.name))
            print(fetcher.get_src_dir())

    def setup_parser(self, parser):
        parser.add_argument(
            "project",
            help=(
                "name of the project or path to a manifest "
                "file describing the project"
            ),
        )
        parser.add_argument(
            "--recursive",
            help="print the transitive deps also",
            action="store_true",
            default=False,
        )


@cmd("build", "build a given project")
class BuildCmd(SubCmd):
    def run(self, args):
        opts = setup_build_options(args)
        if args.clean:
            clean_dirs(opts)

        ctx_gen = opts.get_context_generator(facebook_internal=args.facebook_internal)
        if args.enable_tests:
            ctx_gen.set_value_for_project(args.project, "test", "on")
        manifest = load_project(opts, args.project)

        print("Building on %s" % ctx_gen.get_context(args.project))
        projects = manifests_in_dependency_order(opts, manifest, ctx_gen)
        manifests_by_name = {m.name: m for m in projects}

        # Accumulate the install directories so that the build steps
        # can find their dep installation
        install_dirs = []

        for m in projects:
            ctx = ctx_gen.get_context(m.name)
            fetcher = m.create_fetcher(opts, ctx)

            if args.clean:
                fetcher.clean()

            dirs = opts.compute_dirs(m, fetcher, manifests_by_name, ctx)
            build_dir = dirs["build_dir"]
            inst_dir = dirs["inst_dir"]

            if m == manifest or not args.no_deps:
                print("Assessing %s..." % m.name)
                change_status = fetcher.update()
                reconfigure = change_status.build_changed()
                sources_changed = change_status.sources_changed()

                built_marker = os.path.join(inst_dir, ".built-by-getdeps")
                if os.path.exists(built_marker):
                    with open(built_marker, "r") as f:
                        built_hash = f.read().strip()
                    if built_hash != dirs["hash"]:
                        # Some kind of inconsistency with a prior build,
                        # let's run it again to be sure
                        os.unlink(built_marker)
                        reconfigure = True

                if sources_changed or reconfigure or not os.path.exists(built_marker):
                    if os.path.exists(built_marker):
                        os.unlink(built_marker)
                    src_dir = fetcher.get_src_dir()
                    builder = m.create_builder(opts, src_dir, build_dir, inst_dir, ctx)
                    builder.build(install_dirs, reconfigure=reconfigure)

                    with open(built_marker, "w") as f:
                        f.write(dirs["hash"])

            install_dirs.append(inst_dir)

    def setup_parser(self, parser):
        parser.add_argument(
            "project",
            help=(
                "name of the project or path to a manifest "
                "file describing the project"
            ),
        )
        parser.add_argument(
            "--clean",
            action="store_true",
            default=False,
            help=(
                "Clean up the build and installation area prior to building, "
                "causing the projects to be built from scratch"
            ),
        )
        parser.add_argument(
            "--no-deps",
            action="store_true",
            default=False,
            help=(
                "Only build the named project, not its deps. "
                "This is most useful after you've built all of the deps, "
                "and helps to avoid waiting for relatively "
                "slow up-to-date-ness checks"
            ),
        )
        parser.add_argument(
            "--enable-tests",
            action="store_true",
            default=False,
            help=(
                "For the named project, build tests so that the test command "
                "is able to execute tests"
            ),
        )
        parser.add_argument(
            "--schedule-type", help="Indicates how the build was activated"
        )


@cmd("fixup-dyn-deps", "Adjusts dynamic dependencies for packaging purposes")
class FixupDeps(SubCmd):
    def run(self, args):
        opts = setup_build_options(args)
        ctx_gen = opts.get_context_generator(facebook_internal=args.facebook_internal)
        if args.enable_tests:
            ctx_gen.set_value_for_project(args.project, "test", "on")

        manifest = load_project(opts, args.project)

        projects = manifests_in_dependency_order(opts, manifest, ctx_gen)
        manifests_by_name = {m.name: m for m in projects}

        # Accumulate the install directories so that the build steps
        # can find their dep installation
        install_dirs = []

        for m in projects:
            ctx = ctx_gen.get_context(m.name)
            fetcher = m.create_fetcher(opts, ctx)

            dirs = opts.compute_dirs(m, fetcher, manifests_by_name, ctx)
            inst_dir = dirs["inst_dir"]

            install_dirs.append(inst_dir)

            if m == manifest:
                dep_munger = create_dyn_dep_munger(opts, install_dirs)
                dep_munger.process_deps(args.destdir, args.final_install_prefix)

    def setup_parser(self, parser):
        parser.add_argument(
            "project",
            help=(
                "name of the project or path to a manifest "
                "file describing the project"
            ),
        )
        parser.add_argument("destdir", help=("Where to copy the fixed up executables"))
        parser.add_argument(
            "--final-install-prefix", help=("specify the final installation prefix")
        )
        parser.add_argument(
            "--enable-tests",
            action="store_true",
            default=False,
            help=(
                "For the named project, build tests so that the test command "
                "is able to execute tests"
            ),
        )
        parser.add_argument(
            "--schedule-type", help="Indicates how the build was activated"
        )


@cmd("test", "test a given project")
class TestCmd(SubCmd):
    def run(self, args):
        opts = setup_build_options(args)
        ctx_gen = opts.get_context_generator(facebook_internal=args.facebook_internal)
        if args.test_all:
            ctx_gen.set_value_for_all_projects("test", "on")
        else:
            ctx_gen.set_value_for_project(args.project, "test", "on")

        manifest = load_project(opts, args.project)
        projects = manifests_in_dependency_order(opts, manifest, ctx_gen)
        manifests_by_name = {m.name: m for m in projects}

        # Accumulate the install directories so that the test steps
        # can find their dep installation
        install_dirs = []

        for m in projects:
            ctx = ctx_gen.get_context(m.name)
            fetcher = m.create_fetcher(opts, ctx)

            dirs = opts.compute_dirs(m, fetcher, manifests_by_name, ctx)
            build_dir = dirs["build_dir"]
            inst_dir = dirs["inst_dir"]

            if m == manifest or args.test_all:
                built_marker = os.path.join(inst_dir, ".built-by-getdeps")
                if not os.path.exists(built_marker):
                    print("project %s has not been built" % m.name)
                    # TODO: we could just go ahead and build it here, but I
                    # want to tackle that as part of adding build-for-test
                    # support.
                    return 1
                src_dir = fetcher.get_src_dir()
                builder = m.create_builder(opts, src_dir, build_dir, inst_dir, ctx)
                builder.run_tests(install_dirs, schedule_type=args.schedule_type)

            install_dirs.append(inst_dir)

    def setup_parser(self, parser):
        parser.add_argument(
            "project",
            help=(
                "name of the project or path to a manifest "
                "file describing the project"
            ),
        )
        parser.add_argument(
            "--test-all",
            action="store_true",
            default=False,
            help="Enable running tests for the named project and all of its deps",
        )
        parser.add_argument(
            "--schedule-type", help="Indicates how the build was activated"
        )


def get_arg_var_name(args):
    for arg in args:
        if arg.startswith("--"):
            return arg[2:].replace("-", "_")

    raise Exception("unable to determine argument variable name from %r" % (args,))


def parse_args():
    # We want to allow common arguments to be specified either before or after
    # the subcommand name.  In order to do this we add them to the main parser
    # and to subcommand parsers.  In order for this to work, we need to tell
    # argparse that the default value is SUPPRESS, so that the default values
    # from the subparser arguments won't override values set by the user from
    # the main parser.  We maintain our own list of desired defaults in the
    # common_defaults dictionary, and manually set those if the argument wasn't
    # present at all.
    common_args = argparse.ArgumentParser(add_help=False)
    common_defaults = {}

    def add_common_arg(*args, **kwargs):
        var_name = get_arg_var_name(args)
        default_value = kwargs.pop("default", None)
        common_defaults[var_name] = default_value
        kwargs["default"] = argparse.SUPPRESS
        common_args.add_argument(*args, **kwargs)

    add_common_arg("--scratch-path", help="Where to maintain checkouts and build dirs")
    add_common_arg(
        "--vcvars-path", default=None, help="Path to the vcvarsall.bat on Windows."
    )
    add_common_arg(
        "--install-prefix",
        help=(
            "Where the final build products will be installed "
            "(default is [scratch-path]/installed)"
        ),
    )
    add_common_arg(
        "--num-jobs",
        type=int,
        help=(
            "Number of concurrent jobs to use while building. "
            "(default=number of cpu cores)"
        ),
    )
    add_common_arg(
        "--use-shipit",
        help="use the real ShipIt instead of the simple shipit transformer",
        action="store_true",
        default=False,
    )
    add_common_arg(
        "--facebook-internal",
        help="Setup the build context as an FB internal build",
        action="store_true",
        default=False,
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

    args = ap.parse_args()
    for var_name, default_value in common_defaults.items():
        if not hasattr(args, var_name):
            setattr(args, var_name, default_value)

    return ap, args


def main():
    ap, args = parse_args()
    if getattr(args, "func", None) is None:
        ap.print_help()
        return 0
    try:
        return args.func(args)
    except TransientFailure as exc:
        print("TransientFailure: %s" % str(exc))
        # This return code is treated as a retryable transient infrastructure
        # error by Facebook's internal CI, rather than eg: a build or code
        # related error that needs to be fixed before progress can be made.
        return 128
    except subprocess.CalledProcessError as exc:
        print("%s" % str(exc), file=sys.stderr)
        print("!! Failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
