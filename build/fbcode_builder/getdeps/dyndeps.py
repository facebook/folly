# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# pyre-unsafe

import errno
import glob
import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
from struct import unpack
from typing import List, Optional

OBJECT_SUBDIRS = ("bin", "lib", "lib64")


def copyfile(src, dest) -> None:
    shutil.copyfile(src, dest)
    shutil.copymode(src, dest)


class DepBase(object):
    def __init__(self, buildopts, env, install_dirs, strip) -> None:
        self.buildopts = buildopts
        self.env = env
        self.install_dirs = install_dirs
        self.strip = strip

        # Deduplicates dependency processing. Keyed on the library
        # destination path.
        self.processed_deps = set()

    def list_dynamic_deps(self, objfile):
        raise RuntimeError("list_dynamic_deps not implemented")

    def interesting_dep(self, d) -> bool:
        return True

    # final_install_prefix must be the equivalent path to `destdir` on the
    # installed system.  For example, if destdir is `/tmp/RANDOM/usr/local' which
    # is intended to map to `/usr/local` in the install image, then
    # final_install_prefix='/usr/local'.
    # If left unspecified, destdir will be used.
    def process_deps(self, destdir, final_install_prefix=None) -> None:
        if self.buildopts.is_windows():
            lib_dir = "bin"
        else:
            lib_dir = "lib"
        # pyre-fixme[16]: `DepBase` has no attribute `munged_lib_dir`.
        self.munged_lib_dir = os.path.join(destdir, lib_dir)

        final_lib_dir = os.path.join(final_install_prefix or destdir, lib_dir)

        if not os.path.isdir(self.munged_lib_dir):
            os.makedirs(self.munged_lib_dir)

        # Look only at the things that got installed in the leaf package,
        # which will be the last entry in the install dirs list
        inst_dir = self.install_dirs[-1]
        print("Process deps under %s" % inst_dir, file=sys.stderr)

        for dir in OBJECT_SUBDIRS:
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

    def find_all_dependencies(self, build_dir):
        all_deps = set()
        for objfile in self.list_objs_in_dir(
            build_dir, recurse=True, output_prefix=build_dir
        ):
            for d in self.list_dynamic_deps(objfile):
                all_deps.add(d)

        interesting_deps = {d for d in all_deps if self.interesting_dep(d)}
        dep_paths = []
        for dep in interesting_deps:
            dep_path = self.resolve_loader_path(dep)
            if dep_path:
                dep_paths.append(dep_path)

        return dep_paths

    def munge_in_place(self, objfile, final_lib_dir) -> None:
        print("Munging %s" % objfile)
        for d in self.list_dynamic_deps(objfile):
            if not self.interesting_dep(d):
                continue

            # Resolve this dep: does it exist in any of our installation
            # directories?  If so, then it is a candidate for processing
            dep = self.resolve_loader_path(d)
            if dep:
                # pyre-fixme[16]: `DepBase` has no attribute `munged_lib_dir`.
                dest_dep = os.path.join(self.munged_lib_dir, os.path.basename(dep))
                print("dep: %s -> %s" % (d, dest_dep))
                if dest_dep in self.processed_deps:
                    # A previous dependency with the same name has already
                    # been installed at dest_dep, so there is no need to copy
                    # or munge the dependency again.
                    # TODO: audit that both source paths have the same inode number
                    pass
                else:
                    self.processed_deps.add(dest_dep)
                    copyfile(dep, dest_dep)
                    self.munge_in_place(dest_dep, final_lib_dir)

                self.rewrite_dep(objfile, d, dep, dest_dep, final_lib_dir)

        if self.strip:
            self.strip_debug_info(objfile)

    def rewrite_dep(self, objfile, depname, old_dep, new_dep, final_lib_dir):
        raise RuntimeError("rewrite_dep not implemented")

    def resolve_loader_path(self, dep: str) -> Optional[str]:
        if os.path.isabs(dep):
            return dep
        d = os.path.basename(dep)
        for inst_dir in self.install_dirs:
            for libdir in OBJECT_SUBDIRS:
                candidate = os.path.join(inst_dir, libdir, d)
                if os.path.exists(candidate):
                    return candidate
        return None

    def list_objs_in_dir(self, dir, recurse: bool = False, output_prefix: str = ""):
        for entry in os.listdir(dir):
            entry_path = os.path.join(dir, entry)
            st = os.lstat(entry_path)
            if stat.S_ISREG(st.st_mode):
                if self.is_objfile(entry_path):
                    relative_result = os.path.join(output_prefix, entry)
                    yield os.path.normcase(relative_result)
            elif recurse and stat.S_ISDIR(st.st_mode):
                child_prefix = os.path.join(output_prefix, entry)
                for result in self.list_objs_in_dir(
                    entry_path, recurse=recurse, output_prefix=child_prefix
                ):
                    yield result

    def is_objfile(self, objfile) -> bool:
        return True

    def strip_debug_info(self, objfile) -> None:
        """override this to define how to remove debug information
        from an object file"""
        pass

    def check_call_verbose(self, args: List[str]) -> None:
        print(" ".join(map(shlex.quote, args)))
        subprocess.check_call(args)


