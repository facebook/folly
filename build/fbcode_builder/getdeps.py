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
import tarfile
import tempfile

# We don't import cache.create_cache directly as the facebook
# specific import below may monkey patch it, and we want to
# observe the patched version of this function!
import getdeps.cache as cache_module
from getdeps.buildopts import setup_build_options
from getdeps.dyndeps import create_dyn_dep_munger
from getdeps.errors import TransientFailure
from getdeps.load import ManifestLoader
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


class UsageError(Exception):
    pass


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


class ProjectCmdBase(SubCmd):
    def run(self, args):
        opts = setup_build_options(args)
        ctx_gen = opts.get_context_generator(facebook_internal=args.facebook_internal)
        if args.test_dependencies:
            ctx_gen.set_value_for_all_projects("test", "on")
        if args.enable_tests:
            ctx_gen.set_value_for_project(args.project, "test", "on")
        else:
            ctx_gen.set_value_for_project(args.project, "test", "off")

        loader = ManifestLoader(opts, ctx_gen)
        self.process_project_dir_arguments(args, loader)

        manifest = loader.load_manifest(args.project)

        self.run_project_cmd(args, loader, manifest)

    def process_project_dir_arguments(self, args, loader):
        def parse_project_arg(arg, arg_type):
            parts = arg.split(":")
            if len(parts) == 2:
                project, path = parts
            elif len(parts) == 1:
                project = args.project
                path = parts[0]
            else:
                raise UsageError(
                    "invalid %s argument; too many ':' characters: %s" % (arg_type, arg)
                )

            return project, os.path.abspath(path)

        for arg in args.src_dir:
            project, path = parse_project_arg(arg, "--src-dir")
            loader.set_project_src_dir(project, path)

        for arg in args.build_dir:
            project, path = parse_project_arg(arg, "--build-dir")
            loader.set_project_build_dir(project, path)

        for arg in args.install_dir:
            project, path = parse_project_arg(arg, "--install-dir")
            loader.set_project_install_dir(project, path)

    def setup_parser(self, parser):
        parser.add_argument(
            "project",
            help=(
                "name of the project or path to a manifest "
                "file describing the project"
            ),
        )
        parser.add_argument(
            "--no-tests",
            action="store_false",
            dest="enable_tests",
            default=True,
            help="Disable building tests for this project.",
        )
        parser.add_argument(
            "--test-dependencies",
            action="store_true",
            help="Enable building tests for dependencies as well.",
        )
        parser.add_argument(
            "--src-dir",
            default=[],
            action="append",
            help="Specify a local directory to use for the project source, "
            "rather than fetching it.",
        )
        parser.add_argument(
            "--build-dir",
            default=[],
            action="append",
            help="Explicitly specify the build directory to use for the "
            "project, instead of the default location in the scratch path. "
            "This only affects the project specified, and not its dependencies.",
        )
        parser.add_argument(
            "--install-dir",
            default=[],
            action="append",
            help="Explicitly specify the install directory to use for the "
            "project, instead of the default location in the scratch path. "
            "This only affects the project specified, and not its dependencies.",
        )

        self.setup_project_cmd_parser(parser)

    def setup_project_cmd_parser(self, parser):
        pass


