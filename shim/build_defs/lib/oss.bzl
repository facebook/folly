# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

def _filter_empty_strings(string_list):
    return filter(lambda d: d != "", string_list)

def _parse_prefix_mappings(raw_rules):
    rules = []
    for raw_rule in raw_rules:
        (match, replace) = raw_rule.split("->", 1)

        (cell, root_dir) = match.split("//")
        match = struct(cell = cell, root_dir = root_dir)

        (cell, root_dir) = replace.split("//")
        replace = struct(cell = cell, root_dir = root_dir)

        rules.append(struct(match = match, replace = replace))

    return rules

def _strip_third_party_rust_version(target: str) -> str:
    # When upgrading libraries we either suffix them as `-old` or with a version, e.g. `-1-08`
    # Strip those so we grab the right one in open source.
    if target.endswith(":md-5"):  # md-5 is the one exception
        return target
    xs = target.split("-")
    for i in reversed(range(len(xs))):
        s = xs[i]
        if s == "old" or s.isdigit():
            xs.pop(i)
        else:
            break
    return "-".join(xs)

# Cell the BUCK file being processed belongs to
ACTIVE_CELL = native.get_cell_name()

# The root cell of this project, generally "root" and does not need to be set.
# Targets that explicitly reference this cell will not be rewritten, and
# targets that do not end up referencing a cell will be replaced with targets
# that reference this cell
ROOT_CELL = read_config("oss", "root_cell", "root")

# The cell this file and the rest of the shim directory belong to, generally
# "shim" and does not need to be set.
SHIM_CELL = read_config("oss", "shim_cell", "shim")

# The internal cell this project originally belonged to.
#
# When applying rewrites, the cell of the target is often considered. Targets
# that do not explicitly specify a cell (eg: "//foo:bar") will be considered
# to belong to INTERNAL_CELL.
INTERNAL_CELL = read_config("oss", "internal_cell", "fbcode")

# There can be situations where a target specifies a cell explicitly and the
# path is part of the local checkout, rather than potentially needing to be
# shimmed. In this case, we want to rewrite the target to use the root cell.
#
# If a target's cell is unspecified or matches the internal cell, and the path
# starts with an entry in this list, The cell replaced with the ROOT_CELL.
#
# Entries are separated by spaces, and evaluated in order. Once a match is
# found, the rewrite is complete and the following entries will not be
# evaluated.
#
# Examples:
#     internal_cell//oss_project/foo:bar -> root//oss_project/foo:bar
PROJECT_DIRS = _filter_empty_strings(read_config("oss", "project_dirs", "").split(" "))

# There are some situations where prefix of the internal directory structure is
# removed from the public filepaths, such as rewriting "internal/foo/bar/baz"
# to "oss/baz". When this happens, the BUCK files are not converted to reflect
# the public directory structure, and targets need to be rewritten to account
# for the discrepancy.
#
# Entries behave similarly to PROJECT_DIRS, except that the root directory will
# also be removed from the path in the rewritten target. This setting is
# applied after PROJECT_DIRS.
#
# Entries are separated by spaces and evaluated in order. Once a match is
# found, the rewrite is complete and the following entries will not be
# evaluated.
#
# Examples:
#     //oss_project/foo:bar -> root//foo:bar
#     internal_cell//oss_project/foo:bar -> root//foo:bar
STRIPPED_ROOT_DIRS = _filter_empty_strings(read_config("oss", "stripped_root_dirs", "").split(" "))

# Internally, most code shares the same cell in a monorepo, but public projects
# only contain a subset, importing dependencies via git submodules or other
# mechanisms. When this happens, the dependency may end up in a different
# filepath, or may have it's own buck2 configuration and should be treated as
# an on disk external cell.
#
# If the target's cell is a match (or if unspecified, INTERNAL_CELL is a
# match),unspecified) matches, and the target's path is within the root
# directory, both the cell and root directory prefix are replaced with the new
# values.
#
# Entries are in the form of "MATCH->REPLACEMENT". Both MATCH and replacement
# shall be in the format of "CELL//DIR_PREFIX".
#
# Entries are separated by spaces and evaluated in order. Once a match is
# found, the rewrite is complete and the following entries will not be
# evaluated.
#
# Examples:
#     internal//foo->foo//foo; internal//foo/bar:baz -> foo//foo/bar:baz
PREFIX_MAPPINGS = _parse_prefix_mappings(
    _filter_empty_strings(read_config("oss", "prefix_mappings", "").split(" ")),
)