class WinDeps(DepBase):
    def __init__(self, buildopts, env, install_dirs, strip) -> None:
        super(WinDeps, self).__init__(buildopts, env, install_dirs, strip)
        self.dumpbin = self.find_dumpbin()

    def find_dumpbin(self) -> str:
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
            (
                "C:/Program Files/Microsoft Visual Studio/*/Professional/VC/Tools/MSVC/*/bin/HostX64/x64/dumpbin.exe"
            ),
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

    def rewrite_dep(self, objfile, depname, old_dep, new_dep, final_lib_dir) -> None:
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

    def interesting_dep(self, d) -> bool:
        if "api-ms-win-crt" in d:
            return False
        if d in self.SYSTEM_DLLS:
            return False
        return True

    def is_objfile(self, objfile) -> bool:
        if not os.path.isfile(objfile):
            return False
        if objfile.lower().endswith(".exe"):
            return True
        return False

    def emit_dev_run_script(self, script_path, dep_dirs) -> None:
        """Emit a script that can be used to run build artifacts directly from the
        build directory, without installing them.

        The dep_dirs parameter should be a list of paths that need to be added to $PATH.
        This can be computed by calling compute_dependency_paths() or
        compute_dependency_paths_fast().

        This is only necessary on Windows, which does not have RPATH, and instead
        requires the $PATH environment variable be updated in order to find the proper
        library dependencies.
        """
        contents = self._get_dev_run_script_contents(dep_dirs)
        with open(script_path, "w") as f:
            f.write(contents)

    def compute_dependency_paths(self, build_dir):
        """Return a list of all directories that need to be added to $PATH to ensure
        that library dependencies can be found correctly.  This is computed by scanning
        binaries to determine exactly the right list of dependencies.

        The compute_dependency_paths_fast() is a alternative function that runs faster
        but may return additional extraneous paths.
        """
        dep_dirs = set()
        # Find paths by scanning the binaries.
        for dep in self.find_all_dependencies(build_dir):
            dep_dirs.add(os.path.dirname(dep))

        dep_dirs.update(self.read_custom_dep_dirs(build_dir))
        return sorted(dep_dirs)

    def compute_dependency_paths_fast(self, build_dir):
        """Similar to compute_dependency_paths(), but rather than actually scanning
        binaries, just add all library paths from the specified installation
        directories.  This is much faster than scanning the binaries, but may result in
        more paths being returned than actually necessary.
        """
        dep_dirs = set()
        for inst_dir in self.install_dirs:
            for subdir in OBJECT_SUBDIRS:
                path = os.path.join(inst_dir, subdir)
                if os.path.exists(path):
                    dep_dirs.add(path)

        dep_dirs.update(self.read_custom_dep_dirs(build_dir))
        return sorted(dep_dirs)

    def read_custom_dep_dirs(self, build_dir):
        # The build system may also have included libraries from other locations that
        # we might not be able to find normally in find_all_dependencies().
        # To handle this situation we support reading additional library paths
        # from a LIBRARY_DEP_DIRS.txt file that may have been generated in the build
        # output directory.
        dep_dirs = set()
        try:
            explicit_dep_dirs_path = os.path.join(build_dir, "LIBRARY_DEP_DIRS.txt")
            with open(explicit_dep_dirs_path, "r") as f:
                for line in f.read().splitlines():
                    dep_dirs.add(line)
        except OSError as ex:
            if ex.errno != errno.ENOENT:
                raise

        return dep_dirs

    def _get_dev_run_script_contents(self, path_dirs) -> str:
        path_entries = ["$env:PATH"] + path_dirs
        path_str = ";".join(path_entries)
        return """\
$orig_env = $env:PATH
$env:PATH = "{path_str}"

try {{
    $cmd_args = $args[1..$args.length]
    & $args[0] @cmd_args
}} finally {{
    $env:PATH = $orig_env
}}
""".format(
            path_str=path_str
        )


