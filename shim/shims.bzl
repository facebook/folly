# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@bazel_skylib//lib:new_sets.bzl", "sets")
load("@bazel_skylib//lib:paths.bzl", "paths")
load("@prelude//utils:selects.bzl", "selects")
# @lint-ignore-every FBCODEBZLADDLOADS

load("@prelude//utils:type_defs.bzl", "is_list", "is_select", "is_tuple")
load("@shim//build_defs:auto_headers.bzl", "AutoHeaders", "get_auto_headers")

prelude = native

_C_SOURCE_EXTS = (
    ".c",
)

_CPP_SOURCE_EXTS = (
    ".cc",
    ".cpp",
)

_SOURCE_EXTS = _C_SOURCE_EXTS + _CPP_SOURCE_EXTS

# These header suffixes are used to logically group C/C++ source (e.g.
# `foo/Bar.cpp`) with headers with the following suffixes (e.g. `foo/Bar.h` and
# `foo/Bar-inl.tcc`), such that the source provides all implementation for
# methods/classes declared in the headers.
#
# This is important for a couple reasons:
# 1) Automatic dependencies: Tooling can use this property to automatically
#    manage TARGETS dependencies by extracting `#include` references in sources
#    and looking up the rules which "provide" them.
# 2) Modules: This logical group can be combined into a standalone C/C++ module
#    (when such support is available).
_HEADER_SUFFIXES = (
    ".h",
    ".hpp",
    ".tcc",
    "-inl.h",
    "-inl.hpp",
    "-inl.tcc",
    "-defs.h",
    "-defs.hpp",
    "-defs.tcc",
)

def _get_headers_from_sources(srcs):
    """
    Return the headers likely associated with the given sources

    Args:
        srcs: A list of strings representing files or build targets

    Returns:
        A list of header files corresponding to the list of sources. These files are
        validated to exist based on glob()
    """
    split_srcs = [
        paths.split_extension(src_filename)
        for src_filename in [_get_src_filename(src) for src in srcs]
        if "//" not in src_filename and not src_filename.startswith(":")
    ]

    # For e.g. foo.cpp grab a glob on foo.h, foo-inl.h, etc
    headers = [
        base + header_ext
        for base, ext in split_srcs
        if ext in _SOURCE_EXTS
        for header_ext in _HEADER_SUFFIXES
    ]

    # Avoid a warning for an empty glob pattern if there are no headers.
    return glob(headers) if headers else []

def _get_src_filename(src):
    """
    Return filename from a potentilly tuple value entry in srcs attribute
    """

    if is_tuple(src):
        s, _ = src
        return s
    return src

def _update_headers_with_src_headers(src_headers, out_headers):
    """
    Helper function to update raw headers with headers from srcs
    """
    src_headers = sets.to_list(sets.difference(src_headers, sets.make(out_headers)))

    # Looks simple, right? But if a header is explicitly added in, say, a
    # dictionary mapping, we want to make sure to keep the original mapping
    # and drop the F -> F mapping
    if is_list(out_headers):
        out_headers.extend(sorted(src_headers))
    else:
        # Let it throw AttributeError if update() can't be found neither
        out_headers.update({k: k for k in src_headers})
    return out_headers

def prebuilt_cpp_library(
        headers = None,
        linker_flags = None,
        private_linker_flags = None,
        **kwargs):
    prelude.prebuilt_cxx_library(
        exported_headers = headers,
        exported_linker_flags = linker_flags,
        linker_flags = private_linker_flags,
        **kwargs
    )

