# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

# @lint-ignore-every FBCODEBZLADDLOADS

prelude = native

_SELECT_TYPE = type(select({"DEFAULT": []}))

def is_select(thing):
    return type(thing) == _SELECT_TYPE

def cpp_library(
        deps = [],
        external_deps = [],
        undefined_symbols = None,
        visibility = ["PUBLIC"],
        **kwargs):
    _unused = undefined_symbols  # @unused

    prelude.cxx_library(
        deps = _maybe_select_map(deps + external_deps_to_targets(external_deps), _fix_deps),
        visibility = visibility,
        preferred_linkage = "static",
        **kwargs
    )

def rust_library(
        rustc_flags = [],
        deps = [],
        named_deps = None,
        os_deps = None,
        test_deps = None,
        test_env = None,
        test_os_deps = None,
        autocargo = None,
        unittests = None,
        mapped_srcs = {},
        visibility = ["PUBLIC"],
        **kwargs):
    _unused = (test_deps, test_env, test_os_deps, named_deps, autocargo, unittests, visibility)  # @unused
    deps = _maybe_select_map(deps, _fix_deps)
    mapped_srcs = _maybe_select_map(mapped_srcs, _fix_mapped_srcs)
    if os_deps:
        deps += _select_os_deps(_fix_dict_deps(os_deps))

    # Reset visibility because internal and external paths are different.
    visibility = ["PUBLIC"]

    prelude.rust_library(
        rustc_flags = rustc_flags + [_CFG_BUCK_BUILD],
        deps = deps,
        visibility = visibility,
        mapped_srcs = mapped_srcs,
        **kwargs
    )

def rust_binary(
        rustc_flags = [],
        deps = [],
        autocargo = None,
        unittests = None,
        allocator = None,
        default_strip_mode = None,
        visibility = ["PUBLIC"],
        **kwargs):
    _unused = (unittests, allocator, default_strip_mode, autocargo)  # @unused
    deps = _maybe_select_map(deps, _fix_deps)

    # @lint-ignore BUCKLINT: avoid "Direct usage of native rules is not allowed."
    prelude.rust_binary(
        rustc_flags = rustc_flags + [_CFG_BUCK_BUILD],
        deps = deps,
        visibility = visibility,
        **kwargs
    )

def rust_unittest(
        rustc_flags = [],
        deps = [],
        visibility = ["PUBLIC"],
        **kwargs):
    deps = _maybe_select_map(deps, _fix_deps)

    prelude.rust_test(
        rustc_flags = rustc_flags + [_CFG_BUCK_BUILD],
        deps = deps,
        visibility = visibility,
        **kwargs
    )

def rust_protobuf_library(
        name,
        srcs,
        build_script,
        protos,
        build_env = None,
        deps = [],
        test_deps = None,
        doctests = True):
    if build_env:
        build_env = {
            k: _fix_dep_in_string(v)
            for k, v in build_env.items()
        }

    build_name = name + "-build"
    proto_name = name + "-proto"

    rust_binary(
        name = build_name,
        srcs = [build_script],
        crate_root = build_script,
        deps = [
            "fbsource//third-party/rust:tonic-build",
            "//buck2/app/buck2_protoc_dev:buck2_protoc_dev",
        ],
    )

    build_env = build_env or {}
    build_env.update(
        {
            "PROTOC": "$(exe buck//third-party/proto:protoc)",
            "PROTOC_INCLUDE": "$(location buck//third-party/proto:google_protobuf)",
        },
    )

    prelude.genrule(
        name = proto_name,
        srcs = protos + [
            "buck//third-party/proto:google_protobuf",
        ],
        out = ".",
        cmd = "$(exe :" + build_name + ")",
        env = build_env,
    )

    rust_library(
        name = name,
        srcs = srcs,
        doctests = doctests,
        env = {
            # This is where prost looks for generated .rs files
            "OUT_DIR": "$(location :{})".format(proto_name),
        },
        test_deps = test_deps,
        deps = [
            "fbsource//third-party/rust:prost",
            "fbsource//third-party/rust:prost-types",
        ] + (deps or []),
    )

    # For python tests only
    for proto in protos:
        prelude.export_file(
            name = proto,
            visibility = ["PUBLIC"],
        )

def ocaml_binary(
        deps = [],
        visibility = ["PUBLIC"],
        **kwargs):
    deps = _maybe_select_map(deps, _fix_deps)

    prelude.ocaml_binary(
        deps = deps,
        visibility = visibility,
        **kwargs
    )

_CFG_BUCK_BUILD = "--cfg=buck_build"

def _maybe_select_map(v, mapper):
    if is_select(v):
        return select_map(v, mapper)
    return mapper(v)

def _select_os_deps(xss: list[(
    str,
    list[str],
)]) -> Select:
    d = {
        "prelude//os:" + os: xs
        for os, xs in xss
    }
    d["DEFAULT"] = []
    return select(d)

def _fix_dict_deps(xss: list[(
    str,
    list[str],
)]) -> list[(
    str,
    list[str],
)]:
    return [
        (k, _fix_deps(xs))
        for k, xs in xss
    ]

def _fix_mapped_srcs(xs: dict[str, str]):
    # For reasons, this is source -> file path, which is the opposite of what
    # it should be.
    return {_fix_dep(k): v for (k, v) in xs.items()}

def _fix_deps(xs: list[str]) -> list[str]:
    return filter(None, map(_fix_dep, xs))

def _fix_dep(x: str) -> [
    None,
    str,
]:
    if x == "//common/rust/shed/fbinit:fbinit":
        return "fbsource//third-party/rust:fbinit"
    elif x == "//common/rust/shed/sorted_vector_map:sorted_vector_map":
        return "fbsource//third-party/rust:sorted_vector_map"
    elif x == "//watchman/rust/watchman_client:watchman_client":
        return "fbsource//third-party/rust:watchman_client"
    elif x.startswith("fbsource//third-party/rust:") or x.startswith(":"):
        return x
    elif x.startswith("//buck2/facebook/"):
        return None
    elif x.startswith("//buck2/"):
        return "root//" + x.removeprefix("//buck2/")
    elif x.startswith("fbcode//common/ocaml/interop/"):
        return "root//" + x.removeprefix("fbcode//common/ocaml/interop/")
    elif x.startswith("fbcode//third-party-buck/platform010/build/supercaml"):
        return "shim//third-party/ocaml" + x.removeprefix("fbcode//third-party-buck/platform010/build/supercaml")
    else:
        fail("Dependency is unaccounted for `{}`.\n".format(x) +
             "Did you forget 'oss-disable'?")

def _fix_dep_in_string(x: str) -> str:
    """Replace internal labels in string values such as env-vars."""
    return (x
        .replace("//buck2/", "root//"))

# Do a nasty conversion of e.g. ("supercaml", None, "ocaml-dev") to
# 'fbcode//third-party-buck/platform010/build/supercaml:ocaml-dev'
# (which will then get mapped to `shim//third-party/ocaml:ocaml-dev`).
def external_dep_to_target(t):
    return "fbcode//third-party-buck/platform010/build/{}:{}".format(t[0], t[2])

def external_deps_to_targets(ts):
    return [external_dep_to_target(t) for t in ts]
