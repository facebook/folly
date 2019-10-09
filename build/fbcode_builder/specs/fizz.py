#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import specs.folly as folly
import specs.sodium as sodium


def fbcode_builder_spec(builder):
    # Projects that **depend** on fizz should don't need to build tests.
    builder.add_option(
        'fizz/fizz/build:cmake_defines',
        {
            # This is set to ON in the fizz `fbcode_builder_config.py` 
            'BUILD_TESTS': 'OFF',
        }
    )
    return {
        'depends_on': [folly, sodium],
        'steps': [
            builder.fb_github_cmake_install(
                'fizz/fizz/build',
                github_org='facebookincubator',
            ),
        ],
    }
