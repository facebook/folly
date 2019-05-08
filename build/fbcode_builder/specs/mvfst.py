#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import specs.folly as folly
import specs.fizz as fizz


def fbcode_builder_spec(builder):
    return {
        'depends_on': [folly, fizz],
        'steps': [
            builder.fb_github_cmake_install(
                'mvfst/build',
                github_org='facebookincubator',
            ),
        ],
    }