def cpp_library(
        name,
        deps = [],
        srcs = [],
        external_deps = [],
        exported_deps = [],
        exported_external_deps = [],
        undefined_symbols = None,
        visibility = ["PUBLIC"],
        auto_headers = None,
        arch_preprocessor_flags = None,
        modular_headers = None,
        os_deps = [],
        arch_compiler_flags = None,
        tags = None,
        linker_flags = None,
        private_linker_flags = None,
        exported_linker_flags = None,
        headers = None,
        private_headers = None,
        propagated_pp_flags = (),
        **kwargs):
    _unused = (undefined_symbols, arch_preprocessor_flags, modular_headers, arch_compiler_flags, tags)  # @unused
    if os_deps:
        deps += _select_os_deps(_fix_dict_deps(os_deps))
    if headers == None:
        headers = []
    if is_select(srcs) and auto_headers == AutoHeaders.SOURCES:
        # Validate `srcs` and `auto_headers` before the config check
        base_path = native.package_name()
        fail(
            "//{}:{}: `select` srcs cannot support AutoHeaders.SOURCES".format(base_path, name),
        )
    auto_headers = get_auto_headers(auto_headers)
    if auto_headers == AutoHeaders.SOURCES and not is_select(srcs):
        src_headers = sets.make(_get_headers_from_sources(srcs))
        if private_headers:
            src_headers = sets.difference(src_headers, sets.make(private_headers))

        headers = selects.apply(
            headers,
            partial(_update_headers_with_src_headers, src_headers),
        )
    if not is_select(linker_flags):
        linker_flags = linker_flags or []
        linker_flags = list(linker_flags)
        if exported_linker_flags != None:
            linker_flags += exported_linker_flags
    prelude.cxx_library(
        name = name,
        srcs = srcs,
        deps = _maybe_select_map(deps + external_deps_to_targets(external_deps), _fix_deps),
        exported_deps = _maybe_select_map(exported_deps + external_deps_to_targets(exported_external_deps), _fix_deps),
        visibility = visibility,
        preferred_linkage = "static",
        exported_headers = headers,
        headers = private_headers,
        exported_linker_flags = linker_flags,
        linker_flags = private_linker_flags,
        **kwargs
    )

def cpp_unittest(
        deps = [],
        external_deps = [],
        visibility = ["PUBLIC"],
        supports_static_listing = None,
        allocator = None,
        owner = None,
        tags = None,
        emails = None,
        extract_helper_lib = None,
        compiler_specific_flags = None,
        default_strip_mode = None,
        **kwargs):
    _unused = (supports_static_listing, allocator, owner, tags, emails, extract_helper_lib, compiler_specific_flags, default_strip_mode)  # @unused
    prelude.cxx_test(
        deps = _maybe_select_map(deps + external_deps_to_targets(external_deps), _fix_deps),
        visibility = visibility,
        **kwargs
    )

def cpp_binary(
        deps = [],
        external_deps = [],
        visibility = ["PUBLIC"],
        dlopen_enabled = None,
        compiler_specific_flags = None,
        os_linker_flags = None,
        allocator = None,
        modules = None,
        **kwargs):
    _unused = (dlopen_enabled, compiler_specific_flags, os_linker_flags, allocator, modules)  # @unused
    prelude.cxx_binary(
        deps = _maybe_select_map(deps + external_deps_to_targets(external_deps), _fix_deps),
        visibility = visibility,
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

def _select_os_deps(xss) -> Select:
    d = {
        "prelude//os:" + os: xs
        for os, xs in xss
    }
    d["DEFAULT"] = []
    return select(d)

def _fix_dict_deps(xss):
    return [
        (k, _fix_deps(xs))
        for k, xs in xss
    ]

def _fix_mapped_srcs(xs: dict[str, str]):
    # For reasons, this is source -> file path, which is the opposite of what
    # it should be.
    return {_fix_dep(k): v for (k, v) in xs.items()}

def _fix_deps(xs):
    if is_select(xs):
        return xs
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
    elif x.startswith("fbcode//third-party-buck/platform010/build"):
        return "shim//third-party" + x.removeprefix("fbcode//third-party-buck/platform010/build")
    elif x.startswith("fbsource//third-party"):
        return "shim//third-party" + x.removeprefix("fbsource//third-party")
    elif x.startswith("third-party//"):
        return "shim//third-party/" + x.removeprefix("third-party//")
    elif x.startswith("//folly"):
        oss_depends_on_folly = read_config("oss_depends_on", "folly", False)
        if oss_depends_on_folly:
            return "root//folly/" + x.removeprefix("//")
        return "root//" + x.removeprefix("//")
    elif x.startswith("root//folly"):
        return x
    elif x.startswith("//fizz"):
        return "root//" + x.removeprefix("//")
    elif x.startswith("shim//"):
        return x
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
    if type(t) == type(()):
        return "fbcode//third-party-buck/platform010/build/{}:{}".format(t[0], t[2])
    else:
        return "fbcode//third-party-buck/platform010/build/{}:{}".format(t, t)

def external_deps_to_targets(ts):
    return [external_dep_to_target(t) for t in ts]
