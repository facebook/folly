#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import specs.fmt as fmt
import specs.folly as folly
import specs.fizz as fizz
import specs.sodium as sodium


def fbcode_builder_spec(builder):
    # Projects that **depend** on wangle need not spend time on tests.
    builder.add_option(
        'wangle/wangle/build:cmake_defines',
        {
            # This is set to ON in the wangle `fbcode_builder_config.py` 
            'BUILD_TESTS': 'OFF',
        }
    )
    return {
        'depends_on': [fmt, folly, fizz, sodium],
        'steps': [
            builder.fb_github_cmake_install('wangle/wangle/build'),
        ],
    }
