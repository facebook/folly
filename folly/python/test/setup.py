#!/usr/bin/env python3

import os, sys, platform, re
from Cython.Build import cythonize
from Cython.Compiler import Options
from setuptools import Extension, setup

# c=++20 for coroutines
compile_args = ['-std=c++20']

# using local and folly includes
include_dirs = ['.', '..']

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

# Add (prepend) library paths from LD_LIBRARY_PATHs and calculate include paths
if ldpath := os.environ.get('LD_LIBRARY_PATH'):
    library_dirs[:0] = ldpath.split(':')
    include_dirs.extend(path for path in (re.sub(r'/lib$', '/include', p) for p in ldpath.split(':')))

exts = [
    Extension(
        'simplebridge',
        sources=[
            'simplebridge.pyx',
        #    '../executor_intf.cpp',
            '../error.cpp', 
        #    '../fibers.cpp'
        ],
        depends=['simple.h'],
        language='c++', 
        extra_compile_args=compile_args,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=base_libraries,
    ),
    Extension(
        'iobuf_helper',
        sources=[
            'iobuf_helper.pyx',
            'IOBufTestUtils.cpp',
            '../iobuf_intf.cpp',
            '../error.cpp', 
        ],
        depends=[
            'iobuf_helper.pxd',
            'IOBufTestUtils.h',
        ],
        language='c++',
        extra_compile_args=compile_args,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=base_libraries,
    ),
    Extension(
        'simplebridgecoro',
        sources=[
            'simplebridgecoro.pyx',
        #    '../executor_intf.cpp',
            '../error.cpp', 
        ],
        depends=['simplecoro.h'],
        language='c++',
        extra_compile_args=compile_args,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=base_libraries,
    ),
    Extension(
        'simplegenerator',
        sources=['simplegenerator.pyx'],
        depends=['simplegenerator.h'],
        language='c++',
        extra_compile_args=compile_args,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=base_libraries,
    ),
    Extension(
        'test_set_executor_cython',
        sources=['test_set_executor_cython.pyx'],
        depends=['test_set_executor.h'],
        language='c++',
        extra_compile_args=compile_args,
        include_dirs=include_dirs,
        library_dirs=library_dirs,
        libraries=base_libraries,
    ),
]

Options.fast_fail = True

setup(
    name='folly_test',
    setup_requires=['cython'],
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