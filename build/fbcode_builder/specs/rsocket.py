#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import specs.gmock as gmock
import specs.folly as folly


def fbcode_builder_spec(builder):
    return {
        'depends_on': [folly],
        'steps': [
            builder.fb_github_cmake_install(
                'rsocket-cpp/rsocket',
                github_org='rsocket'),
        ],
    }
