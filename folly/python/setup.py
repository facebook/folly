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

import os, sys, platform
from Cython.Build import cythonize
from Cython.Compiler import Options
from setuptools import Extension, setup

if sys.platform == 'darwin' and platform.machine() == 'arm64':
    # Macos (arm64, homebrew path is different than intel)
    library_dirs = ['/opt/homebrew/lib']
else:
    # Debian/Ubuntu
    library_dirs = ['/usr/lib', '/usr/lib/x86_64-linux-gnu']
    
# Add (prepend) library paths from CMAKE_PREFIX_PATH
if cmake_prefix_path := os.environ.get('CMAKE_PREFIX_PATH'):
    library_dirs[:0] = list(os.path.join(p, 'lib') for p in cmake_prefix_path.split(':'))

# Set up the Python path   
if cmake_install_prefix := os.environ.get('CMAKE_INSTALL_PREFIX'):
    site_packages = os.path.join(cmake_prefix_path, 'lib',
        f'python{sys.version_info.major}.{sys.version_info.minor}',
        'site-packages'
    )
    os.environ['PYTHONPATH'] = site_packages

exts = [
    Extension(
        'folly.executor',
        sources=[
            'folly/executor.pyx',
            'folly/executor_intf.cpp',
            'folly/ProactorExecutor.cpp',
            'folly/error.cpp',
        ],
        libraries=['folly', 'glog', 'double-conversion','fmt'],
        extra_compile_args=['-std=c++20'],  # C++20 for coroutines
        include_dirs=['.', '../..'],  # cython generated code
        library_dirs=library_dirs,
    ),
    Extension(
        'folly.iobuf',
        sources=[
            'folly/iobuf.pyx',
            'folly/iobuf_intf.cpp',
            'folly/iobuf_ext.cpp',
            'folly/error.cpp',
        ],
        libraries=['folly', 'glog', 'double-conversion', 'fmt'],
        extra_compile_args=['-std=c++20'],  # C++20 for coroutines
        include_dirs=['.', '../..'],  # cython generated code
        library_dirs=library_dirs,
    ),
    Extension(
        'folly.fiber_manager',
        sources=[
            'folly/fiber_manager.pyx',
            'folly/fibers.cpp',
            'folly/error.cpp',
        ],
        libraries=['folly', "boost_coroutine", "boost_context",
                   'glog', 'double-conversion', 'fmt', 'event'],
        extra_compile_args=['-std=c++20'],  # C++20 for coroutines
        include_dirs=['.', '../..'],  # cython generated code
        library_dirs=library_dirs,
    )
]

Options.fast_fail = True
setup(
    name='folly',
    version='0.0.1',
    packages=['folly'],
    setup_requires=['cython'],
    zip_safe=False,
    package_data={'': ['*.pxd', '*.pyi', '*.h', '__init__.py']},
    ext_modules=cythonize(exts, compiler_directives={'language_level': 3}),
)