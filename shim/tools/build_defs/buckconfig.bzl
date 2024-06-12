# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@prelude//utils:buckconfig.bzl", _read = "read", _read_bool = "read_bool", _read_choice = "read_choice", _read_int = "read_int", _read_list = "read_list", _read_string = "read_string", _resolve_alias = "resolve_alias")

read = _read
read_string = _read_string
read_choice = _read_choice
read_bool = _read_bool
read_int = _read_int
read_list = _read_list
resolve_alias = _resolve_alias
