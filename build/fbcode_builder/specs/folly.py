#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals


def fbcode_builder_spec(builder):
    return {
        'steps': [
            # on macOS the filesystem is typically case insensitive.
            # We need to ensure that the CWD is not the folly source
            # dir when we build, otherwise the system will decide
            # that `folly/String.h` is the file it wants when including
            # `string.h` and the build will fail.
            builder.github_project_workdir('facebook/folly', '_build'),
            builder.cmake_install('facebook/folly'),
        ],
    }