class CachedProject(object):
    """ A helper that allows calling the cache logic for a project
    from both the build and the fetch code """

    def __init__(self, cache, loader, m):
        self.m = m
        self.inst_dir = loader.get_project_install_dir(m)
        self.project_hash = loader.get_project_hash(m)
        self.ctx = loader.ctx_gen.get_context(m.name)
        self.loader = loader
        self.cache = cache

        self.cache_file_name = "-".join(
            (
                m.name,
                self.ctx.get("os"),
                self.ctx.get("distro") or "none",
                self.ctx.get("distro_vers") or "none",
                self.project_hash,
                "buildcache.tgz",
            )
        )

    def is_cacheable(self):
        """ We only cache third party projects """
        return self.cache and not self.m.shipit_fbcode_builder

    def download(self):
        if self.is_cacheable() and not os.path.exists(self.inst_dir):
            print("check cache for %s" % self.cache_file_name)
            dl_dir = os.path.join(self.loader.build_opts.scratch_dir, "downloads")
            if not os.path.exists(dl_dir):
                os.makedirs(dl_dir)
            try:
                target_file_name = os.path.join(dl_dir, self.cache_file_name)
                if self.cache.download_to_file(self.cache_file_name, target_file_name):
                    tf = tarfile.open(target_file_name, "r")
                    print(
                        "Extracting %s -> %s..." % (self.cache_file_name, self.inst_dir)
                    )
                    tf.extractall(self.inst_dir)
                    return True
            except Exception as exc:
                print("%s" % str(exc))

        return False

    def upload(self):
        if self.cache and not self.m.shipit_fbcode_builder:
            # We can prepare an archive and stick it in LFS
            tempdir = tempfile.mkdtemp()
            tarfilename = os.path.join(tempdir, self.cache_file_name)
            print("Archiving for cache: %s..." % tarfilename)
            tf = tarfile.open(tarfilename, "w:gz")
            tf.add(self.inst_dir, arcname=".")
            tf.close()
            try:
                self.cache.upload_from_file(self.cache_file_name, tarfilename)
            except Exception as exc:
                print(
                    "Failed to upload to cache (%s), continue anyway" % str(exc),
                    file=sys.stderr,
                )
            shutil.rmtree(tempdir)


@cmd("fetch", "fetch the code for a given project")
class FetchCmd(ProjectCmdBase):
    def setup_project_cmd_parser(self, parser):
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

    def run_project_cmd(self, args, loader, manifest):
        if args.recursive:
            projects = loader.manifests_in_dependency_order()
        else:
            projects = [manifest]

        cache = cache_module.create_cache()
        for m in projects:
            cached_project = CachedProject(cache, loader, m)
            if cached_project.download():
                continue

            inst_dir = loader.get_project_install_dir(m)
            built_marker = os.path.join(inst_dir, ".built-by-getdeps")
            if os.path.exists(built_marker):
                with open(built_marker, "r") as f:
                    built_hash = f.read().strip()

                project_hash = loader.get_project_hash(m)
                if built_hash == project_hash:
                    continue

            # We need to fetch the sources
            fetcher = loader.create_fetcher(m)
            fetcher.update()


