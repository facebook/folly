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

from .envfuncs import Env, add_path_entry
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
