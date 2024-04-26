# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

def _get_cxx_platform_for_base_path(_base_path):
    return struct(target_platform = None)

platform_utils = struct(get_cxx_platform_for_base_path = _get_cxx_platform_for_base_path)
