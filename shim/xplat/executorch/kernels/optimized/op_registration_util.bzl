# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@fbsource//xplat/executorch/build:runtime_wrapper.bzl", "runtime")
load("@fbsource//xplat/executorch/build:selects.bzl", "selects")
load(
    "@fbsource//xplat/executorch/kernels/optimized:lib_defs.bzl",
    "get_vec_android_preprocessor_flags",
)

def op_target(name, deps = []):
    """Registers an optimized implementation for an operator overload group.

    An operator overload group is a set of operator overloads with a common
    operator name. That common operator name should be the base name of this
    target.

    E.g., the "add" operator overload group, named "op_add" in this target,
    might implement:
    - add.Tensor
    - add_.Tensor
    - add.out
    - add.Scalar

    If an op target would like to share a header/sources with a different op
    target (e.g., helpers/utilities), it should declare a separate cxx_library
    and add it as a dep.

    Args:
        name: The name of the operator overload group; e.g.,
            "op_add". This directory must contain a source file named
            "<name>.cpp"; e.g., "op_add.cpp".
        deps: Optional extra deps to add to the cxx_library(). Note:
            - op targets may not depend on other op targets, to keep the
              dependencies manageable. If two op targets would like to share
              code, define a separate runtime.cxx_library that they both depend
              on.
    """

    # Note that this doesn't actually define the target, but helps register
    # it in a table that's used to define the target.
    return {
        "deps": deps,
        "name": name,
    }

def _enforce_deps(deps, name):
    """Fails if any of the deps are not allowed.

    Args:
        deps: A list of build target strings.
        name: The name of the target; e.g., "op_add"
    """
    for dep in deps:
        if dep.startswith(":op_"):
            # op targets may not depend on other op targets, to keep the
            # dependencies manageable. If two op targets would like to share
            # code, define a separate runtime.cxx_library that they both depend
            # on.
            fail("op_target {} may not depend on other op_target {}".format(
                name,
                dep,
            ))

def define_op_library(name, deps):
    """Defines a cxx_library target for the named operator overload group.

    Args:
        name: The name of the target; e.g., "op_add"
        deps: List of deps for the target.
    """
    selects.apply(obj = deps, function = native.partial(_enforce_deps, name = name))

    augmented_deps = deps + [
        "//executorch/kernels/optimized:libvec",
        "//executorch/kernels/optimized:libutils",
    ]

    runtime.cxx_library(
        name = "{}".format(name),
        srcs = [
            "{}.cpp".format(name),
        ],
        visibility = [
            "//executorch/kernels/portable/test/...",
            "//executorch/kernels/quantized/test/...",
            "//executorch/kernels/optimized/test/...",
            "//executorch/kernels/test/...",
            "@EXECUTORCH_CLIENTS",
        ],
        # kernels often have helpers with no prototypes just disabling the warning here as the headers
        # are codegend and linked in later
        compiler_flags = ["-Wno-missing-prototypes"],
        deps = [
            "//executorch/runtime/kernel:kernel_includes",
        ] + augmented_deps,
        fbandroid_platform_preprocessor_flags = get_vec_android_preprocessor_flags(),
        # sleef needs to be added as a direct dependency of the operator target when building for Android,
        # or a linker error may occur. Not sure why this happens; it seems that fbandroid_platform_deps of
        # dependencies are not transitive
        fbandroid_platform_deps = [
            (
                "^android-arm64.*$",
                [
                    "fbsource//third-party/sleef:sleef_arm",
                ],
            ),
        ],
        # link_whole is necessary because the operators register themselves
        # via static initializers that run at program startup.
        # @lint-ignore BUCKLINT link_whole
        link_whole = True,
    )

def define_op_target(name, deps):
    """Possibly defines cxx_library targets for the named operator group.

    Args:
        name: The base name of the target; e.g., "op_add"
        deps: List of deps for the targets.
    """

    # When building in ATen mode, ATen-compatible (non-custom) operators will
    # use the implementations provided by ATen, so we should not build the
    # versions defined here.
    define_op_library(
        name = name,
        deps = deps,
    )

def is_op_disabled(name):
    # TODO (gjcomer) Enable ops with sleef dependency in OSS
    disabled_ops = ["op_gelu", "op_log_softmax"]
    return name in disabled_ops
