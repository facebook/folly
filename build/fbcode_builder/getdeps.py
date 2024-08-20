#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import argparse
import json
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
    file_name_is_cmake_file,
    list_files_under_dir_newer_than_timestamp,
    SystemPackageFetcher,
)
from getdeps.load import ManifestLoader
from getdeps.manifest import ManifestParser
from getdeps.platform import HostType
from getdeps.runcmd import run_cmd
from getdeps.subcmd import add_subcommands, cmd, SubCmd

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

        ctx_gen = opts.get_context_generator()
        if args.test_dependencies:
            ctx_gen.set_value_for_all_projects("test", "on")
        if args.enable_tests:
            ctx_gen.set_value_for_project(args.project, "test", "on")
        else:
            ctx_gen.set_value_for_project(args.project, "test", "off")

        if opts.shared_libs:
            ctx_gen.set_value_for_all_projects("shared_libs", "on")

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
            # On Windows path contains colon, e.g. C:\open
            elif os.name == "nt" and len(parts) == 3:
                project = parts[0]
                path = parts[1] + ":" + parts[2]
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

    def create_builder(self, loader, manifest):
        fetcher = loader.create_fetcher(manifest)
        src_dir = fetcher.get_src_dir()
        ctx = loader.ctx_gen.get_context(manifest.name)
        build_dir = loader.get_project_build_dir(manifest)
        inst_dir = loader.get_project_install_dir(manifest)
        return manifest.create_builder(
            loader.build_opts,
            src_dir,
            build_dir,
            inst_dir,
            ctx,
            loader,
            loader.dependencies_of(manifest),
        )

    def check_built(self, loader, manifest):
        built_marker = os.path.join(
            loader.get_project_install_dir(manifest), ".built-by-getdeps"
        )
        return os.path.exists(built_marker)


class CachedProject(object):
    """A helper that allows calling the cache logic for a project
    from both the build and the fetch code"""

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
        """We only cache third party projects"""
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
        parser.add_argument(
            "--dry-run",
            action="store_true",
            default=False,
            help="Don't install, just print the commands specs we would run",
        )
        parser.add_argument(
            "--os-type",
            help="Filter to just this OS type to run",
            choices=["linux", "darwin", "windows", "pacman-package"],
            action="store",
            dest="ostype",
            default=None,
        )
        parser.add_argument(
            "--distro",
            help="Filter to just this distro to run",
            choices=["ubuntu", "centos_stream"],
            action="store",
            dest="distro",
            default=None,
        )
        parser.add_argument(
            "--distro-version",
            help="Filter to just this distro version",
            action="store",
            dest="distrovers",
            default=None,
        )

    def run_project_cmd(self, args, loader, manifest):
        if args.recursive:
            projects = loader.manifests_in_dependency_order()
        else:
            projects = [manifest]

        rebuild_ctx_gen = False
        if args.ostype:
            loader.build_opts.host_type.ostype = args.ostype
            loader.build_opts.host_type.distro = None
            loader.build_opts.host_type.distrovers = None
            rebuild_ctx_gen = True

        if args.distro:
            loader.build_opts.host_type.distro = args.distro
            loader.build_opts.host_type.distrovers = None
            rebuild_ctx_gen = True

        if args.distrovers:
            loader.build_opts.host_type.distrovers = args.distrovers
            rebuild_ctx_gen = True

        if rebuild_ctx_gen:
            loader.ctx_gen = loader.build_opts.get_context_generator()

        manager = loader.build_opts.host_type.get_package_manager()

        all_packages = {}
        for m in projects:
            ctx = loader.ctx_gen.get_context(m.name)
            packages = m.get_required_system_packages(ctx)
            for k, v in packages.items():
                merged = all_packages.get(k, [])
                merged += v
                all_packages[k] = merged

        cmd_args = None
        if manager == "rpm":
            packages = sorted(set(all_packages["rpm"]))
            if packages:
                cmd_args = ["sudo", "dnf", "install", "-y"] + packages
        elif manager == "deb":
            packages = sorted(set(all_packages["deb"]))
            if packages:
                cmd_args = ["sudo", "apt", "install", "-y"] + packages
        elif manager == "homebrew":
            packages = sorted(set(all_packages["homebrew"]))
            if packages:
                cmd_args = ["brew", "install"] + packages
        elif manager == "pacman-package":
            packages = sorted(list(set(all_packages["pacman-package"])))
            if packages:
                cmd_args = ["pacman", "-S"] + packages
        else:
            host_tuple = loader.build_opts.host_type.as_tuple_string()
            print(
                f"I don't know how to install any packages on this system {host_tuple}"
            )
            return

        if cmd_args:
            if args.dry_run:
                print(" ".join(cmd_args))
            else:
                run_cmd(cmd_args)
        else:
            print("no packages to install")


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


