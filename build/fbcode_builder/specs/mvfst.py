#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import specs.folly as folly
import specs.fizz as fizz


def fbcode_builder_spec(builder):
    # Projects that **depend** on mvfst should don't need to build tests.
    builder.add_option(
        'mvfst/build:cmake_defines',
        {
            # This is set to ON in the mvfst `fbcode_builder_config.py` 
            'BUILD_TESTS': 'OFF',  
        }
    )
    return {
        'depends_on': [folly, fizz],
        'steps': [
            builder.fb_github_cmake_install(
                'mvfst/build',
                github_org='facebookincubator',
            ),
        ],
    }
