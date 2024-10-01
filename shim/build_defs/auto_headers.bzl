# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@prelude//utils:buckconfig.bzl", "read_choice")

AutoHeaders = struct(
    NONE = "none",
    # Infer headers from sources of the rule.
    SOURCES = "sources",
)

_VALUES = [
    AutoHeaders.NONE,
    AutoHeaders.SOURCES,
]

def get_auto_headers(auto_headers):
    """
    Returns the level of auto-headers to apply to a rule.

    Args:
        auto_headers: One of the values in `AutoHeaders`

    Returns:
        The value passed in as auto_headers, or the value from configuration if
        `auto_headers` is None
    """
    if auto_headers != None:
        if auto_headers not in _VALUES:
            fail("unexpected `auto_headers` value: {!r}".format(auto_headers))
        return auto_headers
    return read_choice("cxx", "auto_headers", _VALUES, AutoHeaders.SOURCES)
