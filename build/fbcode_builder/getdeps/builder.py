#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import glob
import json
import os
import pathlib
import shutil
import stat
import subprocess
import sys
import typing
from typing import Optional

from .dyndeps import create_dyn_dep_munger
from .envfuncs import add_path_entry, Env, path_search
from .fetcher import copy_if_different
from .runcmd import run_cmd

if typing.TYPE_CHECKING:
    from .buildopts import BuildOptions


class BuilderBase(object):
    def __init__(
        self,
        build_opts: "BuildOptions",
        ctx,
        manifest,
        src_dir,
        build_dir,
        inst_dir,
        env=None,
        final_install_prefix=None,
    ) -> None:
        self.env = Env()
        if env:
            self.env.update(env)

        subdir = manifest.get("build", "subdir", ctx=ctx)
        if subdir:
            src_dir = os.path.join(src_dir, subdir)

        self.patchfile = manifest.get("build", "patchfile", ctx=ctx)
        self.patchfile_opts = manifest.get("build", "patchfile_opts", ctx=ctx) or ""
        self.ctx = ctx
        self.src_dir = src_dir
        self.build_dir = build_dir or src_dir
        self.inst_dir = inst_dir
        self.build_opts = build_opts
        self.manifest = manifest
        self.final_install_prefix = final_install_prefix

    def _get_cmd_prefix(self):
        if self.build_opts.is_windows():
            vcvarsall = self.build_opts.get_vcvars_path()
            if vcvarsall is not None:
                # Since it sets rather a large number of variables we mildly abuse
                # the cmd quoting rules to assemble a command that calls the script
                # to prep the environment and then triggers the actual command that
                # we wanted to run.
                return [vcvarsall, "amd64", "&&"]
        return []

    def _run_cmd(
        self,
        cmd,
        cwd=None,
        env=None,
        use_cmd_prefix: bool = True,
        allow_fail: bool = False,
    ) -> int:
        if env:
            e = self.env.copy()
            e.update(env)
            env = e
        else:
            env = self.env

        if use_cmd_prefix:
            cmd_prefix = self._get_cmd_prefix()
            if cmd_prefix:
                cmd = cmd_prefix + cmd

        log_file = os.path.join(self.build_dir, "getdeps_build.log")
        return run_cmd(
            cmd=cmd,
            env=env,
            cwd=cwd or self.build_dir,
            log_file=log_file,
            allow_fail=allow_fail,
        )

    def _reconfigure(self, reconfigure: bool) -> bool:
        if self.build_dir is not None:
            if not os.path.isdir(self.build_dir):
                os.makedirs(self.build_dir)
                reconfigure = True
        return reconfigure

    def _apply_patchfile(self) -> None:
        if self.patchfile is None:
            return
        patched_sentinel_file = pathlib.Path(self.src_dir + "/.getdeps_patched")
        if patched_sentinel_file.exists():
            return
        old_wd = os.getcwd()
        os.chdir(self.src_dir)
        print(f"Patching {self.manifest.name} with {self.patchfile} in {self.src_dir}")
        patchfile = os.path.join(
            self.build_opts.fbcode_builder_dir, "patches", self.patchfile
        )
        patchcmd = ["git", "apply"]
        if self.patchfile_opts:
            patchcmd.append(self.patchfile_opts)
        try:
            subprocess.check_call(patchcmd + [patchfile])
        except subprocess.CalledProcessError:
            raise ValueError(f"Failed to apply patch to {self.manifest.name}")
        os.chdir(old_wd)
        patched_sentinel_file.touch()

    def prepare(self, install_dirs, reconfigure: bool) -> None:
        print("Preparing %s..." % self.manifest.name)
        reconfigure = self._reconfigure(reconfigure)
        self._apply_patchfile()
        self._prepare(install_dirs=install_dirs, reconfigure=reconfigure)

    def build(self, install_dirs, reconfigure: bool) -> None:
        print("Building %s..." % self.manifest.name)
        reconfigure = self._reconfigure(reconfigure)
        self._apply_patchfile()
        self._prepare(install_dirs=install_dirs, reconfigure=reconfigure)
        self._build(install_dirs=install_dirs, reconfigure=reconfigure)

        # On Windows, emit a wrapper script that can be used to run build artifacts
        # directly from the build directory, without installing them.  On Windows $PATH
        # needs to be updated to include all of the directories containing the runtime
        # library dependencies in order to run the binaries.
        if self.build_opts.is_windows():
            script_path = self.get_dev_run_script_path()
            dep_munger = create_dyn_dep_munger(self.build_opts, install_dirs)
            dep_dirs = self.get_dev_run_extra_path_dirs(install_dirs, dep_munger)
            # pyre-fixme[16]: Optional type has no attribute `emit_dev_run_script`.
            dep_munger.emit_dev_run_script(script_path, dep_dirs)

    @property
    def num_jobs(self) -> int:
        # This is a hack, but we don't have a "defaults manifest" that we can
        # customize per platform.
        # TODO: Introduce some sort of defaults config that can select by
        # platform, just like manifest contexts.
        if sys.platform.startswith("freebsd"):
            # clang on FreeBSD is quite memory-efficient.
            default_job_weight = 512
        else:
            # 1.5 GiB is a lot to assume, but it's typical of Facebook-style C++.
            # Some manifests are even heavier and should override.
            default_job_weight = 1536
        return self.build_opts.get_num_jobs(
            int(
                self.manifest.get(
                    "build", "job_weight_mib", default_job_weight, ctx=self.ctx
                )
            )
        )

    def run_tests(
        self, install_dirs, schedule_type, owner, test_filter, retry, no_testpilot
    ) -> None:
        """Execute any tests that we know how to run.  If they fail,
        raise an exception."""
        pass

    def _prepare(self, install_dirs, reconfigure) -> None:
        """Prepare the build. Useful when need to generate config,
        but builder is not the primary build system.
        e.g. cargo when called from cmake"""
        pass

    def _build(self, install_dirs, reconfigure) -> None:
        """Perform the build.
        install_dirs contains the list of installation directories for
        the dependencies of this project.
        reconfigure will be set to true if the fetcher determined
        that the sources have changed in such a way that the build
        system needs to regenerate its rules."""
        pass

    def _compute_env(self, install_dirs):
        # CMAKE_PREFIX_PATH is only respected when passed through the
        # environment, so we construct an appropriate path to pass down
        return self.build_opts.compute_env_for_install_dirs(
            install_dirs, env=self.env, manifest=self.manifest
        )

    def get_dev_run_script_path(self):
        assert self.build_opts.is_windows()
        return os.path.join(self.build_dir, "run.ps1")

    def get_dev_run_extra_path_dirs(self, install_dirs, dep_munger=None):
        assert self.build_opts.is_windows()
        if dep_munger is None:
            dep_munger = create_dyn_dep_munger(self.build_opts, install_dirs)
        return dep_munger.compute_dependency_paths(self.build_dir)


