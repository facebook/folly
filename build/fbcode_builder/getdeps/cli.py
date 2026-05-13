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
from . import (  # noqa: F401  # registers @cmd("generate-github-actions")
    cache as cache_module,
    workflow_generator,
)
from .buildopts import setup_build_options
from .cmd_base import BUILD_TYPE_ARG, ProjectCmdBase, UsageError
from .dyndeps import create_dyn_dep_munger
from .errors import TransientFailure
from .fetcher import (
    file_name_is_cmake_file,
    is_public_commit,
    list_files_under_dir_newer_than_timestamp,
    safe_extractall,
    SystemPackageFetcher,
)
from .getdeps_platform import HostType
from .manifest import ManifestParser
from .runcmd import check_cmd
from .shared_lib import apply_shared_lib_top_level_cmake_defines
from .subcmd import add_subcommands, cmd, SubCmd

try:
    from . import facebook  # noqa: F401
except ImportError:
    # we don't ship the facebook specific subdir,
    # so allow that to fail silently
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


class CachedProject:
    """A helper that allows calling the cache logic for a project
    from both the build and the fetch code"""

    def __init__(self, cache, loader, m):
        self.m = m
        self.inst_dir = loader.get_project_install_dir(m)
        self.project_hash = loader.get_project_hash(m)
        self.ctx = loader.ctx_gen.get_context(m.name)
        self.loader = loader
        self.cache = cache

        self.cache_key = "-".join(
            (
                m.name,
                self.ctx.get("os"),
                self.ctx.get("distro") or "none",
                self.ctx.get("distro_vers") or "none",
                self.project_hash,
            )
        )
        self.cache_file_name = self.cache_key + "-buildcache.tgz"

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
                    with tarfile.open(target_file_name, "r") as tf:
                        print(
                            "Extracting %s -> %s..."
                            % (self.cache_file_name, self.inst_dir)
                        )
                        safe_extractall(tf, self.inst_dir)

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
            fetcher = loader.create_fetcher(m)
            if isinstance(fetcher, SystemPackageFetcher):
                # We are guaranteed that if the fetcher is set to
                # SystemPackageFetcher then this item is completely
                # satisfied by the appropriate system packages
                continue
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

        cmd_argss = []
        # Containers (e.g. manylinux) run as root and don't ship sudo. Only
        # prepend "sudo" if it's actually on PATH; otherwise invoke the
        # package manager directly.
        sudo_cmd = ["sudo"] if shutil.which("sudo") else []
        if manager == "rpm":
            packages = sorted(set(all_packages["rpm"]))
            if packages:
                cmd_argss.append(
                    sudo_cmd + ["dnf", "install", "-y", "--skip-broken"] + packages
                )
        elif manager == "deb":
            packages = sorted(set(all_packages["deb"]))
            if packages:
                cmd_argss.append(
                    sudo_cmd
                    + (["--preserve-env=http_proxy"] if sudo_cmd else [])
                    + [
                        "apt-get",
                        "install",
                        "-y",
                    ]
                    + packages
                )
                cmd_argss.append(["pip", "install", "pex"])
        elif manager == "homebrew":
            packages = sorted(set(all_packages["homebrew"]))
            if packages:
                cmd_argss.append(["brew", "install"] + packages)
        elif manager == "pacman-package":
            packages = sorted(set(all_packages["pacman-package"]))
            if packages:
                cmd_argss.append(["pacman", "-S"] + packages)
        else:
            host_tuple = loader.build_opts.host_type.as_tuple_string()
            print(
                f"I don't know how to install any packages on this system {host_tuple}"
            )
            return

        for cmd_args in cmd_argss:
            if args.dry_run:
                print(" ".join(cmd_args))
            else:
                check_cmd(cmd_args)
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


