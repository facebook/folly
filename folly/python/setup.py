#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Do not call directly, use cmake
#
# Cython requires source files in a specific structure, the structure is
# created as tree of links to the real source files.

import argparse
import sys

import Cython
from Cython.Build import cythonize
from Cython.Compiler import Options
from setuptools import Extension, setup


Options.fast_fail = True

def parse_build_mode(argv: list) -> tuple:
    """
    Parse command line arguments to determine build mode.

    Args:
        argv: Command line arguments (typically sys.argv)

    Returns:
        Tuple of (is_api_only, remaining_args)
    """
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--api-only", action="store_true")
    args, remaining = parser.parse_known_args(argv[1:])
    return args.api_only, [argv[0]] + remaining


is_api_only, remaining_argv = parse_build_mode(sys.argv)
# Restore sys.argv with remaining args so setuptools sees its arguments
# (e.g., "build_ext", "-f", "-I/path") when setup() is called
sys.argv = remaining_argv

if is_api_only:
    # Invoke cython compiler directly instead of calling cythonize().
    # Generating *_api.h files only requires first stage of compilation
    # # from cython source -> cpp source.
    Cython.Compiler.Main.compile(
        "folly/executor.pyx",
        full_module_name="folly.executor",
        cplus=True,
        # include_path=[pkg_dir("folly")],
        language_level=3,
    )
    Cython.Compiler.Main.compile(
        "folly/iobuf.pyx",
        full_module_name="folly.iobuf",
        cplus=True,
        # include_path=[pkg_dir("folly")],
        language_level=3,
    )
else:
    exts = [
        Extension(
            "folly.executor",
            sources=[
                "folly/executor.pyx",
                "folly/ProactorExecutor.cpp",
            ],
            libraries=["folly_python_cpp", "folly", "glog"],

            # So the loader can find bundled .so next to the extension at runtime:
            runtime_library_dirs=['$ORIGIN'],            # Linux
            extra_link_args=['-Wl,-rpath,$ORIGIN'],      # Linux (some dists ignore runtime_library_dirs)
            # On macOS use: extra_link_args=['-Wl,-rpath,@loader_path']
        ),
        Extension(
            "folly.iobuf",
            sources=[
                "folly/iobuf.pyx",
                "folly/iobuf_ext.cpp",
            ],
            libraries=["folly_python_cpp", "folly", "glog"],

            # So the loader can find bundled .so next to the extension at runtime:
            runtime_library_dirs=['$ORIGIN'],            # Linux
            extra_link_args=['-Wl,-rpath,$ORIGIN'],      # Linux (some dists ignore runtime_library_dirs)
            # On macOS use: extra_link_args=['-Wl,-rpath,@loader_path']
        ),
    ]

    setup(
        name="folly",
        version="0.0.1",
        packages=["folly"],
        package_data={
            "": ["*.pxd", "*.h"],
            "folly": ["libfolly_python_cpp.so"],
        },
        # include_package_data=True,                           # (optional) works with MANIFEST.in too

        setup_requires=["cython"],
        zip_safe=False,
        ext_modules=cythonize(exts, compiler_directives={"language_level": 3}),
    )