class MakeBuilder(BuilderBase):
    def __init__(
        self,
        build_opts,
        ctx,
        manifest,
        src_dir,
        build_dir,
        inst_dir,
        build_args,
        install_args,
        test_args,
    ) -> None:
        super(MakeBuilder, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )
        self.build_args = build_args or []
        self.install_args = install_args or []
        self.test_args = test_args

    @property
    def _make_binary(self):
        return self.manifest.get("build", "make_binary", "make", ctx=self.ctx)

    def _get_prefix(self):
        return ["PREFIX=" + self.inst_dir, "prefix=" + self.inst_dir]

    def _build(self, install_dirs, reconfigure) -> None:

        env = self._compute_env(install_dirs)

        # Need to ensure that PREFIX is set prior to install because
        # libbpf uses it when generating its pkg-config file.
        # The lowercase prefix is used by some projects.
        cmd = (
            [self._make_binary, "-j%s" % self.num_jobs]
            + self.build_args
            + self._get_prefix()
        )
        self._run_cmd(cmd, env=env)

        install_cmd = [self._make_binary] + self.install_args + self._get_prefix()
        self._run_cmd(install_cmd, env=env)

        # bz2's Makefile doesn't install its .so properly
        if self.manifest and self.manifest.name == "bz2":
            libdir = os.path.join(self.inst_dir, "lib")
            srcpattern = os.path.join(self.src_dir, "lib*.so.*")
            print(f"copying to {libdir} from {srcpattern}")
            for file in glob.glob(srcpattern):
                shutil.copy(file, libdir)

    def run_tests(
        self, install_dirs, schedule_type, owner, test_filter, retry, no_testpilot
    ) -> None:
        if not self.test_args:
            return

        env = self._compute_env(install_dirs)

        cmd = [self._make_binary] + self.test_args + self._get_prefix()
        self._run_cmd(cmd, env=env)


class CMakeBootStrapBuilder(MakeBuilder):
    def _build(self, install_dirs, reconfigure) -> None:
        self._run_cmd(
            [
                "./bootstrap",
                "--prefix=" + self.inst_dir,
                f"--parallel={self.num_jobs}",
            ]
        )
        super(CMakeBootStrapBuilder, self)._build(install_dirs, reconfigure)


