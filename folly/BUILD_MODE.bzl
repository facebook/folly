""" build mode definitions for folly """

load("@fbcode//:BUILD_MODE.bzl", get_parent_modes = "get_empty_modes")
load("@fbcode_macros//build_defs:create_build_mode.bzl", "extend_build_modes")

_extra_cflags = [
    "-Wsign-compare",
    "-Wunused-parameter",
]

_extra_cxxflags = [
]

_extra_clang_flags = [
    "-Wconditional-uninitialized",
    "-Wconstant-conversion",
    "-Wdeprecated-declarations",
    "-Wextra",
    "-Wextra-semi",
    "-Wexceptions",
    "-Wfloat-conversion",
    "-Wgnu-conditional-omitted-operand",
    "-Wheader-hygiene",
    "-Wimplicit-fallthrough",
    "-Wmismatched-tags",
    "-Wmissing-braces",
    "-Wmissing-noreturn",
    "-Wshadow",
    "-Wshift-sign-overflow",
    "-Wsometimes-uninitialized",
    "-Wuninitialized",
    "-Wuninitialized-const-reference",
    "-Wunused-const-variable",
    "-Wunused-exception-parameter",
    "-Wunused-function",
    "-Wunused-lambda-capture",
    "-Wunused-value",
    "-Wunused-variable",
    "-Wswitch-enum",
]

_extra_gcc_flags = [
    "-Wdeprecated-declarations",
    "-Wmaybe-uninitialized",
    "-Wmissing-braces",
    "-Wshadow",
    "-Wuninitialized",
    "-Wunused-but-set-variable",
]

_extra_asan_options = {
    "detect_leaks": "1",
    "detect_odr_violation": "2",
}

_tags = [
]

_modes = extend_build_modes(
    get_parent_modes(),
    asan_options = _extra_asan_options,
    c_flags = _extra_cflags,
    clang_flags = _extra_clang_flags,
    cxx_flags = _extra_cxxflags,
    cxx_modular_headers = True,
    gcc_flags = _extra_gcc_flags,
    tags = _tags,
)

def get_modes():
    """ Return modes for this file """
    return _modes
