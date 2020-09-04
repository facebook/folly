#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

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
from getdeps.fetcher import (
    SystemPackageFetcher,
    file_name_is_cmake_file,
    list_files_under_dir_newer_than_timestamp,
)
from getdeps.load import ManifestLoader
from getdeps.manifest import ManifestParser
from getdeps.platform import HostType
from getdeps.runcmd import run_cmd
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

        if args.current_project is not None:
            opts.repo_project = args.current_project
        if args.project is None:
            if opts.repo_project is None:
                raise UsageError(
                    "no project name specified, and no .projectid file found"
                )
            if opts.repo_project == "fbsource":
                # The fbsource repository is a little special.  There is no project
                # manifest file for it.  A specific project must always be explicitly
                # specified when building from fbsource.
                raise UsageError(
                    "no project name specified (required when building in fbsource)"
                )
            args.project = opts.repo_project

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

        # If we are currently running from a project repository,
        # use the current repository for the project sources.
        build_opts = loader.build_opts
        if build_opts.repo_project is not None and build_opts.repo_root is not None:
            loader.set_project_src_dir(build_opts.repo_project, build_opts.repo_root)

        for arg in args.src_dir:
            project, path = parse_project_arg(arg, "--src-dir")
            loader.set_project_src_dir(project, path)

        for arg in args.build_dir:
            project, path = parse_project_arg(arg, "--build-dir")
            loader.set_project_build_dir(project, path)

        for arg in args.install_dir:
            project, path = parse_project_arg(arg, "--install-dir")
            loader.set_project_install_dir(project, path)

        for arg in args.project_install_prefix:
            project, path = parse_project_arg(arg, "--install-prefix")
            loader.set_project_install_prefix(project, path)

    def setup_parser(self, parser):
        parser.add_argument(
            "project",
            nargs="?",
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
            "--current-project",
            help="Specify the name of the fbcode_builder manifest file for the "
            "current repository.  If not specified, the code will attempt to find "
            "this in a .projectid file in the repository root.",
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
        parser.add_argument(
            "--project-install-prefix",
            default=[],
            action="append",
            help="Specify the final deployment installation path for a project",
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
        return self.cache and self.m.shipit_project is None

    def was_cached(self):
        cached_marker = os.path.join(self.inst_dir, ".getdeps-cached-build")
        return os.path.exists(cached_marker)

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

                    cached_marker = os.path.join(self.inst_dir, ".getdeps-cached-build")
                    with open(cached_marker, "w") as f:
                        f.write("\n")

                    return True
            except Exception as exc:
                print("%s" % str(exc))

        return False

    def upload(self):
        if self.is_cacheable():
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


@cmd("install-system-deps", "Install system packages to satisfy the deps for a project")
class InstallSysDepsCmd(ProjectCmdBase):
    def setup_project_cmd_parser(self, parser):
        parser.add_argument(
            "--recursive",
            help="install the transitive deps also",
            action="store_true",
            default=False,
        )

    def run_project_cmd(self, args, loader, manifest):
        if args.recursive:
            projects = loader.manifests_in_dependency_order()
        else:
            projects = [manifest]

        cache = cache_module.create_cache()
        all_packages = {}
        for m in projects:
            ctx = loader.ctx_gen.get_context(m.name)
            packages = m.get_required_system_packages(ctx)
            for k, v in packages.items():
                merged = all_packages.get(k, [])
                merged += v
                all_packages[k] = merged

        manager = loader.build_opts.host_type.get_package_manager()
        if manager == "rpm":
            packages = sorted(list(set(all_packages["rpm"])))
            if packages:
                run_cmd(["dnf", "install", "-y"] + packages)
        elif manager == "deb":
            packages = sorted(list(set(all_packages["deb"])))
            if packages:
                run_cmd(["apt", "install", "-y"] + packages)
        else:
            print("I don't know how to install any packages on this system")


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
            inst_dir = loader.get_project_install_dir_respecting_install_prefix(m)
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

        cache = cache_module.create_cache() if args.use_build_cache else None

        # Accumulate the install directories so that the build steps
        # can find their dep installation
        install_dirs = []

        for m in projects:
            fetcher = loader.create_fetcher(m)

            if isinstance(fetcher, SystemPackageFetcher):
                # We are guaranteed that if the fetcher is set to
                # SystemPackageFetcher then this item is completely
                # satisfied by the appropriate system packages
                continue

            if args.clean:
                fetcher.clean()

            build_dir = loader.get_project_build_dir(m)
            inst_dir = loader.get_project_install_dir(m)

            if (
                m == manifest
                and not args.only_deps
                or m != manifest
                and not args.no_deps
            ):
                print("Assessing %s..." % m.name)
                project_hash = loader.get_project_hash(m)
                ctx = loader.ctx_gen.get_context(m.name)
                built_marker = os.path.join(inst_dir, ".built-by-getdeps")

                cached_project = CachedProject(cache, loader, m)

                reconfigure, sources_changed = self.compute_source_change_status(
                    cached_project, fetcher, m, built_marker, project_hash
                )

                if os.path.exists(built_marker) and not cached_project.was_cached():
                    # We've previously built this. We may need to reconfigure if
                    # our deps have changed, so let's check them.
                    dep_reconfigure, dep_build = self.compute_dep_change_status(
                        m, built_marker, loader
                    )
                    if dep_reconfigure:
                        reconfigure = True
                    if dep_build:
                        sources_changed = True

                if sources_changed or reconfigure or not os.path.exists(built_marker):
                    if os.path.exists(built_marker):
                        os.unlink(built_marker)
                    src_dir = fetcher.get_src_dir()
                    builder = m.create_builder(
                        loader.build_opts,
                        src_dir,
                        build_dir,
                        inst_dir,
                        ctx,
                        loader,
                        final_install_prefix=loader.get_project_install_prefix(m),
                    )
                    builder.build(install_dirs, reconfigure=reconfigure)

                    with open(built_marker, "w") as f:
                        f.write(project_hash)

                    # Only populate the cache from continuous build runs
                    if args.schedule_type == "continuous":
                        cached_project.upload()

            install_dirs.append(inst_dir)

    def compute_dep_change_status(self, m, built_marker, loader):
        reconfigure = False
        sources_changed = False
        st = os.lstat(built_marker)

        ctx = loader.ctx_gen.get_context(m.name)
        dep_list = sorted(m.get_section_as_dict("dependencies", ctx).keys())
        for dep in dep_list:
            if reconfigure and sources_changed:
                break

            dep_manifest = loader.load_manifest(dep)
            dep_root = loader.get_project_install_dir(dep_manifest)
            for dep_file in list_files_under_dir_newer_than_timestamp(
                dep_root, st.st_mtime
            ):
                if os.path.basename(dep_file) == ".built-by-getdeps":
                    continue
                if file_name_is_cmake_file(dep_file):
                    if not reconfigure:
                        reconfigure = True
                        print(
                            f"Will reconfigure cmake because {dep_file} is newer than {built_marker}"
                        )
                else:
                    if not sources_changed:
                        sources_changed = True
                        print(
                            f"Will run build because {dep_file} is newer than {built_marker}"
                        )

                if reconfigure and sources_changed:
                    break

        return reconfigure, sources_changed

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
                    # While we don't need to consult the fetcher for the
                    # status in this case, we may still need to have eg: shipit
                    # run in order to have a correct source tree.
                    fetcher.update()

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
            "--only-deps",
            action="store_true",
            default=False,
            help=(
                "Only build the named project's deps. "
                "This is most useful when you want to separate out building "
                "of all of the deps and your project"
            ),
        )
        parser.add_argument(
            "--no-build-cache",
            action="store_false",
            default=True,
            dest="use_build_cache",
            help="Do not attempt to use the build cache.",
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
            inst_dir = loader.get_project_install_dir_respecting_install_prefix(m)
            install_dirs.append(inst_dir)

            if m == manifest:
                dep_munger = create_dyn_dep_munger(
                    loader.build_opts, install_dirs, args.strip
                )
                dep_munger.process_deps(args.destdir, args.final_install_prefix)

    def setup_project_cmd_parser(self, parser):
        parser.add_argument("destdir", help="Where to copy the fixed up executables")
        parser.add_argument(
            "--final-install-prefix", help="specify the final installation prefix"
        )
        parser.add_argument(
            "--strip",
            action="store_true",
            default=False,
            help="Strip debug info while processing executables",
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
                    loader.build_opts, src_dir, build_dir, inst_dir, ctx, loader
                )

                builder.run_tests(
                    install_dirs,
                    schedule_type=args.schedule_type,
                    owner=args.test_owner,
                    test_filter=args.filter,
                    retry=args.retry,
                    no_testpilot=args.no_testpilot,
                )

            install_dirs.append(inst_dir)

    def setup_project_cmd_parser(self, parser):
        parser.add_argument(
            "--schedule-type", help="Indicates how the build was activated"
        )
        parser.add_argument("--test-owner", help="Owner for testpilot")
        parser.add_argument("--filter", help="Only run the tests matching the regex")
        parser.add_argument(
            "--retry",
            type=int,
            default=3,
            help="Number of immediate retries for failed tests "
            "(noop in continuous and testwarden runs)",
        )
        parser.add_argument(
            "--no-testpilot",
            help="Do not use Test Pilot even when available",
            action="store_true",
        )


@cmd("generate-github-actions", "generate a GitHub actions configuration")
class GenerateGitHubActionsCmd(ProjectCmdBase):
    RUN_ON_ALL = """ [push, pull_request]"""
    RUN_ON_DEFAULT = """
  push:
    branches:
    - master
  pull_request:
    branches:
    - master"""

    def run_project_cmd(self, args, loader, manifest):
        platforms = [
            HostType("linux", "ubuntu", "18"),
            HostType("darwin", None, None),
            HostType("windows", None, None),
        ]

        for p in platforms:
            self.write_job_for_platform(p, args)

    # TODO: Break up complex function
    def write_job_for_platform(self, platform, args):  # noqa: C901
        build_opts = setup_build_options(args, platform)
        ctx_gen = build_opts.get_context_generator()
        loader = ManifestLoader(build_opts, ctx_gen)
        manifest = loader.load_manifest(args.project)
        manifest_ctx = loader.ctx_gen.get_context(manifest.name)
        run_on = self.RUN_ON_ALL if args.run_on_all_branches else self.RUN_ON_DEFAULT

        # Some projects don't do anything "useful" as a leaf project, only
        # as a dep for a leaf project. Check for those here; we don't want
        # to waste the effort scheduling them on CI.
        # We do this by looking at the builder type in the manifest file
        # rather than creating a builder and checking its type because we
        # don't know enough to create the full builder instance here.
        if manifest.get("build", "builder", ctx=manifest_ctx) == "nop":
            return None

        # We want to be sure that we're running things with python 3
        # but python versioning is honestly a bit of a frustrating mess.
        # `python` may be version 2 or version 3 depending on the system.
        # python3 may not be a thing at all!
        # Assume an optimistic default
        py3 = "python3"

        if build_opts.is_linux():
            job_name = "linux"
            runs_on = f"ubuntu-{args.ubuntu_version}"
        elif build_opts.is_windows():
            # We're targeting the windows-2016 image because it has
            # Visual Studio 2017 installed, and at the time of writing,
            # the version of boost in the manifests (1.69) is not
            # buildable with Visual Studio 2019
            job_name = "windows"
            runs_on = "windows-2016"
            # The windows runners are python 3 by default; python2.exe
            # is available if needed.
            py3 = "python"
        else:
            job_name = "mac"
            runs_on = "macOS-latest"

        os.makedirs(args.output_dir, exist_ok=True)
        output_file = os.path.join(args.output_dir, f"getdeps_{job_name}.yml")
        with open(output_file, "w") as out:
            # Deliberate line break here because the @ and the generated
            # symbols are meaningful to our internal tooling when they
            # appear in a single token
            out.write("# This file was @")
            out.write("generated by getdeps.py\n")
            out.write(
                f"""
name: {job_name}

on:{run_on}

jobs:
"""
            )

            getdeps = f"{py3} build/fbcode_builder/getdeps.py"
            if not args.disallow_system_packages:
                getdeps += " --allow-system-packages"

            out.write("  build:\n")
            out.write("    runs-on: %s\n" % runs_on)
            out.write("    steps:\n")
            out.write("    - uses: actions/checkout@v1\n")

            if build_opts.is_windows():
                # cmake relies on BOOST_ROOT but GH deliberately don't set it in order
                # to avoid versioning issues:
                # https://github.com/actions/virtual-environments/issues/319
                # Instead, set the version we think we need; this is effectively
                # coupled with the boost manifest
                # This is the unusual syntax for setting an env var for the rest of
                # the steps in a workflow:
                # https://help.github.com/en/actions/reference/workflow-commands-for-github-actions#setting-an-environment-variable
                out.write("    - name: Export boost environment\n")
                out.write(
                    '      run: "echo ::set-env name=BOOST_ROOT::%BOOST_ROOT_1_69_0%"\n'
                )
                out.write("      shell: cmd\n")

                # The git installation may not like long filenames, so tell it
                # that we want it to use them!
                out.write("    - name: Fix Git config\n")
                out.write("      run: git config --system core.longpaths true\n")
            elif not args.disallow_system_packages:
                out.write("    - name: Install system deps\n")
                out.write(
                    f"      run: sudo {getdeps} install-system-deps --recursive {manifest.name}\n"
                )

            projects = loader.manifests_in_dependency_order()

            for m in projects:
                if m != manifest:
                    out.write("    - name: Fetch %s\n" % m.name)
                    out.write(f"      run: {getdeps} fetch --no-tests {m.name}\n")

            for m in projects:
                if m != manifest:
                    out.write("    - name: Build %s\n" % m.name)
                    out.write(f"      run: {getdeps} build --no-tests {m.name}\n")

            out.write("    - name: Build %s\n" % manifest.name)

            project_prefix = ""
            if not build_opts.is_windows():
                project_prefix = (
                    " --project-install-prefix %s:/usr/local" % manifest.name
                )

            out.write(
                f"      run: {getdeps} build --src-dir=. {manifest.name} {project_prefix}\n"
            )

            out.write("    - name: Copy artifacts\n")
            if build_opts.is_linux():
                # Strip debug info from the binaries, but only on linux.
                # While the `strip` utility is also available on macOS,
                # attempting to strip there results in an error.
                # The `strip` utility is not available on Windows.
                strip = " --strip"
            else:
                strip = ""

            out.write(
                f"      run: {getdeps} fixup-dyn-deps{strip} "
                f"--src-dir=. {manifest.name} _artifacts/{job_name} {project_prefix} "
                f"--final-install-prefix /usr/local\n"
            )

            out.write("    - uses: actions/upload-artifact@master\n")
            out.write("      with:\n")
            out.write("        name: %s\n" % manifest.name)
            out.write("        path: _artifacts\n")

            out.write("    - name: Test %s\n" % manifest.name)
            out.write(
                f"      run: {getdeps} test --src-dir=. {manifest.name} {project_prefix}\n"
            )

    def setup_project_cmd_parser(self, parser):
        parser.add_argument(
            "--disallow-system-packages",
            help="Disallow satisfying third party deps from installed system packages",
            action="store_true",
            default=False,
        )
        parser.add_argument(
            "--output-dir", help="The directory that will contain the yml files"
        )
        parser.add_argument(
            "--run-on-all-branches",
            action="store_true",
            help="Allow CI to fire on all branches - Handy for testing",
        )
        parser.add_argument(
            "--ubuntu-version", default="18.04", help="Version of Ubuntu to use"
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
        default=None,
    )
    add_common_arg(
        "--no-facebook-internal",
        help="Perform a non-FB internal build, even when in an fbsource repository",
        action="store_false",
        dest="facebook_internal",
    )
    add_common_arg(
        "--allow-system-packages",
        help="Allow satisfying third party deps from installed system packages",
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
