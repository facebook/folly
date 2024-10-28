# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@prelude//cfg/modifier:cfg_constructor.bzl?v2_only", "cfg_constructor_post_constraint_analysis", "cfg_constructor_pre_constraint_analysis")
load("@prelude//cfg/modifier:common.bzl?v2_only", "MODIFIER_METADATA_KEY")

SHIM_ALIASES = {
    "fedora": "shim//os/linux/distro/constraints:fedora",
    "ubuntu": "shim//os/linux/distro/constraints:ubuntu",
}

def set_cfg_constructor(aliases = dict()):
    project_root_cell = read_root_config("cell_aliases", "root")
    current_root_cell = read_config("cell_aliases", "root")
    if project_root_cell == current_root_cell:
        native.set_cfg_constructor(
            stage0 = cfg_constructor_pre_constraint_analysis,
            stage1 = cfg_constructor_post_constraint_analysis,
            key = MODIFIER_METADATA_KEY,
            aliases = struct(**aliases),
            extra_data = struct(),
        )

def get_shim_modifiers():
    modifiers = []

    linux_distro = read_config("linux", "distro")

    if linux_distro:
        modifiers.append("shim//os/linux/distro/constraints:{}".format(linux_distro))

    known_broken = read_config("oss", "known_broken", "disable")
    modifiers.append("shim//opensource/macros/broken_in_oss/constraints:{}".format(known_broken))

    return modifiers
