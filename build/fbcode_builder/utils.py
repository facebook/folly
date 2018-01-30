#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
'Miscellaneous utility functions.'

import itertools
import logging
import os
import subprocess
import sys

from contextlib import contextmanager


def recursively_flatten_list(l):
    return itertools.chain.from_iterable(
        (recursively_flatten_list(i) if type(i) is list else (i,))
            for i in l
    )


def run_command(*cmd, **kwargs):
    'The stdout of most fbcode_builder utilities is meant to be parsed.'
    logging.debug('Running: {0} with {1}'.format(cmd, kwargs))
    kwargs['stdout'] = sys.stderr
    subprocess.check_call(cmd, **kwargs)


@contextmanager
def make_temp_dir(d):
    os.mkdir(d)
    try:
        yield d
    finally:
        if os.path.exists(d):
            os.rmdir(d)


@contextmanager
def push_dir(d):
    old_dir = os.getcwd()
    os.chdir(d)
    try:
        yield d
    finally:
        os.chdir(old_dir)


def read_fbcode_builder_config(filename):
    # Allow one spec to read another
    scope = {'read_fbcode_builder_config': read_fbcode_builder_config}
    with open(filename) as config_file:
        exec(config_file.read(), scope)
    return scope['config']


def steps_for_spec(builder, spec, processed_modules=None):
    '''
    Sets `builder` configuration, and returns all the builder steps
    necessary to build `spec` and its dependencies.

    Traverses the dependencies in depth-first order, honoring the sequencing
    in each 'depends_on' list.
    '''
    if processed_modules is None:
        processed_modules = set()
    steps = []
    for module in spec.get('depends_on', []):
        if module not in processed_modules:
            processed_modules.add(module)
            steps.extend(steps_for_spec(
                builder,
                module.fbcode_builder_spec(builder),
                processed_modules
            ))
    steps.extend(spec.get('steps', []))
    return steps


def build_fbcode_builder_config(config):
    return lambda builder: builder.build(
        steps_for_spec(builder, config['fbcode_builder_spec'](builder))
    )