@cmd("query-paths", "print the paths for tooling to use")
class QueryPathsCmd(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        if args.recursive:
            manifests = loader.manifests_in_dependency_order()
        else:
            manifests = [manifest]

        cache = cache_module.create_cache()
        for m in manifests:
            fetcher = loader.create_fetcher(m)
            if isinstance(fetcher, SystemPackageFetcher):
                # We are guaranteed that if the fetcher is set to
                # SystemPackageFetcher then this item is completely
                # satisfied by the appropriate system packages
                continue
            src_dir = fetcher.get_src_dir()
            print(f"{m.name}_SOURCE={src_dir}")
            inst_dir = loader.get_project_install_dir_respecting_install_prefix(m)
            print(f"{m.name}_INSTALL={inst_dir}")
            cached_project = CachedProject(cache, loader, m)
            print(f"{m.name}_CACHE_KEY={cached_project.cache_key}")

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
    def run_project_cmd(self, args, loader, manifest):  # noqa: C901
        if args.clean:
            clean_dirs(loader.build_opts)

        print("Building on %s" % loader.ctx_gen.get_context(args.project))
        loader.build_opts.top_level_manifest_name = manifest.name
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
                if m == manifest:
                    apply_shared_lib_top_level_cmake_defines(
                        extra_cmake_defines, loader.build_opts
                    )

                extra_b2_args = args.extra_b2_args or []
                cmake_targets = args.cmake_target or ["install"]

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
                        cmake_targets=(cmake_targets if m == manifest else ["install"]),
                        extra_b2_args=extra_b2_args,
                    )
                    builder.build(reconfigure=reconfigure)

                    # If we are building the project (not dependency) and a specific
                    # cmake_target (not 'install') has been requested, then we don't
                    # set the built_marker. This allows subsequent runs of getdeps.py
                    # for the project to run with different cmake_targets to trigger
                    # cmake
                    has_built_marker = False
                    if not (m == manifest and "install" not in cmake_targets):
                        os.makedirs(os.path.dirname(built_marker), exist_ok=True)
                        with open(built_marker, "w") as f:
                            f.write(project_hash)
                            has_built_marker = True

                    # Only populate the cache from continuous build runs, and
                    # only if we have a built_marker.
                    if not args.skip_upload and has_built_marker:
                        if args.schedule_type == "continuous":
                            cached_project.upload()
                        elif args.schedule_type == "base_retry":
                            # Check if on public commit before uploading
                            if is_public_commit(loader.build_opts):
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
            "--cmake-target",
            help=("Repeatable argument that specifies targets for cmake build."),
            default=[],
            action="append",
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
            help="After installing each dependency, delete its build directory to free disk space during the build",
            action="store_true",
            default=False,
        )
        parser.add_argument("--build-type", **BUILD_TYPE_ARG)


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
        loader.build_opts.top_level_manifest_name = manifest.name
        return self.create_builder(loader, manifest).run_tests(
            schedule_type=args.schedule_type,
            owner=args.test_owner,
            test_filter=args.filter,
            test_exclude=args.exclude,
            retry=args.retry,
            no_testpilot=args.no_testpilot,
            timeout=args.timeout,
        )

    def setup_project_cmd_parser(self, parser):
        parser.add_argument("--test-owner", help="Owner for testpilot")
        parser.add_argument("--filter", help="Only run the tests matching the regex")
        parser.add_argument("--exclude", help="Exclude tests matching the regex")
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
        parser.add_argument(
            "--timeout",
            type=int,
            default=None,
            help="Timeout in seconds for each individual test",
        )
        parser.add_argument("--build-type", **BUILD_TYPE_ARG)


@cmd(
    "debug",
    "start a shell in the given project's build dir with the correct environment for running the build",
)
class DebugCmd(ProjectCmdBase):
    def run_project_cmd(self, args, loader, manifest):
        loader.build_opts.top_level_manifest_name = manifest.name
        self.create_builder(loader, manifest).debug(reconfigure=False)


@cmd(
    "env",
    "print the environment in a shell sourceable format",
)
class EnvCmd(ProjectCmdBase):
    def setup_project_cmd_parser(self, parser):
        parser.add_argument(
            "--os-type",
            help="Filter to just this OS type to run",
            choices=["linux", "darwin", "windows"],
            action="store",
            dest="ostype",
            default=None,
        )

    def run_project_cmd(self, args, loader, manifest):
        if args.ostype:
            loader.build_opts.host_type.ostype = args.ostype
        loader.build_opts.top_level_manifest_name = manifest.name
        self.create_builder(loader, manifest).printenv(reconfigure=False)


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
        "--shared-lib",
        help=(
            "Build deps as static-PIC archives and the top-level project as a "
            "shared library that links them in."
        ),
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
    add_common_arg(
        "--schedule-type",
        nargs="?",
        help="Indicates how the build was activated",
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
