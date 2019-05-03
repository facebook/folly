# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals

import os


def resolve_manifest_path(build_opts, project_name):
    if "/" in project_name or "\\" in project_name:
        # Assume this is a path already
        return project_name

    # Otherwise, resolve it relative to the manifests dir
    return os.path.join(build_opts.fbcode_builder_dir, "manifests", project_name)