# Hardcoded rewrite rules that apply to many projects and only produce targets
# within the shim cell. They are applied after the rules from .buckconfig, and
# will not be applied if any other rules match.
IMPLICIT_REWRITE_RULES = {
    "fbcode": struct(
        exact = {
            "common/rust/shed/fbinit:fbinit": "third-party/rust:fbinit",
            "common/rust/shed/sorted_vector_map:sorted_vector_map": "third-party/rust:sorted_vector_map",
            "watchman/rust/watchman_client:watchman_client": "third-party/rust:watchman_client",
        },
        dirs = [
            ("third-party-buck/platform010/build/supercaml", "third-party/ocaml"),
            ("third-party-buck/platform010/build", "third-party"),
        ],
    ),
    "fbsource": struct(
        dirs = [
            ("third-party", "third-party"),
        ],
        dynamic = [
            ("third-party/rust", _strip_third_party_rust_version),
        ],
    ),
    "third-party": struct(
        dirs = [
            ("", "third-party"),
        ],
        dynamic = [
            ("rust", lambda path: "third-party/" + _strip_third_party_rust_version(path)),
        ],
    ),
}

DEFAULT_REWRITE_CTX = struct(
    cells = struct(
        active = ACTIVE_CELL,
        root = ROOT_CELL,
        shim = SHIM_CELL,
        internal = INTERNAL_CELL,
    ),
    project_dirs = PROJECT_DIRS,
    stripped_root_dirs = STRIPPED_ROOT_DIRS,
    prefix_mappings = PREFIX_MAPPINGS,
    implicit_rewrite_rules = IMPLICIT_REWRITE_RULES,
)

"""
Rewrite an internal target string to one that is compatible with this OSS
project.

Some example use cases for this:
- Map dependency targets to shim targets in this dir
- Handle mismatching buck roots between internal and oss
  (eg: internal/oss-project/... is exposed externally as oss-project/...)
- Handle submodules that result in filepaths that do not match internal
  (eg: internal/my_library/... and oss-project/my_library/my_library/...)
"""

def translate_target(
        target: str,
        ctx = DEFAULT_REWRITE_CTX) -> str:
    if "//" not in target:
        # This is a local target, aka ":foo". Don't touch
        return target

    (cell, path) = target.split("//", 1)

    if cell == ctx.cells.root:
        # This cell is explicitly root. Don't touch
        return target

    resolved_cell = ctx.cells.active if cell == "" else cell
    internal_cell = ctx.cells.internal if resolved_cell == ctx.cells.root else resolved_cell

    if internal_cell == ctx.cells.internal:
        for d in ctx.project_dirs:
            if _path_rooted_in_dir(path, d):
                return ctx.cells.root + "//" + path

        for d in ctx.stripped_root_dirs:
            if _path_rooted_in_dir(path, d):
                return ctx.cells.root + "//" + _strip_root_dir_from_path(path, d)

    for rule in ctx.prefix_mappings:
        if internal_cell == rule.match.cell and _path_rooted_in_dir(path, rule.match.root_dir):
            return rule.replace.cell + "//" + _swap_root_dir_for_path(path, rule.match.root_dir, rule.replace.root_dir)

    rules = ctx.implicit_rewrite_rules.get(internal_cell)

    if rules == None:
        # No implicit rewrite rules
        return target

    exact = getattr(rules, "exact", {}).get(path)
    if exact != None:
        return ctx.cells.shim + "//" + exact

    for (match_root_dir, replace_root_dir) in getattr(rules, "dirs", []):
        if _path_rooted_in_dir(path, match_root_dir):
            return ctx.cells.shim + "//" + _swap_root_dir_for_path(path, match_root_dir, replace_root_dir)

    for (match_root_dir, fn) in getattr(rules, "dynamic", []):
        if _path_rooted_in_dir(path, match_root_dir):
            return ctx.cells.shim + "//" + fn(path)

    return target

def _path_rooted_in_dir(path: str, d: str) -> bool:
    return d == "" or path == d or path.startswith(d + "/") or path.startswith(d + ":")

def _strip_root_dir_from_path(path: str, d: str) -> str:
    return path.removeprefix(d).removeprefix("/")

def _swap_root_dir_for_path(path: str, root_dir: str, new_root_dir) -> str:
    suffix = _strip_root_dir_from_path(path, root_dir)
    if not suffix.startswith(":"):
        suffix = "/" + suffix
    replace_path = new_root_dir.removesuffix("/") + suffix
    return replace_path.removeprefix("/")
