"""Provides helper functions for the folly library

[folly]
    have_libgflags_override = {True|[False]}
"""

load("@fbsource//tools/build_defs:buckconfig.bzl", "read_bool")
load(
    "@fbsource//tools/build_defs:default_platform_defs.bzl",
    "ANDROID",
    "APPLE",
    "CXX",
    "FBCODE",
    "IOS",
    "MACOSX",
    "WINDOWS",
)
load("@fbsource//tools/build_defs:fb_xplat_cxx_binary.bzl", "fb_xplat_cxx_binary")
load("@fbsource//tools/build_defs:fb_xplat_cxx_library.bzl", "fb_xplat_cxx_library")
load("@fbsource//tools/build_defs:fb_xplat_cxx_test.bzl", "fb_xplat_cxx_test")

def should_enable_gflags():
    return read_bool("folly", "have_libgflags_override", False)

def is_folly_mobile_flag():
    return native.read_config("cpp_flags", "preprocessing", "") == "DFOLLY_MOBILE"

def cpp_flags():
    flags = [
        "-DFOLLY_HAVE_LIBJEMALLOC=0",
        "-DFOLLY_HAVE_PREADV=0",
        "-DFOLLY_HAVE_PWRITEV=0",
        "-DFOLLY_HAVE_TFO=0",
    ]

    if is_folly_mobile_flag():
        flags += select({
            "DEFAULT": [],
            "ovr_config//os:linux": ["-DFOLLY_MOBILE=1"],
        })

    else:
        flags += select({
            "DEFAULT": ["-DFOLLY_MOBILE=1"],
            "ovr_config//build_mode:arvr_mode": select({
                "DEFAULT": ["-DFOLLY_MOBILE=1"],
                "ovr_config//os:linux": [],
                "ovr_config//os:macos": [],
            }),
        })

    return flags

FBANDROID_CPPFLAGS = [
    "-DFOLLY_HAVE_IFUNC=0",
    "-DFOLLY_HAVE_INT128_T=0",
    "-DSSLCONTEXT_NO_REFCOUNT",
    "-D__STDC_FORMAT_MACROS",
    "-D__STDC_LIMIT_MACROS",
    "-D__STDC_CONSTANT_MACROS",
]

CLANG_CXX_FLAGS = [
    "-frtti",
    "-fexceptions",
    "-Wall",
    "-Werror",
    "-Wno-unused-local-typedefs",
    "-Wno-unused-variable",
    "-Wno-sign-compare",
    "-Wno-comment",
    "-Wno-return-type",
    "-Wno-global-constructors",
    "-Wno-missing-prototypes",
    "-Wno-nullability-completeness",
    "-Wno-c++17-extensions",
    "-Wno-undef",
    "-Wno-unreachable-code",
    "-Wno-deprecated-declarations",
]
CXXFLAGS = select({
    "DEFAULT": [],
    "ovr_config//compiler:clang": CLANG_CXX_FLAGS,
})

FBOBJC_CXXFLAGS = ["-Os"]

FBANDROID_CXXFLAGS = [
    "-ffunction-sections",
    "-Wno-uninitialized",
]

WINDOWS_MSVC_CXXFLAGS = [
    "/EHs",
    "/D_ENABLE_EXTENDED_ALIGNED_STORAGE",
]

WINDOWS_CLANG_CXX_FLAGS = [
    "-Wno-deprecated-declarations",
    "-Wno-microsoft-cast",
    "-Wno-missing-braces",
    "-Wno-unused-function",
    "-Wno-undef",
    "-DBOOST_HAS_THREADS",
    "-D_ENABLE_EXTENDED_ALIGNED_STORAGE",
]

DEFAULT_APPLE_SDKS = (IOS, MACOSX)
DEFAULT_PLATFORMS = (CXX, ANDROID, APPLE, FBCODE, WINDOWS)

