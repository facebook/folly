#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import glob
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

    # final_install_prefix must be the equivalent path to `destdir` on the
    # installed system.  For example, if destdir is `/tmp/RANDOM/usr/local' which
    # is intended to map to `/usr/local` in the install image, then
    # final_install_prefix='/usr/local'.
    # If left unspecified, destdir will be used.
    def process_deps(self, destdir, final_install_prefix=None):
        if self.buildopts.is_windows():
            lib_dir = "bin"
        else:
            lib_dir = "lib"
        self.munged_lib_dir = os.path.join(destdir, lib_dir)

        final_lib_dir = os.path.join(final_install_prefix or destdir, lib_dir)

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
            dest_dir = os.path.join(destdir, dir)
            if not os.path.exists(dest_dir):
                os.makedirs(dest_dir)

            for objfile in self.list_objs_in_dir(src_dir):
                print("Consider %s/%s" % (dir, objfile))
                dest_obj = os.path.join(dest_dir, objfile)
                copyfile(os.path.join(src_dir, objfile), dest_obj)
                self.munge_in_place(dest_obj, final_lib_dir)

    def munge_in_place(self, objfile, final_lib_dir):
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
                    self.munge_in_place(dest_dep, final_lib_dir)

                self.rewrite_dep(objfile, d, dep, dest_dep, final_lib_dir)

    def rewrite_dep(self, objfile, depname, old_dep, new_dep, final_lib_dir):
        raise RuntimeError("rewrite_dep not implemented")

    def resolve_loader_path(self, dep):
        if os.path.isabs(dep):
            return dep
        d = os.path.basename(dep)
        for inst_dir in self.install_dirs:
            for libdir in ["bin", "lib", "lib64"]:
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


class WinDeps(DepBase):
    def __init__(self, buildopts, install_dirs):
        super(WinDeps, self).__init__(buildopts, install_dirs)
        self.dumpbin = self.find_dumpbin()

    def find_dumpbin(self):
        # Looking for dumpbin in the following hardcoded paths.
        # The registry option to find the install dir doesn't work anymore.
        globs = [
            (
                "C:/Program Files (x86)/"
                "Microsoft Visual Studio/"
                "*/*/VC/Tools/"
                "MSVC/*/bin/Hostx64/x64/dumpbin.exe"
            ),
            (
                "C:/Program Files (x86)/"
                "Common Files/"
                "Microsoft/Visual C++ for Python/*/"
                "VC/bin/dumpbin.exe"
            ),
            ("c:/Program Files (x86)/Microsoft Visual Studio */VC/bin/dumpbin.exe"),
        ]
        for pattern in globs:
            for exe in glob.glob(pattern):
                return exe

        raise RuntimeError("could not find dumpbin.exe")

    def list_dynamic_deps(self, exe):
        deps = []
        print("Resolve deps for %s" % exe)
        output = subprocess.check_output(
            [self.dumpbin, "/nologo", "/dependents", exe]
        ).decode("utf-8")

        lines = output.split("\n")
        for line in lines:
            m = re.match("\\s+(\\S+.dll)", line, re.IGNORECASE)
            if m:
                deps.append(m.group(1).lower())

        return deps

    def rewrite_dep(self, objfile, depname, old_dep, new_dep, final_lib_dir):
        # We can't rewrite on windows, but we will
        # place the deps alongside the exe so that
        # they end up in the search path
        pass

    # These are the Windows system dll, which we don't want to copy while
    # packaging.
    SYSTEM_DLLS = set(  # noqa: C405
        [
            "advapi32.dll",
            "dbghelp.dll",
            "kernel32.dll",
            "msvcp140.dll",
            "vcruntime140.dll",
            "ws2_32.dll",
            "ntdll.dll",
            "shlwapi.dll",
        ]
    )

    def interesting_dep(self, d):
        if "api-ms-win-crt" in d:
            return False
        if d in self.SYSTEM_DLLS:
            return False
        return True

    def is_objfile(self, objfile):
        if not os.path.isfile(objfile):
            return False
        if objfile.lower().endswith(".exe"):
            return True
        return False


class ElfDeps(DepBase):
    def __init__(self, buildopts, install_dirs):
        super(ElfDeps, self).__init__(buildopts, install_dirs)

        # We need patchelf to rewrite deps, so ensure that it is built...
        subprocess.check_call([sys.executable, sys.argv[0], "build", "patchelf"])
        # ... and that we know where it lives
        self.patchelf = os.path.join(
            subprocess.check_output(
                [sys.executable, sys.argv[0], "show-inst-dir", "patchelf"]
            ).strip(),
            "bin/patchelf",
        )

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

    def rewrite_dep(self, objfile, depname, old_dep, new_dep, final_lib_dir):
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
            header = f.read(4)
            if len(header) != 4:
                return False
            magic = unpack("I", header)[0]
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

    def rewrite_dep(self, objfile, depname, old_dep, new_dep, final_lib_dir):
        if objfile.endswith(".dylib"):
            # Erase the original location from the id of the shared
            # object.  It doesn't appear to hurt to retain it, but
            # it does look weird, so let's rewrite it to be sure.
            subprocess.check_call(
                ["install_name_tool", "-id", os.path.basename(objfile), objfile]
            )
        final_dep = os.path.join(
            final_lib_dir, os.path.relpath(new_dep, self.munged_lib_dir)
        )

        subprocess.check_call(
            ["install_name_tool", "-change", depname, final_dep, objfile]
        )


def create_dyn_dep_munger(buildopts, install_dirs):
    if buildopts.is_linux():
        return ElfDeps(buildopts, install_dirs)
    if buildopts.is_darwin():
        return MachDeps(buildopts, install_dirs)
    if buildopts.is_windows():
        return WinDeps(buildopts, install_dirs)
