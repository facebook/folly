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

from Cython.Build import cythonize
from Cython.Compiler import Options
from setuptools import Extension, setup


Options.fast_fail = True

exts = [
    Extension(
        "folly.executor",
        sources=["folly/executor.pyx", "folly/ProactorExecutor.cpp"],
        libraries=["folly", "glog"],
        extra_compile_args=[
            "-std=c++17",
        ],
    ),
    Extension(
        "folly.iobuf",
        sources=["folly/iobuf.pyx", "folly/iobuf_ext.cpp"],
        libraries=["folly", "glog"],
        extra_compile_args=[
            "-std=c++17",
        ],
    ),
]

setup(
    name="folly",
    version="0.0.1",
    packages=["folly"],
    package_data={"": ["*.pxd", "*.h"]},
    setup_requires=["cython"],
    zip_safe=False,
    ext_modules=cythonize(exts, compiler_directives={"language_level": 3}),
)
