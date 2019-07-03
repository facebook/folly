#!/usr/bin/env python
# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals

import os
import re
import shutil
import subprocess
import sys
from struct import unpack

from .envfuncs import path_search


def copyfile(src, dest):
    shutil.copyfile(src, dest)
    shutil.copymode(src, dest)


class DepBase(object):
    def __init__(self, buildopts, install_dirs):
        self.buildopts = buildopts
        self.env = buildopts.compute_env_for_install_dirs(install_dirs)
        self.install_dirs = install_dirs
        self.processed_deps = set()

    def list_dynamic_deps(self, objfile):
        raise RuntimeError("list_dynamic_deps not implemented")

    def interesting_dep(self, d):
        return True

    def process_deps(self, destdir, final_install_prefix=None):
        if final_install_prefix is None:
            final_install_prefix = destdir

        if self.buildopts.is_windows():
            self.munged_lib_dir = os.path.join(final_install_prefix, "bin")
        else:
            self.munged_lib_dir = os.path.join(final_install_prefix, "lib")

        if not os.path.isdir(self.munged_lib_dir):
            os.makedirs(self.munged_lib_dir)

        # Look only at the things that got installed in the leaf package,
        # which will be the last entry in the install dirs list
        inst_dir = self.install_dirs[-1]
        print("Process deps under %s" % inst_dir, file=sys.stderr)

        for dir in ["bin", "lib", "lib64"]:
            src_dir = os.path.join(inst_dir, dir)
            if not os.path.isdir(src_dir):
                continue
            dest_dir = os.path.join(final_install_prefix, dir)
            if not os.path.exists(dest_dir):
                os.makedirs(dest_dir)

            for objfile in self.list_objs_in_dir(src_dir):
                print("Consider %s/%s" % (dir, objfile))
                dest_obj = os.path.join(dest_dir, objfile)
                copyfile(os.path.join(src_dir, objfile), dest_obj)
                self.munge_in_place(dest_obj)

    def munge_in_place(self, objfile):
        print("Munging %s" % objfile)
        for d in self.list_dynamic_deps(objfile):
            if not self.interesting_dep(d):
                continue

            # Resolve this dep: does it exist in any of our installation
            # directories?  If so, then it is a candidate for processing
            dep = self.resolve_loader_path(d)
            print("dep: %s -> %s" % (d, dep))
            if dep:
                dest_dep = os.path.join(self.munged_lib_dir, os.path.basename(dep))
                if dep not in self.processed_deps:
                    self.processed_deps.add(dep)
                    copyfile(dep, dest_dep)
                    self.munge_in_place(dest_dep)

                self.rewrite_dep(objfile, d, dep, dest_dep)

    def rewrite_dep(self, objfile, depname, old_dep, new_dep):
        raise RuntimeError("rewrite_dep not implemented")

    def resolve_loader_path(self, dep):
        if os.path.isabs(dep):
            return dep
        d = os.path.basename(dep)
        for inst_dir in self.install_dirs:
            for libdir in ["lib", "lib64"]:
                candidate = os.path.join(inst_dir, libdir, d)
                if os.path.exists(candidate):
                    return candidate
        return None

    def list_objs_in_dir(self, dir):
        objs = []
        for d in os.listdir(dir):
            if self.is_objfile(os.path.join(dir, d)):
                objs.append(os.path.normcase(d))

        return objs

    def is_objfile(self, objfile):
        return True


class ElfDeps(DepBase):
    def __init__(self, buildopts, install_dirs):
        super(ElfDeps, self).__init__(buildopts, install_dirs)
        self.patchelf = path_search(self.env, "patchelf")

    def list_dynamic_deps(self, objfile):
        out = (
            subprocess.check_output(
                [self.patchelf, "--print-needed", objfile], env=dict(self.env.items())
            )
            .decode("utf-8")
            .strip()
        )
        lines = out.split("\n")
        return lines

    def rewrite_dep(self, objfile, depname, old_dep, new_dep):
        subprocess.check_call(
            [self.patchelf, "--replace-needed", depname, new_dep, objfile]
        )

    def is_objfile(self, objfile):
        if not os.path.isfile(objfile):
            return False
        with open(objfile, "rb") as f:
            # https://en.wikipedia.org/wiki/Executable_and_Linkable_Format#File_header
            magic = f.read(4)
            return magic == b"\x7fELF"


# MACH-O magic number
MACH_MAGIC = 0xFEEDFACF


class MachDeps(DepBase):
    def interesting_dep(self, d):
        if d.startswith("/usr/lib/") or d.startswith("/System/"):
            return False
        return True

    def is_objfile(self, objfile):
        if not os.path.isfile(objfile):
            return False
        with open(objfile, "rb") as f:
            # mach stores the magic number in native endianness,
            # so unpack as native here and compare
            magic = unpack("I", f.read(4))[0]
            return magic == MACH_MAGIC

    def list_dynamic_deps(self, objfile):
        if not self.interesting_dep(objfile):
            return
        out = (
            subprocess.check_output(
                ["otool", "-L", objfile], env=dict(self.env.items())
            )
            .decode("utf-8")
            .strip()
        )
        lines = out.split("\n")
        deps = []
        for line in lines:
            m = re.match("\t(\\S+)\\s", line)
            if m:
                if os.path.basename(m.group(1)) != os.path.basename(objfile):
                    deps.append(os.path.normcase(m.group(1)))
        return deps

    def rewrite_dep(self, objfile, depname, old_dep, new_dep):
        if objfile.endswith(".dylib"):
            # Erase the original location from the id of the shared
            # object.  It doesn't appear to hurt to retain it, but
            # it does look weird, so let's rewrite it to be sure.
            subprocess.check_call(
                ["install_name_tool", "-id", os.path.basename(objfile), objfile]
            )
        subprocess.check_call(
            ["install_name_tool", "-change", depname, new_dep, objfile]
        )


def create_dyn_dep_munger(buildopts, install_dirs):
    if buildopts.is_linux():
        return ElfDeps(buildopts, install_dirs)
    if buildopts.is_darwin():
        return MachDeps(buildopts, install_dirs)
    if buildopts.is_windows():
        return DepBase(buildopts, install_dirs)