class AutoconfBuilder(BuilderBase):
    def __init__(
        self,
        build_opts,
        ctx,
        manifest,
        src_dir,
        build_dir,
        inst_dir,
        args,
        conf_env_args,
    ) -> None:
        super(AutoconfBuilder, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )
        self.args = args or []
        self.conf_env_args = conf_env_args or {}

    @property
    def _make_binary(self):
        return self.manifest.get("build", "make_binary", "make", ctx=self.ctx)

    def _build(self, install_dirs, reconfigure) -> None:
        configure_path = os.path.join(self.src_dir, "configure")
        autogen_path = os.path.join(self.src_dir, "autogen.sh")

        env = self._compute_env(install_dirs)

        # Some configure scripts need additional env values passed derived from cmds
        for (k, cmd_args) in self.conf_env_args.items():
            out = (
                subprocess.check_output(cmd_args, env=dict(env.items()))
                .decode("utf-8")
                .strip()
            )
            if out:
                env.set(k, out)

        if not os.path.exists(configure_path):
            print("%s doesn't exist, so reconfiguring" % configure_path)
            # This libtoolize call is a bit gross; the issue is that
            # `autoreconf` as invoked by libsodium's `autogen.sh` doesn't
            # seem to realize that it should invoke libtoolize and then
            # error out when the configure script references a libtool
            # related symbol.
            self._run_cmd(["libtoolize"], cwd=self.src_dir, env=env)

            # We generally prefer to call the `autogen.sh` script provided
            # by the project on the basis that it may know more than plain
            # autoreconf does.
            if os.path.exists(autogen_path):
                self._run_cmd(["bash", autogen_path], cwd=self.src_dir, env=env)
            else:
                self._run_cmd(["autoreconf", "-ivf"], cwd=self.src_dir, env=env)
        configure_cmd = [configure_path, "--prefix=" + self.inst_dir] + self.args
        self._run_cmd(configure_cmd, env=env)
        self._run_cmd([self._make_binary, "-j%s" % self.num_jobs], env=env)
        self._run_cmd([self._make_binary, "install"], env=env)


class Iproute2Builder(BuilderBase):
    # ./configure --prefix does not work for iproute2.
    # Thus, explicitly copy sources from src_dir to build_dir, bulid,
    # and then install to inst_dir using DESTDIR
    # lastly, also copy include from build_dir to inst_dir
    def __init__(self, build_opts, ctx, manifest, src_dir, build_dir, inst_dir) -> None:
        super(Iproute2Builder, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )

    def _patch(self) -> None:
        # FBOSS build currently depends on an old version of iproute2 (commit
        # 7ca63aef7d1b0c808da0040c6b366ef7a61f38c1). This is missing a commit
        # (ae717baf15fb4d30749ada3948d9445892bac239) needed to build iproute2
        # successfully. Apply it viz.: include stdint.h
        # Reference: https://fburl.com/ilx9g5xm
        with open(self.build_dir + "/tc/tc_core.c", "r") as f:
            data = f.read()

        with open(self.build_dir + "/tc/tc_core.c", "w") as f:
            f.write("#include <stdint.h>\n")
            f.write(data)

    def _build(self, install_dirs, reconfigure) -> None:
        configure_path = os.path.join(self.src_dir, "configure")

        env = self.env.copy()
        self._run_cmd([configure_path], env=env)
        shutil.rmtree(self.build_dir)
        shutil.copytree(self.src_dir, self.build_dir)
        self._patch()
        self._run_cmd(["make", "-j%s" % self.num_jobs], env=env)
        install_cmd = ["make", "install", "DESTDIR=" + self.inst_dir]

        for d in ["include", "lib"]:
            if not os.path.isdir(os.path.join(self.inst_dir, d)):
                shutil.copytree(
                    os.path.join(self.build_dir, d), os.path.join(self.inst_dir, d)
                )

        self._run_cmd(install_cmd, env=env)