@cmd("list-deps", "lists the transitive deps for a given project")
class ListDepsCmd(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        for m in loader.manifests_in_dependency_order():
            print(m.name)
        return 0

    def setup_project_cmd_parser(self, parser):
        parser.add_argument(
            "--host-type",
            help=(
                "Produce the list for the specified host type, "
                "rather than that of the current system"
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
class ShowInstDirCmd(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        if args.recursive:
            manifests = loader.manifests_in_dependency_order()
        else:
            manifests = [manifest]

        for m in manifests:
            inst_dir = loader.get_project_install_dir(m)
            print(inst_dir)

    def setup_project_cmd_parser(self, parser):
        parser.add_argument(
            "--recursive",
            help="print the transitive deps also",
            action="store_true",
            default=False,
        )


@cmd("show-source-dir", "print the source dir for a given project")
class ShowSourceDirCmd(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        if args.recursive:
            manifests = loader.manifests_in_dependency_order()
        else:
            manifests = [manifest]

        for m in manifests:
            fetcher = loader.create_fetcher(m)
            print(fetcher.get_src_dir())

    def setup_project_cmd_parser(self, parser):
        parser.add_argument(
            "--recursive",
            help="print the transitive deps also",
            action="store_true",
            default=False,
        )


@cmd("build", "build a given project")
class BuildCmd(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        if args.clean:
            clean_dirs(loader.build_opts)

        print("Building on %s" % loader.ctx_gen.get_context(args.project))
        projects = loader.manifests_in_dependency_order()

        cache = cache_module.create_cache()

        # Accumulate the install directories so that the build steps
        # can find their dep installation
        install_dirs = []

        for m in projects:
            fetcher = loader.create_fetcher(m)

            if args.clean:
                fetcher.clean()

            build_dir = loader.get_project_build_dir(m)
            inst_dir = loader.get_project_install_dir(m)

            if m == manifest or not args.no_deps:
                print("Assessing %s..." % m.name)
                project_hash = loader.get_project_hash(m)
                ctx = loader.ctx_gen.get_context(m.name)
                built_marker = os.path.join(inst_dir, ".built-by-getdeps")

                cached_project = CachedProject(cache, loader, m)

                reconfigure, sources_changed = self.compute_source_change_status(
                    cached_project, fetcher, m, built_marker, project_hash
                )

                if sources_changed or reconfigure or not os.path.exists(built_marker):
                    if os.path.exists(built_marker):
                        os.unlink(built_marker)
                    src_dir = fetcher.get_src_dir()
                    builder = m.create_builder(
                        loader.build_opts, src_dir, build_dir, inst_dir, ctx
                    )
                    builder.build(install_dirs, reconfigure=reconfigure)

                    with open(built_marker, "w") as f:
                        f.write(project_hash)

                    # Only populate the cache from continuous build runs
                    if args.schedule_type == "continuous":
                        cached_project.upload()

            install_dirs.append(inst_dir)

    def compute_source_change_status(
        self, cached_project, fetcher, m, built_marker, project_hash
    ):
        reconfigure = False
        sources_changed = False
        if not cached_project.download():
            check_fetcher = True
            if os.path.exists(built_marker):
                check_fetcher = False
                with open(built_marker, "r") as f:
                    built_hash = f.read().strip()
                if built_hash == project_hash:
                    if cached_project.is_cacheable():
                        # We can blindly trust the build status
                        reconfigure = False
                        sources_changed = False
                    else:
                        # Otherwise, we may have changed the source, so let's
                        # check in with the fetcher layer
                        check_fetcher = True
                else:
                    # Some kind of inconsistency with a prior build,
                    # let's run it again to be sure
                    os.unlink(built_marker)
                    reconfigure = True
                    sources_changed = True

            if check_fetcher:
                change_status = fetcher.update()
                reconfigure = change_status.build_changed()
                sources_changed = change_status.sources_changed()

        return reconfigure, sources_changed

    def setup_project_cmd_parser(self, parser):
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
            "--schedule-type", help="Indicates how the build was activated"
        )


@cmd("fixup-dyn-deps", "Adjusts dynamic dependencies for packaging purposes")
class FixupDeps(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        projects = loader.manifests_in_dependency_order()

        # Accumulate the install directories so that the build steps
        # can find their dep installation
        install_dirs = []

        for m in projects:
            inst_dir = loader.get_project_install_dir(m)
            install_dirs.append(inst_dir)

            if m == manifest:
                dep_munger = create_dyn_dep_munger(loader.build_opts, install_dirs)
                dep_munger.process_deps(args.destdir, args.final_install_prefix)

    def setup_project_cmd_parser(self, parser):
        parser.add_argument("destdir", help=("Where to copy the fixed up executables"))
        parser.add_argument(
            "--final-install-prefix", help=("specify the final installation prefix")
        )


@cmd("test", "test a given project")
class TestCmd(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        projects = loader.manifests_in_dependency_order()

        # Accumulate the install directories so that the test steps
        # can find their dep installation
        install_dirs = []

        for m in projects:
            inst_dir = loader.get_project_install_dir(m)

            if m == manifest or args.test_dependencies:
                built_marker = os.path.join(inst_dir, ".built-by-getdeps")
                if not os.path.exists(built_marker):
                    print("project %s has not been built" % m.name)
                    # TODO: we could just go ahead and build it here, but I
                    # want to tackle that as part of adding build-for-test
                    # support.
                    return 1
                fetcher = loader.create_fetcher(m)
                src_dir = fetcher.get_src_dir()
                ctx = loader.ctx_gen.get_context(m.name)
                build_dir = loader.get_project_build_dir(m)
                builder = m.create_builder(
                    loader.build_opts, src_dir, build_dir, inst_dir, ctx
                )
                builder.run_tests(
                    install_dirs,
                    schedule_type=args.schedule_type,
                    owner=args.test_owner,
                )

            install_dirs.append(inst_dir)

    def setup_project_cmd_parser(self, parser):
        parser.add_argument(
            "--schedule-type", help="Indicates how the build was activated"
        )
        parser.add_argument("--test-owner", help="Owner for testpilot")


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
    except UsageError as exc:
        ap.error(str(exc))
        return 1
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