@cmd("show-scratch-dir", "show the scratch dir")
class ShowScratchDirCmd(SubCmd):
    def run(self, args):
        opts = setup_build_options(args)
        print(opts.scratch_dir)


@cmd("show-build-dir", "print the build dir for a given project")
class ShowBuildDirCmd(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        if args.recursive:
            manifests = loader.manifests_in_dependency_order()
        else:
            manifests = [manifest]

        for m in manifests:
            inst_dir = loader.get_project_build_dir(m)
            print(inst_dir)

    def setup_project_cmd_parser(self, parser):
        parser.add_argument(
            "--recursive",
            help="print the transitive deps also",
            action="store_true",
            default=False,
        )


@cmd("show-inst-dir", "print the installation dir for a given project")
class ShowInstDirCmd(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        if args.recursive:
            manifests = loader.manifests_in_dependency_order()
        else:
            manifests = [manifest]

        for m in manifests:
            fetcher = loader.create_fetcher(m)
            if isinstance(fetcher, SystemPackageFetcher):
                # We are guaranteed that if the fetcher is set to
                # SystemPackageFetcher then this item is completely
                # satisfied by the appropriate system packages
                continue
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

        dep_manifests = []

        for m in projects:
            dep_manifests.append(m)

            fetcher = loader.create_fetcher(m)

            if args.build_skip_lfs_download and hasattr(fetcher, "skip_lfs_download"):
                print("skipping lfs download for %s" % m.name)
                fetcher.skip_lfs_download()

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

                extra_cmake_defines = (
                    json.loads(args.extra_cmake_defines)
                    if args.extra_cmake_defines
                    else {}
                )

                extra_b2_args = args.extra_b2_args or []

                if sources_changed or reconfigure or not os.path.exists(built_marker):
                    if os.path.exists(built_marker):
                        os.unlink(built_marker)
                    src_dir = fetcher.get_src_dir()
                    # Prepare builders write out config before the main builder runs
                    prepare_builders = m.create_prepare_builders(
                        loader.build_opts,
                        ctx,
                        src_dir,
                        build_dir,
                        inst_dir,
                        loader,
                        dep_manifests,
                    )
                    for preparer in prepare_builders:
                        preparer.prepare(reconfigure=reconfigure)

                    builder = m.create_builder(
                        loader.build_opts,
                        src_dir,
                        build_dir,
                        inst_dir,
                        ctx,
                        loader,
                        dep_manifests,
                        final_install_prefix=loader.get_project_install_prefix(m),
                        extra_cmake_defines=extra_cmake_defines,
                        cmake_target=args.cmake_target if m == manifest else "install",
                        extra_b2_args=extra_b2_args,
                    )
                    builder.build(reconfigure=reconfigure)

                    # If we are building the project (not dependency) and a specific
                    # cmake_target (not 'install') has been requested, then we don't
                    # set the built_marker. This allows subsequent runs of getdeps.py
                    # for the project to run with different cmake_targets to trigger
                    # cmake
                    has_built_marker = False
                    if not (m == manifest and args.cmake_target != "install"):
                        with open(built_marker, "w") as f:
                            f.write(project_hash)
                            has_built_marker = True

                    # Only populate the cache from continuous build runs, and
                    # only if we have a built_marker.
                    if (
                        not args.skip_upload
                        and args.schedule_type == "continuous"
                        and has_built_marker
                    ):
                        cached_project.upload()
                elif args.verbose:
                    print("found good %s" % built_marker)

    def compute_dep_change_status(self, m, built_marker, loader):
        reconfigure = False
        sources_changed = False
        st = os.lstat(built_marker)

        ctx = loader.ctx_gen.get_context(m.name)
        dep_list = m.get_dependencies(ctx)
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
        if cached_project.download():
            if not os.path.exists(built_marker):
                fetcher.update()
        else:
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
        parser.add_argument(
            "--cmake-target",
            help=("Target for cmake build."),
            default="install",
        )
        parser.add_argument(
            "--extra-b2-args",
            help=(
                "Repeatable argument that contains extra arguments to pass "
                "to b2, which compiles boost. "
                "e.g.: 'cxxflags=-fPIC' 'cflags=-fPIC'"
            ),
            action="append",
        )
        parser.add_argument(
            "--free-up-disk",
            help="Remove unused tools and clean up intermediate files if possible to maximise space for the build",
            action="store_true",
            default=False,
        )
        parser.add_argument(
            "--build-type",
            help="Set the build type explicitly.  Cmake and cargo builders act on them. Only Debug and RelWithDebInfo widely supported.",
            choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
            action="store",
            default=None,
        )


@cmd("fixup-dyn-deps", "Adjusts dynamic dependencies for packaging purposes")
class FixupDeps(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        projects = loader.manifests_in_dependency_order()

        # Accumulate the install directories so that the build steps
        # can find their dep installation
        install_dirs = []
        dep_manifests = []

        for m in projects:
            inst_dir = loader.get_project_install_dir_respecting_install_prefix(m)
            install_dirs.append(inst_dir)
            dep_manifests.append(m)

            if m == manifest:
                ctx = loader.ctx_gen.get_context(m.name)
                env = loader.build_opts.compute_env_for_install_dirs(
                    loader, dep_manifests, ctx
                )
                dep_munger = create_dyn_dep_munger(
                    loader.build_opts, env, install_dirs, args.strip
                )
                if dep_munger is None:
                    print(f"dynamic dependency fixups not supported on {sys.platform}")
                else:
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
        if not self.check_built(loader, manifest):
            print("project %s has not been built" % manifest.name)
            return 1
        self.create_builder(loader, manifest).run_tests(
            schedule_type=args.schedule_type,
            owner=args.test_owner,
            test_filter=args.filter,
            retry=args.retry,
            no_testpilot=args.no_testpilot,
        )

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


@cmd(
    "debug",
    "start a shell in the given project's build dir with the correct environment for running the build",
)
class DebugCmd(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        self.create_builder(loader, manifest).debug(reconfigure=False)


@cmd("generate-github-actions", "generate a GitHub actions configuration")
class GenerateGitHubActionsCmd(ProjectCmdBase):
    RUN_ON_ALL = """ [push, pull_request]"""

    def run_project_cmd(self, args, loader, manifest):
        platforms = [
            HostType("linux", "ubuntu", "22"),
            HostType("darwin", None, None),
            HostType("windows", None, None),
        ]

        for p in platforms:
            if args.os_types and p.ostype not in args.os_types:
                continue
            self.write_job_for_platform(p, args)

    def get_run_on(self, args):
        if args.run_on_all_branches:
            return self.RUN_ON_ALL
        if args.cron:
            return f"""
  schedule:
    - cron: '{args.cron}'"""

        return f"""
  push:
    branches:
    - {args.main_branch}
  pull_request:
    branches:
    - {args.main_branch}"""

    # TODO: Break up complex function
    def write_job_for_platform(self, platform, args):  # noqa: C901
        build_opts = setup_build_options(args, platform)
        ctx_gen = build_opts.get_context_generator()
        loader = ManifestLoader(build_opts, ctx_gen)
        manifest = loader.load_manifest(args.project)
        manifest_ctx = loader.ctx_gen.get_context(manifest.name)
        run_on = self.get_run_on(args)

        # Some projects don't do anything "useful" as a leaf project, only
        # as a dep for a leaf project. Check for those here; we don't want
        # to waste the effort scheduling them on CI.
        # We do this by looking at the builder type in the manifest file
        # rather than creating a builder and checking its type because we
        # don't know enough to create the full builder instance here.
        builder_name = manifest.get("build", "builder", ctx=manifest_ctx)
        if builder_name == "nop":
            return None

        # We want to be sure that we're running things with python 3
        # but python versioning is honestly a bit of a frustrating mess.
        # `python` may be version 2 or version 3 depending on the system.
        # python3 may not be a thing at all!
        # Assume an optimistic default
        py3 = "python3"

        if build_opts.is_linux():
            artifacts = "linux"
            runs_on = f"ubuntu-{args.ubuntu_version}"
        elif build_opts.is_windows():
            artifacts = "windows"
            runs_on = "windows-2019"
            # The windows runners are python 3 by default; python2.exe
            # is available if needed.
            py3 = "python"
        else:
            artifacts = "mac"
            runs_on = "macOS-latest"

        os.makedirs(args.output_dir, exist_ok=True)

        job_file_prefix = "getdeps_"
        if args.job_file_prefix:
            job_file_prefix = args.job_file_prefix

        output_file = os.path.join(args.output_dir, f"{job_file_prefix}{artifacts}.yml")

        if args.job_name_prefix:
            job_name = args.job_name_prefix + artifacts.capitalize()
        else:
            job_name = artifacts

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

permissions:
  contents: read  #  to fetch code (actions/checkout)

jobs:
"""
            )

            getdepscmd = f"{py3} build/fbcode_builder/getdeps.py"

            out.write("  build:\n")
            out.write("    runs-on: %s\n" % runs_on)
            out.write("    steps:\n")

            if build_opts.is_windows():
                # cmake relies on BOOST_ROOT but GH deliberately don't set it in order
                # to avoid versioning issues:
                # https://github.com/actions/virtual-environments/issues/319
                # Instead, set the version we think we need; this is effectively
                # coupled with the boost manifest
                # This is the unusual syntax for setting an env var for the rest of
                # the steps in a workflow:
                # https://github.blog/changelog/2020-10-01-github-actions-deprecating-set-env-and-add-path-commands/
                out.write("    - name: Export boost environment\n")
                out.write(
                    '      run: "echo BOOST_ROOT=%BOOST_ROOT_1_83_0% >> %GITHUB_ENV%"\n'
                )
                out.write("      shell: cmd\n")

                # The git installation may not like long filenames, so tell it
                # that we want it to use them!
                out.write("    - name: Fix Git config\n")
                out.write("      run: git config --system core.longpaths true\n")
                out.write("    - name: Disable autocrlf\n")
                out.write("      run: git config --system core.autocrlf false\n")

            out.write("    - uses: actions/checkout@v4\n")

            build_type_arg = ""
            if args.build_type:
                build_type_arg = f"--build-type {args.build_type} "

            if build_opts.free_up_disk:
                free_up_disk = "--free-up-disk "
                if not build_opts.is_windows():
                    out.write("    - name: Show disk space at start\n")
                    out.write("      run: df -h\n")
                    # remove the unused github supplied android dev tools
                    out.write("    - name: Free up disk space\n")
                    out.write("      run: sudo rm -rf /usr/local/lib/android\n")
                    out.write("    - name: Show disk space after freeing up\n")
                    out.write("      run: df -h\n")
            else:
                free_up_disk = ""

            allow_sys_arg = ""
            if (
                build_opts.allow_system_packages
                and build_opts.host_type.get_package_manager()
            ):
                sudo_arg = "sudo "
                allow_sys_arg = " --allow-system-packages"
                if build_opts.host_type.get_package_manager() == "deb":
                    out.write("    - name: Update system package info\n")
                    out.write(f"      run: {sudo_arg}apt-get update\n")

                out.write("    - name: Install system deps\n")
                if build_opts.is_darwin():
                    # brew is installed as regular user
                    sudo_arg = ""
                out.write(
                    f"      run: {sudo_arg}python3 build/fbcode_builder/getdeps.py --allow-system-packages install-system-deps --recursive {manifest.name}\n"
                )
                if build_opts.is_linux() or build_opts.is_freebsd():
                    out.write("    - name: Install packaging system deps\n")
                    out.write(
                        f"      run: {sudo_arg}python3 build/fbcode_builder/getdeps.py --allow-system-packages install-system-deps --recursive patchelf\n"
                    )

            projects = loader.manifests_in_dependency_order()

            main_repo_url = manifest.get_repo_url(manifest_ctx)
            has_same_repo_dep = False

            # Add the rust dep which doesn't have a manifest
            for m in projects:
                if m == manifest:
                    continue
                mbuilder_name = m.get("build", "builder", ctx=manifest_ctx)
                if (
                    m.name == "rust"
                    or builder_name == "cargo"
                    or mbuilder_name == "cargo"
                ):
                    out.write("    - name: Install Rust Stable\n")
                    out.write("      uses: dtolnay/rust-toolchain@stable\n")
                    break

            # Normal deps that have manifests
            for m in projects:
                if m == manifest or m.name == "rust":
                    continue
                ctx = loader.ctx_gen.get_context(m.name)
                if m.get_repo_url(ctx) != main_repo_url:
                    out.write("    - name: Fetch %s\n" % m.name)
                    out.write(
                        f"      run: {getdepscmd}{allow_sys_arg} fetch --no-tests {m.name}\n"
                    )

            for m in projects:
                if m != manifest:
                    if m.name == "rust":
                        continue
                    else:
                        src_dir_arg = ""
                        ctx = loader.ctx_gen.get_context(m.name)
                        if main_repo_url and m.get_repo_url(ctx) == main_repo_url:
                            # Its in the same repo, so src-dir is also .
                            src_dir_arg = "--src-dir=. "
                            has_same_repo_dep = True
                        out.write("    - name: Build %s\n" % m.name)
                        out.write(
                            f"      run: {getdepscmd}{allow_sys_arg} build {build_type_arg}{src_dir_arg}{free_up_disk}--no-tests {m.name}\n"
                        )

            out.write("    - name: Build %s\n" % manifest.name)

            project_prefix = ""
            if not build_opts.is_windows():
                project_prefix = (
                    " --project-install-prefix %s:/usr/local" % manifest.name
                )

            # If we have dep from same repo, we already built it and don't want to rebuild it again
            no_deps_arg = ""
            if has_same_repo_dep:
                no_deps_arg = "--no-deps "

            no_tests_arg = ""
            if not args.enable_tests:
                no_tests_arg = "--no-tests "

            out.write(
                f"      run: {getdepscmd}{allow_sys_arg} build {build_type_arg}{no_tests_arg}{no_deps_arg}--src-dir=. {manifest.name} {project_prefix}\n"
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
                f"      run: {getdepscmd}{allow_sys_arg} fixup-dyn-deps{strip} "
                f"--src-dir=. {manifest.name} _artifacts/{artifacts} {project_prefix} "
                f"--final-install-prefix /usr/local\n"
            )

            out.write("    - uses: actions/upload-artifact@v2\n")
            out.write("      with:\n")
            out.write("        name: %s\n" % manifest.name)
            out.write("        path: _artifacts\n")

            if (
                args.enable_tests
                and manifest.get("github.actions", "run_tests", ctx=manifest_ctx)
                != "off"
            ):
                out.write("    - name: Test %s\n" % manifest.name)
                out.write(
                    f"      run: {getdepscmd}{allow_sys_arg} test --src-dir=. {manifest.name} {project_prefix}\n"
                )
            if build_opts.free_up_disk and not build_opts.is_windows():
                out.write("    - name: Show disk space at end\n")
                out.write("      run: df -h\n")

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
            "--ubuntu-version", default="22.04", help="Version of Ubuntu to use"
        )
        parser.add_argument(
            "--cron",
            help="Specify that the job runs on a cron schedule instead of on pushes",
        )
        parser.add_argument(
            "--main-branch",
            default="main",
            help="Main branch to trigger GitHub Action on",
        )
        parser.add_argument(
            "--os-type",
            help="Filter to just this OS type to run",
            choices=["linux", "darwin", "windows"],
            action="append",
            dest="os_types",
            default=[],
        )
        parser.add_argument(
            "--job-file-prefix",
            type=str,
            help="add a prefix to all job file names",
            default=None,
        )
        parser.add_argument(
            "--job-name-prefix",
            type=str,
            help="add a prefix to all job names",
            default=None,
        )
        parser.add_argument(
            "--free-up-disk",
            help="Remove unused tools and clean up intermediate files if possible to maximise space for the build",
            action="store_true",
            default=False,
        )
        parser.add_argument(
            "--build-type",
            help="Set the build type explicitly.  Cmake and cargo builders act on them. Only Debug and RelWithDebInfo widely supported.",
            choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
            action="store",
            default=None,
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
        "--shared-libs",
        help="Build shared libraries if possible",
        action="store_true",
        default=False,
    )
    add_common_arg(
        "--extra-cmake-defines",
        help=(
            "Input json map that contains extra cmake defines to be used "
            "when compiling the current project and all its deps. "
            'e.g: \'{"CMAKE_CXX_FLAGS": "--bla"}\''
        ),
    )
    add_common_arg(
        "--allow-system-packages",
        help="Allow satisfying third party deps from installed system packages",
        action="store_true",
        default=False,
    )
    add_common_arg(
        "-v",
        "--verbose",
        help="Print more output",
        action="store_true",
        default=False,
    )
    add_common_arg(
        "-su",
        "--skip-upload",
        help="skip upload steps",
        action="store_true",
        default=False,
    )
    add_common_arg(
        "--lfs-path",
        help="Provide a parent directory for lfs when fbsource is unavailable",
        default=None,
    )
    add_common_arg(
        "--build-skip-lfs-download",
        action="store_true",
        default=False,
        help=(
            "Download from the URL, rather than LFS. This is useful "
            "in cases where the upstream project has uploaded a new "
            "version of the archive with a different hash"
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
