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

# https://github.com/cython/cython/issues/3262 deadlock with spawned multiprocessing even with nthreads=1
# import multiprocessing
# multiprocessing.set_start_method('fork', force=True)

# c=++20 for coroutines
compile_args = ['-std=c++20']

# using local and folly includes
include_dirs = ['.', '../..']

# Define base libraries needed across all platforms
base_libraries = ['folly', 'glog', 'double-conversion', 'fmt']

if sys.platform == 'darwin':
    from distutils import sysconfig
    cfg_vars = sysconfig.get_config_vars()
    for key in ('CFLAGS', 'CCSHARED', 'LDSHARED', 'LDCXXSHARED', 'PY_LDFLAGS', 'PY_CFLAGS', 'PY_CPPFLAGS'):
        if key in cfg_vars:
            cfg_vars[key] = cfg_vars[key].replace('-mmacosx-version-min=10.9', '-mmacosx-version-min=12')
    if platform.machine() == 'arm64':
        # Macos (arm64, homebrew path is different than intel)
        library_dirs = ['/opt/homebrew/lib']
    else:
        library_dirs = ['/usr/local/lib', '/usr/lib']
else:
    # Debian/Ubuntu
    library_dirs = ['/usr/lib', '/usr/lib/x86_64-linux-gnu']
    # add libunwind explicitly
    base_libraries.append('unwind')

# Set Python path from GETDEPS_INSTALL_DIR
folly_lib = None
if install_dir := os.environ.get('GETDEPS_INSTALL_DIR'):
    folly_lib = os.path.join(install_dir, 'folly', 'lib')
    pypath = os.path.join(
        folly_lib, 
        f'python{sys.version_info.major}.{sys.version_info.minor}', 
        'site-packages'
    )
    print(f"\nexport PYTHONPATH=\"{pypath}\"\n")
    os.environ['PYTHONPATH'] = pypath

# Add (prepend) library paths from LD_LIBRARY_PATHs
if ldpath := os.environ.get('LD_LIBRARY_PATH'):
    ldpaths = ldpath.split(':')
    library_dirs[:0] = ldpaths
    if folly_lib and folly_lib not in ldpaths:
        print(f'export LD_LIBRARY_PATH="{folly_lib}:{ldpath}"\n')
    else:
        print(f'export LD_LIBRARY_PATH="{ldpath}"\n')

exts = [
    Extension(
        'folly.executor',
        sources=[
            'folly/executor.pyx',
            'folly/executor_intf.cpp',
            'folly/ProactorExecutor.cpp',
            'folly/error.cpp',
        ],
        language='c++',
        extra_compile_args=compile_args,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=base_libraries,
    ),
    Extension(
        'folly.iobuf',
        sources=[
            'folly/iobuf.pyx',
            'folly/iobuf_ext.cpp',
            'folly/iobuf_intf.cpp',
            'folly/error.cpp',
        ],
        language='c++',
        extra_compile_args=compile_args,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=base_libraries,
    ),
    Extension(
        'folly.fiber_manager',
        sources=[
            'folly/fiber_manager.pyx',
            'folly/fibers.cpp',
            'folly/error.cpp',
        ],
        language='c++',
        extra_compile_args=compile_args,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=base_libraries+['boost_coroutine', 'boost_context', 'event'],
    ),
    Extension(
        'folly.build_mode',
        sources=['folly/build_mode.pyx'],
        language='c++',
        extra_compile_args=compile_args,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=base_libraries,
    ),
]

# os.environ['CYTHON_DEBUG']='1'

Options.fast_fail = True
if __name__ == '__main__':
    setup(
        name='folly',
        zip_safe=False,
        version='0.1.1',
        packages=['folly'],
        setup_requires=['cython'],
        headers=[
            'folly/AsyncioExecutor.h',
            'folly/ProactorExecutor.h',
            'folly/coro.h',
            'folly/futures.h',
            'folly/async_generator.h',
            'folly/executor_intf.h',
            'folly/iobuf_intf.h',
            'folly/iobuf_ext.h',
            'folly/error.h',
            'folly/import.h',
        ],
        package_data={'': ['*.pxd', '*.pyi', '__init__.py', '*_api.h']},
        ext_modules=cythonize(
            exts, 
            verbose=True,
            show_all_warnings=True,
            compiler_directives={
                'language_level': 3,
                'c_string_encoding': 'utf8'
            }
        ),
    )