class ElfDeps(DepBase):
    def __init__(self, buildopts, env, install_dirs, strip) -> None:
        super(ElfDeps, self).__init__(buildopts, env, install_dirs, strip)

        # We need patchelf to rewrite deps, so ensure that it is built...
        args = [sys.executable, sys.argv[0]]
        if buildopts.allow_system_packages:
            args.append("--allow-system-packages")
        subprocess.check_call(args + ["build", "patchelf"])

        # ... and that we know where it lives
        patchelf_install = os.fsdecode(
            subprocess.check_output(args + ["show-inst-dir", "patchelf"]).strip()
        )
        if not patchelf_install:
            # its a system package, so we assume it is in the path
            patchelf_install = "patchelf"
        else:
            patchelf_install = os.path.join(patchelf_install, "bin", "patchelf")
        self.patchelf = patchelf_install

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

    def rewrite_dep(self, objfile, depname, old_dep, new_dep, final_lib_dir) -> None:
        final_dep = os.path.join(
            final_lib_dir,
            # pyre-fixme[16]: `ElfDeps` has no attribute `munged_lib_dir`.
            os.path.relpath(new_dep, self.munged_lib_dir),
        )
        self.check_call_verbose(
            [self.patchelf, "--replace-needed", depname, final_dep, objfile]
        )

    def is_objfile(self, objfile) -> bool:
        if not os.path.isfile(objfile):
            return False
        with open(objfile, "rb") as f:
            # https://en.wikipedia.org/wiki/Executable_and_Linkable_Format#File_header
            magic = f.read(4)
            return magic == b"\x7fELF"

    def strip_debug_info(self, objfile) -> None:
        self.check_call_verbose(["strip", objfile])


# MACH-O magic number
MACH_MAGIC = 0xFEEDFACF


class MachDeps(DepBase):
    def interesting_dep(self, d) -> bool:
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

    def rewrite_dep(self, objfile, depname, old_dep, new_dep, final_lib_dir) -> None:
        if objfile.endswith(".dylib"):
            # Erase the original location from the id of the shared
            # object.  It doesn't appear to hurt to retain it, but
            # it does look weird, so let's rewrite it to be sure.
            self.check_call_verbose(
                ["install_name_tool", "-id", os.path.basename(objfile), objfile]
            )
        final_dep = os.path.join(
            final_lib_dir,
            # pyre-fixme[16]: `MachDeps` has no attribute `munged_lib_dir`.
            os.path.relpath(new_dep, self.munged_lib_dir),
        )

        self.check_call_verbose(
            ["install_name_tool", "-change", depname, final_dep, objfile]
        )


def create_dyn_dep_munger(
    buildopts, env, install_dirs, strip: bool = False
) -> Optional[DepBase]:
    if buildopts.is_linux():
        return ElfDeps(buildopts, env, install_dirs, strip)
    if buildopts.is_darwin():
        return MachDeps(buildopts, env, install_dirs, strip)
    if buildopts.is_windows():
        return WinDeps(buildopts, env, install_dirs, strip)
    if buildopts.is_freebsd():
        return ElfDeps(buildopts, env, install_dirs, strip)
    return None