class CMakeBuilder(BuilderBase):
    MANUAL_BUILD_SCRIPT = """\
#!{sys.executable}


import argparse
import subprocess
import sys

CMAKE = {cmake!r}
CTEST = {ctest!r}
SRC_DIR = {src_dir!r}
BUILD_DIR = {build_dir!r}
INSTALL_DIR = {install_dir!r}
CMD_PREFIX = {cmd_prefix!r}
CMAKE_ENV = {env_str}
CMAKE_DEFINE_ARGS = {define_args_str}


def get_jobs_argument(num_jobs_arg: int) -> str:
    if num_jobs_arg > 0:
        return "-j" + str(num_jobs_arg)

    import multiprocessing
    num_jobs = multiprocessing.cpu_count() // 2
    return "-j" + str(num_jobs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
      "cmake_args",
      nargs=argparse.REMAINDER,
      help='Any extra arguments after an "--" argument will be passed '
      "directly to CMake."
    )
    ap.add_argument(
      "--mode",
      choices=["configure", "build", "install", "test"],
      default="configure",
      help="The mode to run: configure, build, or install.  "
      "Defaults to configure",
    )
    ap.add_argument(
      "--build",
      action="store_const",
      const="build",
      dest="mode",
      help="An alias for --mode=build",
    )
    ap.add_argument(
      "-j",
      "--num-jobs",
      action="store",
      type=int,
      default=0,
      help="Run the build or tests with the specified number of parallel jobs",
    )
    ap.add_argument(
      "--install",
      action="store_const",
      const="install",
      dest="mode",
      help="An alias for --mode=install",
    )
    ap.add_argument(
      "--test",
      action="store_const",
      const="test",
      dest="mode",
      help="An alias for --mode=test",
    )
    args = ap.parse_args()

    # Strip off a leading "--" from the additional CMake arguments
    if args.cmake_args and args.cmake_args[0] == "--":
        args.cmake_args = args.cmake_args[1:]

    env = CMAKE_ENV

    if args.mode == "configure":
        full_cmd = CMD_PREFIX + [CMAKE, SRC_DIR] + CMAKE_DEFINE_ARGS + args.cmake_args
    elif args.mode in ("build", "install"):
        target = "all" if args.mode == "build" else "install"
        full_cmd = CMD_PREFIX + [
                CMAKE,
                "--build",
                BUILD_DIR,
                "--target",
                target,
                "--config",
                "Release",
                get_jobs_argument(args.num_jobs),
        ] + args.cmake_args
    elif args.mode == "test":
        full_cmd = CMD_PREFIX + [
            {dev_run_script}CTEST,
            "--output-on-failure",
            get_jobs_argument(args.num_jobs),
        ] + args.cmake_args
    else:
        ap.error("unknown invocation mode: %s" % (args.mode,))

    cmd_str = " ".join(full_cmd)
    print("Running: %r" % (cmd_str,))
    proc = subprocess.run(full_cmd, env=env, cwd=BUILD_DIR)
    sys.exit(proc.returncode)


if __name__ == "__main__":
    main()
"""

    def __init__(
        self,
        build_opts,
        ctx,
        manifest,
        src_dir,
        build_dir,
        inst_dir,
        defines,
        loader=None,
        final_install_prefix=None,
        extra_cmake_defines=None,
        cmake_target="install",
    ) -> None:
        super(CMakeBuilder, self).__init__(
            build_opts,
            ctx,
            manifest,
            src_dir,
            build_dir,
            inst_dir,
            final_install_prefix=final_install_prefix,
        )
        self.defines = defines or {}
        if extra_cmake_defines:
            self.defines.update(extra_cmake_defines)
        self.cmake_target = cmake_target

        try:
            from .facebook.vcvarsall import extra_vc_cmake_defines
        except ImportError:
            pass
        else:
            self.defines.update(extra_vc_cmake_defines)

        self.loader = loader
        if build_opts.shared_libs:
            self.defines["BUILD_SHARED_LIBS"] = "ON"

    def _invalidate_cache(self) -> None:
        for name in [
            "CMakeCache.txt",
            "CMakeFiles/CMakeError.log",
            "CMakeFiles/CMakeOutput.log",
        ]:
            name = os.path.join(self.build_dir, name)
            if os.path.isdir(name):
                shutil.rmtree(name)
            elif os.path.exists(name):
                os.unlink(name)

    def _needs_reconfigure(self) -> bool:
        for name in ["CMakeCache.txt", "build.ninja"]:
            name = os.path.join(self.build_dir, name)
            if not os.path.exists(name):
                return True
        return False

    def _write_build_script(self, **kwargs) -> None:
        env_lines = ["    {!r}: {!r},".format(k, v) for k, v in kwargs["env"].items()]
        kwargs["env_str"] = "\n".join(["{"] + env_lines + ["}"])

        if self.build_opts.is_windows():
            kwargs["dev_run_script"] = '"powershell.exe", {!r}, '.format(
                self.get_dev_run_script_path()
            )
        else:
            kwargs["dev_run_script"] = ""

        define_arg_lines = ["["]
        for arg in kwargs["define_args"]:
            # Replace the CMAKE_INSTALL_PREFIX argument to use the INSTALL_DIR
            # variable that we define in the MANUAL_BUILD_SCRIPT code.
            if arg.startswith("-DCMAKE_INSTALL_PREFIX="):
                value = "    {!r}.format(INSTALL_DIR),".format(
                    "-DCMAKE_INSTALL_PREFIX={}"
                )
            else:
                value = "    {!r},".format(arg)
            define_arg_lines.append(value)
        define_arg_lines.append("]")
        kwargs["define_args_str"] = "\n".join(define_arg_lines)

        # In order to make it easier for developers to manually run builds for
        # CMake-based projects, write out some build scripts that can be used to invoke
        # CMake manually.
        build_script_path = os.path.join(self.build_dir, "run_cmake.py")
        script_contents = self.MANUAL_BUILD_SCRIPT.format(**kwargs)
        with open(build_script_path, "wb") as f:
            f.write(script_contents.encode())
        os.chmod(build_script_path, 0o755)

    def _compute_cmake_define_args(self, env):
        defines = {
            "CMAKE_INSTALL_PREFIX": self.final_install_prefix or self.inst_dir,
            "BUILD_SHARED_LIBS": "OFF",
            # Some of the deps (rsocket) default to UBSAN enabled if left
            # unspecified.  Some of the deps fail to compile in release mode
            # due to warning->error promotion.  RelWithDebInfo is the happy
            # medium.
            "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        }
        if "SANDCASTLE" not in os.environ:
            # We sometimes see intermittent ccache related breakages on some
            # of the FB internal CI hosts, so we prefer to disable ccache
            # when running in that environment.
            ccache = path_search(env, "ccache")
            if ccache:
                defines["CMAKE_CXX_COMPILER_LAUNCHER"] = ccache
        else:
            # rocksdb does its own probing for ccache.
            # Ensure that it is disabled on sandcastle
            env["CCACHE_DISABLE"] = "1"
            # Some sandcastle hosts have broken ccache related dirs, and
            # even though we've asked for it to be disabled ccache is
            # still invoked by rocksdb's cmake.
            # Redirect its config directory to somewhere that is guaranteed
            # fresh to us, and that won't have any ccache data inside.
            env["CCACHE_DIR"] = f"{self.build_opts.scratch_dir}/ccache"

        if "GITHUB_ACTIONS" in os.environ and self.build_opts.is_windows():
            # GitHub actions: the host has both gcc and msvc installed, and
            # the default behavior of cmake is to prefer gcc.
            # Instruct cmake that we want it to use cl.exe; this is important
            # because Boost prefers cl.exe and the mismatch results in cmake
            # with gcc not being able to find boost built with cl.exe.
            defines["CMAKE_C_COMPILER"] = "cl.exe"
            defines["CMAKE_CXX_COMPILER"] = "cl.exe"

        if self.build_opts.is_darwin():
            # Try to persuade cmake to set the rpath to match the lib
            # dirs of the dependencies.  This isn't automatic, and to
            # make things more interesting, cmake uses `;` as the path
            # separator, so translate the runtime path to something
            # that cmake will parse
            defines["CMAKE_INSTALL_RPATH"] = ";".join(
                env.get("DYLD_LIBRARY_PATH", "").split(":")
            )
            # Tell cmake that we want to set the rpath in the tree
            # at build time.  Without this the rpath is only set
            # at the moment that the binaries are installed.  That
            # default is problematic for example when using the
            # gtest integration in cmake which runs the built test
            # executables during the build to discover the set of
            # tests.
            defines["CMAKE_BUILD_WITH_INSTALL_RPATH"] = "ON"

        boost_169_is_required = False
        if self.loader:
            for m in self.loader.manifests_in_dependency_order():
                preinstalled = m.get_section_as_dict("preinstalled.env", self.ctx)
                boost_169_is_required = "BOOST_ROOT_1_69_0" in preinstalled.keys()
                if boost_169_is_required:
                    break

        if (
            boost_169_is_required
            and self.build_opts.allow_system_packages
            and self.build_opts.host_type.get_package_manager()
            and self.build_opts.host_type.get_package_manager() == "rpm"
        ):
            # Boost 1.69 rpms don't install cmake config to the system, so to point to them explicitly
            defines["BOOST_INCLUDEDIR"] = "/usr/include/boost169"
            defines["BOOST_LIBRARYDIR"] = "/usr/lib64/boost169"

        defines.update(self.defines)
        define_args = ["-D%s=%s" % (k, v) for (k, v) in defines.items()]

        # if self.build_opts.is_windows():
        #    define_args += ["-G", "Visual Studio 15 2017 Win64"]
        define_args += ["-G", "Ninja"]

        return define_args

    def _build(self, install_dirs, reconfigure: bool) -> None:
        reconfigure = reconfigure or self._needs_reconfigure()

        env = self._compute_env(install_dirs)
        if not self.build_opts.is_windows() and self.final_install_prefix:
            env["DESTDIR"] = self.inst_dir

        # Resolve the cmake that we installed
        cmake = path_search(env, "cmake")
        if cmake is None:
            raise Exception("Failed to find CMake")

        if reconfigure:
            define_args = self._compute_cmake_define_args(env)
            self._write_build_script(
                cmd_prefix=self._get_cmd_prefix(),
                cmake=cmake,
                ctest=path_search(env, "ctest"),
                env=env,
                define_args=define_args,
                src_dir=self.src_dir,
                build_dir=self.build_dir,
                install_dir=self.inst_dir,
                sys=sys,
            )

            self._invalidate_cache()
            self._run_cmd([cmake, self.src_dir] + define_args, env=env)

        self._run_cmd(
            [
                cmake,
                "--build",
                self.build_dir,
                "--target",
                self.cmake_target,
                "--config",
                "Release",
                "-j",
                str(self.num_jobs),
            ],
            env=env,
        )

    def run_tests(
        self, install_dirs, schedule_type, owner, test_filter, retry: int, no_testpilot
    ) -> None:
        env = self._compute_env(install_dirs)
        ctest = path_search(env, "ctest")
        cmake = path_search(env, "cmake")

        def require_command(path: Optional[str], name: str) -> str:
            if path is None:
                raise RuntimeError("unable to find command `{}`".format(name))
            return path

        # On Windows, we also need to update $PATH to include the directories that
        # contain runtime library dependencies.  This is not needed on other platforms
        # since CMake will emit RPATH properly in the binary so they can find these
        # dependencies.
        if self.build_opts.is_windows():
            path_entries = self.get_dev_run_extra_path_dirs(install_dirs)
            path = env.get("PATH")
            if path:
                path_entries.insert(0, path)
            env["PATH"] = ";".join(path_entries)

        # Don't use the cmd_prefix when running tests.  This is vcvarsall.bat on
        # Windows.  vcvarsall.bat is only needed for the build, not tests.  It
        # unfortunately fails if invoked with a long PATH environment variable when
        # running the tests.
        use_cmd_prefix = False

        def get_property(test, propname, defval=None):
            """extracts a named property from a cmake test info json blob.
            The properties look like:
            [{"name": "WORKING_DIRECTORY"},
             {"value": "something"}]
            We assume that it is invalid for the same named property to be
            listed more than once.
            """
            props = test.get("properties", [])
            for p in props:
                if p.get("name", None) == propname:
                    return p.get("value", defval)
            return defval

        def list_tests():
            output = subprocess.check_output(
                [require_command(ctest, "ctest"), "--show-only=json-v1"],
                env=env,
                cwd=self.build_dir,
            )
            try:
                data = json.loads(output.decode("utf-8"))
            except ValueError as exc:
                raise Exception(
                    "Failed to decode cmake test info using %s: %s.  Output was: %r"
                    % (ctest, str(exc), output)
                )

            tests = []
            machine_suffix = self.build_opts.host_type.as_tuple_string()
            for test in data["tests"]:
                working_dir = get_property(test, "WORKING_DIRECTORY")
                labels = []
                machine_suffix = self.build_opts.host_type.as_tuple_string()
                labels.append("tpx-fb-test-type=3")
                labels.append("tpx_test_config::buildsystem=getdeps")
                labels.append("tpx_test_config::platform={}".format(machine_suffix))

                if get_property(test, "DISABLED"):
                    labels.append("disabled")
                command = test["command"]
                if working_dir:
                    command = [
                        require_command(cmake, "cmake"),
                        "-E",
                        "chdir",
                        working_dir,
                    ] + command

                import os

                tests.append(
                    {
                        "type": "custom",
                        "target": "%s-%s-getdeps-%s"
                        % (self.manifest.name, test["name"], machine_suffix),
                        "command": command,
                        "labels": labels,
                        "env": {},
                        "required_paths": [],
                        "contacts": [],
                        "cwd": os.getcwd(),
                    }
                )
            return tests

        if schedule_type == "continuous" or schedule_type == "testwarden":
            # for continuous and testwarden runs, disabling retry can give up
            # better signals for flaky tests.
            retry = 0

        tpx = path_search(env, "tpx")
        if tpx and not no_testpilot:
            buck_test_info = list_tests()
            import os

            from .facebook.testinfra import start_run

            buck_test_info_name = os.path.join(self.build_dir, ".buck-test-info.json")
            with open(buck_test_info_name, "w") as f:
                json.dump(buck_test_info, f)

            env.set("http_proxy", "")
            env.set("https_proxy", "")
            runs = []
            from sys import platform

            with start_run(env["FBSOURCE_HASH"]) as run_id:
                testpilot_args = [
                    tpx,
                    "--force-local-execution",
                    "--buck-test-info",
                    buck_test_info_name,
                    "--retry=%d" % retry,
                    "-j=%s" % str(self.num_jobs),
                    "--print-long-results",
                ]

                if owner:
                    testpilot_args += ["--contacts", owner]

                if env:
                    testpilot_args.append("--env")
                    testpilot_args.extend(f"{key}={val}" for key, val in env.items())

                if run_id is not None:
                    testpilot_args += ["--run-id", run_id]

                if test_filter:
                    testpilot_args += ["--", test_filter]

                if schedule_type == "diff":
                    runs.append(["--collection", "oss-diff", "--purpose", "diff"])
                elif schedule_type == "continuous":
                    runs.append(
                        [
                            "--tag-new-tests",
                            "--collection",
                            "oss-continuous",
                            "--purpose",
                            "continuous",
                        ]
                    )
                elif schedule_type == "testwarden":
                    # One run to assess new tests
                    runs.append(
                        [
                            "--tag-new-tests",
                            "--collection",
                            "oss-new-test-stress",
                            "--stress-runs",
                            "10",
                            "--purpose",
                            "stress-run-new-test",
                        ]
                    )
                    # And another for existing tests
                    runs.append(
                        [
                            "--tag-new-tests",
                            "--collection",
                            "oss-existing-test-stress",
                            "--stress-runs",
                            "10",
                            "--purpose",
                            "stress-run",
                        ]
                    )
                else:
                    runs.append([])

                for run in runs:
                    self._run_cmd(
                        testpilot_args + run,
                        cwd=self.build_opts.fbcode_builder_dir,
                        env=env,
                        use_cmd_prefix=use_cmd_prefix,
                    )
        else:
            args = [
                require_command(ctest, "ctest"),
                "--output-on-failure",
                "-j",
                str(self.num_jobs),
            ]
            if test_filter:
                args += ["-R", test_filter]

            count = 0
            while count <= retry:
                retcode = self._run_cmd(
                    args, env=env, use_cmd_prefix=use_cmd_prefix, allow_fail=True
                )

                if retcode == 0:
                    break
                if count == 0:
                    # Only add this option in the second run.
                    args += ["--rerun-failed"]
                count += 1
            # pyre-fixme[61]: `retcode` is undefined, or not always defined.
            if retcode != 0:
                # Allow except clause in getdeps.main to catch and exit gracefully
                # This allows non-testpilot runs to fail through the same logic as failed testpilot runs, which may become handy in case if post test processing is needed in the future
                # pyre-fixme[61]: `retcode` is undefined, or not always defined.
                raise subprocess.CalledProcessError(retcode, args)


class NinjaBootstrap(BuilderBase):
    def __init__(self, build_opts, ctx, manifest, build_dir, src_dir, inst_dir) -> None:
        super(NinjaBootstrap, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )

    def _build(self, install_dirs, reconfigure) -> None:
        self._run_cmd([sys.executable, "configure.py", "--bootstrap"], cwd=self.src_dir)
        src_ninja = os.path.join(self.src_dir, "ninja")
        dest_ninja = os.path.join(self.inst_dir, "bin/ninja")
        bin_dir = os.path.dirname(dest_ninja)
        if not os.path.exists(bin_dir):
            os.makedirs(bin_dir)
        shutil.copyfile(src_ninja, dest_ninja)
        shutil.copymode(src_ninja, dest_ninja)


class OpenSSLBuilder(BuilderBase):
    def __init__(self, build_opts, ctx, manifest, build_dir, src_dir, inst_dir) -> None:
        super(OpenSSLBuilder, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )

    def _build(self, install_dirs, reconfigure) -> None:
        configure = os.path.join(self.src_dir, "Configure")

        # prefer to resolve the perl that we installed from
        # our manifest on windows, but fall back to the system
        # path on eg: darwin
        env = self.env.copy()
        for d in install_dirs:
            bindir = os.path.join(d, "bin")
            add_path_entry(env, "PATH", bindir, append=False)

        perl = typing.cast(str, path_search(env, "perl", "perl"))

        make_j_args = []
        if self.build_opts.is_windows():
            make = "nmake.exe"
            args = ["VC-WIN64A-masm", "-utf-8"]
        elif self.build_opts.is_darwin():
            make = "make"
            make_j_args = ["-j%s" % self.num_jobs]
            args = (
                ["darwin64-x86_64-cc"]
                if not self.build_opts.is_arm()
                else ["darwin64-arm64-cc"]
            )
        elif self.build_opts.is_linux():
            make = "make"
            make_j_args = ["-j%s" % self.num_jobs]
            args = (
                ["linux-x86_64"] if not self.build_opts.is_arm() else ["linux-aarch64"]
            )
        else:
            raise Exception("don't know how to build openssl for %r" % self.ctx)

        self._run_cmd(
            [
                perl,
                configure,
                "--prefix=%s" % self.inst_dir,
                "--openssldir=%s" % self.inst_dir,
            ]
            + args
            + [
                "enable-static-engine",
                "enable-capieng",
                "no-makedepend",
                "no-unit-test",
                "no-tests",
            ]
        )
        make_build = [make] + make_j_args
        self._run_cmd(make_build)
        make_install = [make, "install_sw", "install_ssldirs"]
        self._run_cmd(make_install)


class Boost(BuilderBase):
    def __init__(
        self, build_opts, ctx, manifest, src_dir, build_dir, inst_dir, b2_args
    ) -> None:
        children = os.listdir(src_dir)
        assert len(children) == 1, "expected a single directory entry: %r" % (children,)
        boost_src = children[0]
        assert boost_src.startswith("boost")
        src_dir = os.path.join(src_dir, children[0])
        super(Boost, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )
        self.b2_args = b2_args

    def _build(self, install_dirs, reconfigure) -> None:
        env = self._compute_env(install_dirs)
        linkage = ["static"]
        if self.build_opts.is_windows() or self.build_opts.shared_libs:
            linkage.append("shared")

        args = []
        if self.build_opts.is_darwin():
            clang = subprocess.check_output(["xcrun", "--find", "clang"])
            user_config = os.path.join(self.build_dir, "project-config.jam")
            with open(user_config, "w") as jamfile:
                jamfile.write("using clang : : %s ;\n" % clang.decode().strip())
            args.append("--user-config=%s" % user_config)

        for link in linkage:
            bootstrap_args = self.manifest.get_section_as_args(
                "bootstrap.args", self.ctx
            )
            if self.build_opts.is_windows():
                bootstrap = os.path.join(self.src_dir, "bootstrap.bat")
                self._run_cmd([bootstrap] + bootstrap_args, cwd=self.src_dir, env=env)
                args += ["address-model=64"]
            else:
                bootstrap = os.path.join(self.src_dir, "bootstrap.sh")
                self._run_cmd(
                    [bootstrap, "--prefix=%s" % self.inst_dir] + bootstrap_args,
                    cwd=self.src_dir,
                    env=env,
                )

            b2 = os.path.join(self.src_dir, "b2")
            self._run_cmd(
                [
                    b2,
                    "-j%s" % self.num_jobs,
                    "--prefix=%s" % self.inst_dir,
                    "--builddir=%s" % self.build_dir,
                ]
                + args
                + self.b2_args
                + [
                    "link=%s" % link,
                    "runtime-link=shared",
                    "variant=release",
                    "threading=multi",
                    "debug-symbols=on",
                    "visibility=global",
                    "-d2",
                    "install",
                ],
                cwd=self.src_dir,
                env=env,
            )


class NopBuilder(BuilderBase):
    def __init__(self, build_opts, ctx, manifest, src_dir, inst_dir) -> None:
        super(NopBuilder, self).__init__(
            build_opts, ctx, manifest, src_dir, None, inst_dir
        )

    def build(self, install_dirs, reconfigure: bool) -> None:
        print("Installing %s -> %s" % (self.src_dir, self.inst_dir))
        parent = os.path.dirname(self.inst_dir)
        if not os.path.exists(parent):
            os.makedirs(parent)

        install_files = self.manifest.get_section_as_ordered_pairs(
            "install.files", self.ctx
        )
        if install_files:
            for src_name, dest_name in self.manifest.get_section_as_ordered_pairs(
                "install.files", self.ctx
            ):
                full_dest = os.path.join(self.inst_dir, dest_name)
                full_src = os.path.join(self.src_dir, src_name)

                dest_parent = os.path.dirname(full_dest)
                if not os.path.exists(dest_parent):
                    os.makedirs(dest_parent)
                if os.path.isdir(full_src):
                    if not os.path.exists(full_dest):
                        shutil.copytree(full_src, full_dest)
                else:
                    shutil.copyfile(full_src, full_dest)
                    shutil.copymode(full_src, full_dest)
                    # This is a bit gross, but the mac ninja.zip doesn't
                    # give ninja execute permissions, so force them on
                    # for things that look like they live in a bin dir
                    if os.path.dirname(dest_name) == "bin":
                        st = os.lstat(full_dest)
                        os.chmod(full_dest, st.st_mode | stat.S_IXUSR)
        else:
            if not os.path.exists(self.inst_dir):
                shutil.copytree(self.src_dir, self.inst_dir)


class OpenNSABuilder(NopBuilder):
    # OpenNSA libraries are stored with git LFS. As a result, fetcher fetches
    # LFS pointers and not the contents. Use git-lfs to pull the real contents
    # before copying to install dir using NoopBuilder.
    # In future, if more builders require git-lfs, we would consider installing
    # git-lfs as part of the sandcastle infra as against repeating similar
    # logic for each builder that requires git-lfs.
    def __init__(self, build_opts, ctx, manifest, src_dir, inst_dir) -> None:
        super(OpenNSABuilder, self).__init__(
            build_opts, ctx, manifest, src_dir, inst_dir
        )

    def build(self, install_dirs, reconfigure: bool) -> None:
        env = self._compute_env(install_dirs)
        self._run_cmd(["git", "lfs", "install", "--local"], cwd=self.src_dir, env=env)
        self._run_cmd(["git", "lfs", "pull"], cwd=self.src_dir, env=env)

        super(OpenNSABuilder, self).build(install_dirs, reconfigure)


class SqliteBuilder(BuilderBase):
    def __init__(self, build_opts, ctx, manifest, src_dir, build_dir, inst_dir) -> None:
        super(SqliteBuilder, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )

    def _build(self, install_dirs, reconfigure) -> None:
        for f in ["sqlite3.c", "sqlite3.h", "sqlite3ext.h"]:
            src = os.path.join(self.src_dir, f)
            dest = os.path.join(self.build_dir, f)
            copy_if_different(src, dest)

        cmake_lists = """
cmake_minimum_required(VERSION 3.1.3 FATAL_ERROR)
project(sqlite3 C)
add_library(sqlite3 STATIC sqlite3.c)
# These options are taken from the defaults in Makefile.msc in
# the sqlite distribution
target_compile_definitions(sqlite3 PRIVATE
    -DSQLITE_ENABLE_COLUMN_METADATA=1
    -DSQLITE_ENABLE_FTS3=1
    -DSQLITE_ENABLE_RTREE=1
    -DSQLITE_ENABLE_GEOPOLY=1
    -DSQLITE_ENABLE_JSON1=1
    -DSQLITE_ENABLE_STMTVTAB=1
    -DSQLITE_ENABLE_DBPAGE_VTAB=1
    -DSQLITE_ENABLE_DBSTAT_VTAB=1
    -DSQLITE_INTROSPECTION_PRAGMAS=1
    -DSQLITE_ENABLE_DESERIALIZE=1
)
install(TARGETS sqlite3)
install(FILES sqlite3.h sqlite3ext.h DESTINATION include)
            """

        with open(os.path.join(self.build_dir, "CMakeLists.txt"), "w") as f:
            f.write(cmake_lists)

        defines = {
            "CMAKE_INSTALL_PREFIX": self.inst_dir,
            "BUILD_SHARED_LIBS": "ON" if self.build_opts.shared_libs else "OFF",
            "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        }
        define_args = ["-D%s=%s" % (k, v) for (k, v) in defines.items()]
        define_args += ["-G", "Ninja"]

        env = self._compute_env(install_dirs)

        # Resolve the cmake that we installed
        cmake = path_search(env, "cmake")

        self._run_cmd([cmake, self.build_dir] + define_args, env=env)
        self._run_cmd(
            [
                cmake,
                "--build",
                self.build_dir,
                "--target",
                "install",
                "--config",
                "Release",
                "-j",
                str(self.num_jobs),
            ],
            env=env,
        )
