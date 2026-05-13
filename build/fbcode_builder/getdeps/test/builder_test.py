# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# pyre-unsafe

import os
import unittest
from unittest.mock import MagicMock, patch

from .. import builder as builder_module
from ..builder import CMakeBuilder
from ..envfuncs import Env
from ..manifest import ManifestContext, ManifestParser


MINIMAL_MANIFEST = """
[manifest]
name = test

[build]
builder = cmake
"""


def make_cmake_builder() -> CMakeBuilder:
    manifest = ManifestParser("test", MINIMAL_MANIFEST)
    loader = MagicMock()
    loader.get_project_install_dir.return_value = "/tmp/install"
    build_opts = MagicMock()
    build_opts.is_windows.return_value = False
    build_opts.is_darwin.return_value = False
    build_opts.shared_lib = False
    build_opts.build_type = "RelWithDebInfo"
    return CMakeBuilder(
        loader=loader,
        dep_manifests=[],
        build_opts=build_opts,
        ctx=ManifestContext(
            {
                "os": None,
                "distro": None,
                "distro_vers": None,
                "fb": "off",
                "fbsource": "off",
                "test": "off",
            }
        ),
        manifest=manifest,
        src_dir="/tmp/src",
        build_dir="/tmp/build",
        inst_dir="/tmp/inst",
        defines=None,
    )


class CMakeBuilderCompilerCacheTest(unittest.TestCase):
    def setUp(self) -> None:
        # Ensure SANDCASTLE is not set so compiler cache detection runs
        self._orig_sandcastle = os.environ.pop("SANDCASTLE", None)

    def tearDown(self) -> None:
        if self._orig_sandcastle is not None:
            os.environ["SANDCASTLE"] = self._orig_sandcastle
        else:
            os.environ.pop("SANDCASTLE", None)

    def _launcher_args(self, define_args: list) -> list:
        return [a for a in define_args if "CMAKE_CXX_COMPILER_LAUNCHER" in a]

    def test_sccache_preferred_over_ccache_when_both_available(self) -> None:
        builder = make_cmake_builder()
        env = Env()

        def fake_path_search(env, name):
            if name == "sccache":
                return "/usr/bin/sccache"
            if name == "ccache":
                return "/usr/bin/ccache"
            return None

        with patch.object(builder_module, "path_search", side_effect=fake_path_search):
            define_args = builder._compute_cmake_define_args(env)

        launcher_args = self._launcher_args(define_args)
        self.assertEqual(len(launcher_args), 1)
        self.assertIn("/usr/bin/sccache", launcher_args[0])

    def test_ccache_used_when_sccache_unavailable(self) -> None:
        builder = make_cmake_builder()
        env = Env()

        def fake_path_search(env, name):
            if name == "sccache":
                return None
            if name == "ccache":
                return "/usr/bin/ccache"
            return None

        with patch.object(builder_module, "path_search", side_effect=fake_path_search):
            define_args = builder._compute_cmake_define_args(env)

        launcher_args = self._launcher_args(define_args)
        self.assertEqual(len(launcher_args), 1)
        self.assertIn("/usr/bin/ccache", launcher_args[0])

    def test_no_compiler_cache_when_neither_available(self) -> None:
        builder = make_cmake_builder()
        env = Env()

        with patch.object(builder_module, "path_search", return_value=None):
            define_args = builder._compute_cmake_define_args(env)

        launcher_args = self._launcher_args(define_args)
        self.assertEqual(len(launcher_args), 0)
