load("@fbcode_macros//build_defs:config.bzl", "config")
load("@fbcode_macros//build_defs:cpp_library.bzl", "cpp_library")
load("@fbcode_macros//build_defs:cpp_unittest.bzl", "cpp_unittest")
load("@fbcode_macros//build_defs:custom_unittest.bzl", "custom_unittest")
load("@fbsource//tools/build_defs/buck2:is_buck2.bzl", "is_buck2")

SPLIT_DWARF_FLAGS = {
    "none": ["-gno-split-dwarf"],
    "single_inlining": ["-gsplit-dwarf=single", "-fsplit-dwarf-inlining"],
    "single_no_inlining": ["-gsplit-dwarf=single", "-fno-split-dwarf-inlining"],
    "split_no_inlining": ["-gsplit-dwarf=split", "-fno-split-dwarf-inlining"],
}

def _dwarf_size_flag(size):
    if size == 32:
        return []
    else:
        return ["-gdwarf{}".format(size)]

def customized_unittest(
        available_dwarf_versions = [],
        avilable_split_dwarf_keys = [],
        available_dwarf_sizes = [],
        extra_compiler_flags = [],
        custom_suffix = ""):
    # Testing different combinations of the following options:
    # 1. Dwarf4 or Dwarf5.
    # 2. Dwarf32 or Dwarf 64.
    # 3. Split dwarf options.
    # 4. Use aaranges or not.
    for dwarf_version in available_dwarf_versions:
        for dwarf_size in available_dwarf_sizes:
            for split_dwarf_option in avilable_split_dwarf_keys:
                # buck2 doesn't support split version yet.
                if split_dwarf_option == "none" or split_dwarf_option == "single_inlining" or not is_buck2():
                    for use_aaranges in [False, True]:
                        cpp_library(
                            name = "symbolizer_test_utils_" + dwarf_version +
                                   ("_dwarf{}".format(dwarf_size)) +
                                   ("" if split_dwarf_option == "none" else "_" + split_dwarf_option) +
                                   ("_aaranges" if use_aaranges else "_noaaranges") + custom_suffix,
                            srcs = ["SymbolizerTestUtils.cpp"],
                            headers = [
                                "SymbolizerTestUtils.h",
                                "SymbolizerTestUtils-inl.h",
                            ],
                            # Tests rely on this library having full debug info, so use `-g` to override
                            # the platform default, and use `--emit-relocs` to prevent `--strip-debug-*`
                            # flags from dropping debug info.
                            compiler_flags = ["-g"] +
                                             (["-gdwarf-5"] if dwarf_version == "dwarf5" else ["-gdwarf-4"]) +
                                             (_dwarf_size_flag(dwarf_size)) +
                                             SPLIT_DWARF_FLAGS[split_dwarf_option] +
                                             (["-gdwarf-aranges"] if use_aaranges else []) +
                                             extra_compiler_flags,
                            modular_headers = False,
                            private_linker_flags = [
                                "--emit-relocs",  # makes linker ignore `--strip-debug-*` flags
                            ],
                        )
                        cpp_unittest(
                            name = "symbolizer_test_" + dwarf_version +
                                   ("_dwarf{}".format(dwarf_size)) +
                                   ("" if split_dwarf_option == "none" else "_" + split_dwarf_option) +
                                   ("_aaranges" if use_aaranges else "_noaaranges") + custom_suffix,
                            srcs = ["SymbolizerTest.cpp"],
                            supports_static_listing = True,
                            tags = ["dwp"] if split_dwarf_option == "single_inlining" and use_aaranges else [],
                            # This tests requires full debug info, so use `-g` to override the platform
                            # default, and use `--emit-relocs` to prevent `--strip-debug-*` flags from
                            # dropping debug info.
                            compiler_flags = ["-g"] +
                                             (["-gdwarf-5"] if dwarf_version == "dwarf5" else ["-gdwarf-4"]) +
                                             (_dwarf_size_flag(dwarf_size)) +
                                             SPLIT_DWARF_FLAGS[split_dwarf_option] +
                                             (["-gdwarf-aranges"] if use_aaranges else []) + extra_compiler_flags,
                            linker_flags = [
                                "--emit-relocs",  # makes linker ignore `--strip-debug-*` flags
                            ],
                            deps = [
                                ":symbolizer_test_utils_" + dwarf_version +
                                ("_dwarf{}".format(dwarf_size)) +
                                ("" if split_dwarf_option == "none" else "_" + split_dwarf_option) +
                                ("_aaranges" if use_aaranges else "_noaaranges"),  # @manual
                                "//folly:demangle",
                                "//folly:range",
                                "//folly:scope_guard",
                                "//folly:string",
                                "//folly/experimental/symbolizer:elf_cache",
                                "//folly/experimental/symbolizer:symbolized_frame",
                                "//folly/experimental/symbolizer:symbolizer",
                                "//folly/experimental/symbolizer/detail:debug",
                                "//folly/portability:filesystem",
                                "//folly/portability:gtest",
                                "//folly/portability:unistd",
                                "//folly/synchronization:baton",
                                "//folly/test:test_utils",
                            ],
                            external_deps = [
                                "glog",
                            ],
                        )

def validate_folly_symbolizer(name, binary):
    custom_unittest(
        name = name,
        command = [
            "$(exe //folly/experimental/symbolizer/test:compare-addr2line.sh)",
            "$(location //folly/experimental/symbolizer/tool:folly-addr2line)",
            "$(location //third-party-buck/platform010/build/llvm-fb/15:bin/llvm-addr2line)",
            "$(location {})".format(binary),
        ],
        type = "simple",
    )

def validate_symbolizer_dwp(name, binary):
    # Only test in opt mode.
    # In dev mode, the test still depends on the shared libraries except
    # binary + dwp file.
    if config.get_build_mode().startswith("opt"):
        custom_unittest(
            name = name,
            command = [
                "$(exe //folly/experimental/symbolizer/test:symbolizer_dwp_compability.sh)",
                "$(location {})".format(binary),
                "$(location {}[dwp])".format(binary),
                config.get_build_mode(),
            ],
            type = "simple",
        )
