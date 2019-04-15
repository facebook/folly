#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals


def fbcode_builder_spec(builder):
    builder.add_option('fmtlib/fmt:git_hash', '5.3.0')
    return {
        'steps': [
            builder.github_project_workdir('fmtlib/fmt', 'build'),
            builder.cmake_install('fmtlib/fmt'),
        ],
    }
