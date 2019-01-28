#!/usr/bin/env python3

# Do not call directly, use cmake
#
# Cython requires source files in a specific structure, the structure is
# created as tree of links to the real source files.

from setuptools import setup, Extension
from Cython.Build import cythonize
from Cython.Compiler import Options

Options.fast_fail = True

ext = Extension("folly.executor",
                sources=['folly/executor.pyx'],
                libraries=['folly_pic', 'glog', 'double-conversion', 'iberty'])

setup(name="folly",
      version='0.0.1',
      packages=['folly'],
      package_data={"": ['*.pxd', '*.h']},
      zip_safe=False,
      ext_modules=cythonize([ext],
                            compiler_directives={'language_level': 3, }))
