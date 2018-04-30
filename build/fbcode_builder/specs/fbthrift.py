#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import specs.folly as folly
import specs.wangle as wangle
import specs.zstd as zstd

from shell_quoting import ShellQuoted


def fbcode_builder_spec(builder):
    # This API should change rarely, so build the latest tag instead of master.
    builder.add_option(
        'no1msd/mstch:git_hash',
        ShellQuoted('$(git describe --abbrev=0 --tags)')
    )
    builder.add_option(
        'rsocket/rsocket-cpp/yarpl/build:cmake_defines', {'BUILD_TESTS': 'OFF'}
    )
    return {
        'depends_on': [folly, wangle, zstd],
        'steps': [
            # This isn't a separete spec, since only fbthrift uses mstch.
            builder.github_project_workdir('no1msd/mstch', 'build'),
            builder.cmake_install('no1msd/mstch'),
            builder.github_project_workdir(
                'rsocket/rsocket-cpp', 'yarpl/build'
            ),
            builder.step('configuration for yarpl', [
                builder.cmake_configure('rsocket/rsocket-cpp/yarpl/build'),
            ]),
            builder.cmake_install('rsocket/rsocket-cpp/yarpl'),
            builder.fb_github_cmake_install('fbthrift/thrift'),
        ],
    }
