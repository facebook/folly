#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import specs.fizz as fizz
import specs.fmt as fmt
import specs.folly as folly
import specs.sodium as sodium
import specs.wangle as wangle
import specs.zstd as zstd


def fbcode_builder_spec(builder):
    return {
        'depends_on': [fmt, folly, fizz, sodium, wangle, zstd],
        'steps': [
            builder.fb_github_cmake_install('fbthrift/thrift'),
        ],
    }