def _compute_include_directories():
    base_path = native.package_name()
    if base_path == "xplat/folly":
        return [".."]
    folly_path = base_path[6:]
    return ["/".join(len(folly_path.split("/")) * [".."])]

def folly_xplat_library(
        name,
        srcs = (),
        header_namespace = "",
        exported_headers = (),
        raw_headers = (),
        deps = (),
        exported_deps = (),
        force_static = True,
        apple_sdks = None,
        platforms = None,
        enable_static_variant = True,
        labels = (),
        **kwargs):
    """Translate a simpler declartion into the more complete library target"""

    # Set default platform settings. `()` means empty, whereas None
    # means default
    if apple_sdks == None:
        apple_sdks = DEFAULT_APPLE_SDKS
    if platforms == None:
        platforms = DEFAULT_PLATFORMS

    # We use gflags on fbcode platforms, which don't mix well when mixing static
    # and dynamic linking.
    force_static = select({
        "DEFAULT": select({
            "DEFAULT": force_static,
            "ovr_config//runtime:fbcode": False,
        }),
        "ovr_config//build_mode:arvr_mode": force_static,
    })

    fb_xplat_cxx_library(
        name = name,
        srcs = srcs,
        header_namespace = header_namespace,
        exported_headers = exported_headers,
        raw_headers = raw_headers,
        public_include_directories = _compute_include_directories(),
        deps = deps,
        exported_deps = exported_deps,
        force_static = force_static,
        apple_sdks = apple_sdks,
        platforms = platforms,
        enable_static_variant = enable_static_variant,
        labels = list(labels),
        compiler_flags = CXXFLAGS + kwargs.pop("compiler_flags", []) + select({
            "DEFAULT": [],
            "ovr_config//os:android": FBANDROID_CXXFLAGS,
            "ovr_config//os:iphoneos": CLANG_CXX_FLAGS,
            # TODO: Why iphoneos and macos are not marked as clang compilers?
            "ovr_config//os:macos": CLANG_CXX_FLAGS + ["-fvisibility=default"],
        }) + select({
            "DEFAULT": [],
            "ovr_config//os:windows-cl": WINDOWS_MSVC_CXXFLAGS,
            "ovr_config//os:windows-gcc-or-clang": WINDOWS_CLANG_CXX_FLAGS,
        }) + [
            "-fexceptions",
            "-frtti",
        ],
        fbobjc_compiler_flags = kwargs.pop("fbobjc_compiler_flags", []) +
                                FBOBJC_CXXFLAGS,
        fbcode_compiler_flags_override = kwargs.pop("fbcode_compiler_flags", []),
        windows_preferred_linkage = "static",
        visibility = kwargs.pop("visibility", ["PUBLIC"]),
        **kwargs
    )

def folly_xplat_cxx_library(name, **kwargs):
    folly_xplat_library(
        name = name,
        **kwargs
    )

def folly_xplat_cxx_test(
        name,
        srcs,
        raw_headers = [],
        deps = [],
        contacts = [],
        **kwargs):
    # resources is cherry picked because some of the other kwargs
    # have issues that need to be investigated.
    # e.g., Some args are duplicated. Some args cause TSAN errors.
    # TODO(T188948036): Fix xplat/folly:folly-futures-test and folly_xplat_cxx_test
    resources = kwargs.get("resources", [])

    fb_xplat_cxx_test(
        name = name,
        srcs = srcs,
        raw_headers = raw_headers,
        resources = resources,
        include_directories = _compute_include_directories(),
        deps = deps + [
            "//xplat/folly/test/common:test_main",
        ],
        contacts = contacts,
        platforms = (CXX,),
    )

def folly_xplat_cxx_binary(
        name,
        srcs,
        raw_headers = [],
        deps = [],
        contacts = [],
        **kwargs):
    fb_xplat_cxx_binary(
        name = name,
        srcs = srcs,
        raw_headers = raw_headers,
        include_directories = _compute_include_directories(),
        deps = deps,
        contacts = contacts,
        platforms = (CXX,),
    )
