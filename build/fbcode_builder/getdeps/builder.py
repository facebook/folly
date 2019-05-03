#!/usr/bin/env python
# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals

import glob
import os
import shutil

from .envfuncs import Env, add_path_entry, path_search
from .runcmd import run_cmd


class BuilderBase(object):
    def __init__(
        self, build_opts, ctx, manifest, src_dir, build_dir, inst_dir, env=None
    ):
        self.env = Env()
        if env:
            self.env.update(env)

        subdir = manifest.get("build", "subdir", ctx=ctx)
        if subdir:
            src_dir = os.path.join(src_dir, subdir)

        self.ctx = ctx
        self.src_dir = src_dir
        self.build_dir = build_dir or src_dir
        self.inst_dir = inst_dir
        self.build_opts = build_opts
        self.manifest = manifest

    def _run_cmd(self, cmd, cwd=None, env=None):
        if env:
            e = self.env.copy()
            e.update(env)
            env = e
        else:
            env = self.env

        if self.build_opts.is_windows():
            # On Windows, the compiler is not available in the PATH by default
            # so we need to run the vcvarsall script to populate the environment.
            # We use a glob to find some version of this script as deployed with
            # Visual Studio 2017.  This logic will need updating when we switch
            # to a newer compiler.
            vcvarsall = glob.glob(
                os.path.join(
                    os.environ["ProgramFiles(x86)"],
                    "Microsoft Visual Studio",
                    "2017",
                    "*",
                    "VC",
                    "Auxiliary",
                    "Build",
                    "vcvarsall.bat",
                )
            )

            if len(vcvarsall) > 0:
                # Since it sets rather a large number of variables we mildly abuse
                # the cmd quoting rules to assemble a command that calls the script
                # to prep the environment and then triggers the actual command that
                # we wanted to run.
                cmd = [vcvarsall[0], "amd64", "&&"] + cmd

        run_cmd(cmd=cmd, env=env, cwd=cwd or self.build_dir)

    def build(self, install_dirs, reconfigure):
        print("Building %s..." % self.manifest.name)

        if self.build_dir is not None:
            if not os.path.isdir(self.build_dir):
                os.makedirs(self.build_dir)
                reconfigure = True

        self._build(install_dirs=install_dirs, reconfigure=reconfigure)

    def _build(self, install_dirs, reconfigure):
        """ Perform the build.
        install_dirs contains the list of installation directories for
        the dependencies of this project.
        reconfigure will be set to true if the fetcher determined
        that the sources have changed in such a way that the build
        system needs to regenerate its rules. """
        pass


class MakeBuilder(BuilderBase):
    def __init__(self, build_opts, ctx, manifest, src_dir, build_dir, inst_dir, args):
        super(MakeBuilder, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )
        self.args = args or []

    def _build(self, install_dirs, reconfigure):
        cmd = ["make", "-j%s" % self.build_opts.num_jobs] + self.args
        self._run_cmd(cmd)

        install_cmd = ["make", "install", "PREFIX=" + self.inst_dir]
        self._run_cmd(install_cmd)


class AutoconfBuilder(BuilderBase):
    def __init__(self, build_opts, ctx, manifest, src_dir, build_dir, inst_dir, args):
        super(AutoconfBuilder, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )
        self.args = args or []

    def _build(self, install_dirs, reconfigure):
        configure_path = os.path.join(self.src_dir, "configure")
        autogen_path = os.path.join(self.src_dir, "autogen.sh")

        env = self.env.copy()
        for d in install_dirs:
            add_path_entry(env, "PKG_CONFIG_PATH", "%s/lib/pkgconfig" % d)
            bindir = os.path.join(d, "bin")
            add_path_entry(env, "PATH", bindir, append=False)

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
                self._run_cmd([autogen_path], cwd=self.src_dir, env=env)
            else:
                self._run_cmd(["autoreconf", "-ivf"], cwd=self.src_dir, env=env)
        configure_cmd = [configure_path, "--prefix=" + self.inst_dir] + self.args
        self._run_cmd(configure_cmd, env=env)
        self._run_cmd(["make", "-j%s" % self.build_opts.num_jobs], env=env)
        self._run_cmd(["make", "install"], env=env)


class CMakeBuilder(BuilderBase):
    def __init__(
        self, build_opts, ctx, manifest, src_dir, build_dir, inst_dir, defines
    ):
        super(CMakeBuilder, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )
        self.defines = defines or {}

    def _invalidate_cache(self):
        for name in ["CMakeCache.txt", "CMakeFiles"]:
            name = os.path.join(self.build_dir, name)
            if os.path.isdir(name):
                shutil.rmtree(name)
            elif os.path.exists(name):
                os.unlink(name)

    def _needs_reconfigure(self):
        for name in ["CMakeCache.txt", "build.ninja"]:
            name = os.path.join(self.build_dir, name)
            if not os.path.exists(name):
                return True
        return False

    def _build(self, install_dirs, reconfigure):
        reconfigure = reconfigure or self._needs_reconfigure()

        defines = {
            "CMAKE_INSTALL_PREFIX": self.inst_dir,
            "BUILD_SHARED_LIBS": "OFF",
            # Some of the deps (rsocket) default to UBSAN enabled if left
            # unspecified.  Some of the deps fail to compile in release mode
            # due to warning->error promotion.  RelWithDebInfo is the happy
            # medium.
            "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        }

        defines.update(self.defines)
        define_args = ["-D%s=%s" % (k, v) for (k, v) in defines.items()]

        # if self.build_opts.is_windows():
        #    define_args += ["-G", "Visual Studio 15 2017 Win64"]
        define_args += ["-G", "Ninja"]

        # CMAKE_PREFIX_PATH is only respected when passed through the
        # environment, so we construct an appropriate path to pass down
        env = self.env.copy()
        for d in install_dirs:
            add_path_entry(env, "CMAKE_PREFIX_PATH", d)
            add_path_entry(env, "PKG_CONFIG_PATH", "%s/lib/pkgconfig" % d)

            bindir = os.path.join(d, "bin")
            add_path_entry(env, "PATH", bindir, append=False)

        # Resolve the cmake that we installed
        cmake = path_search(env, "cmake")

        if reconfigure:
            self._invalidate_cache()
            self._run_cmd([cmake, self.src_dir] + define_args, env=env)

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
                str(self.build_opts.num_jobs),
            ],
            env=env,
        )
