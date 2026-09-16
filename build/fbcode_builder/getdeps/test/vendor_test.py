# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


import argparse
import contextlib
import io
import os
import shutil
import tempfile
import unittest
from unittest.mock import MagicMock, patch

from ..buildopts import BuildOptions
from ..cli import VendorCmd
from ..fetcher import (
    ChangeStatus,
    LocalDirFetcher,
    PreinstalledNopFetcher,
    ShipitTransformerFetcher,
)
from ..getdeps_platform import HostType
from ..manifest import ManifestContext, ManifestParser


def make_manifest(name: str, extra: str = "") -> ManifestParser:
    return ManifestParser(name, f"[manifest]\nname = {name}\n{extra}")


def make_ctx() -> ManifestContext:
    return ManifestContext(
        {
            "os": "linux",
            "distro": None,
            "distro_vers": None,
            "fb": "off",
            "fbsource": "off",
            "test": "off",
        }
    )


class FakeSourceFetcher:
    """Stands in for a Git/Archive fetcher: owns a source tree on disk."""

    def __init__(self, src_dir: str, hash_value: str) -> None:
        self.src_dir = src_dir
        self.hash_value = hash_value
        self.updated = False

    def update(self) -> ChangeStatus:
        self.updated = True
        return ChangeStatus()

    def hash(self) -> str:
        return self.hash_value

    def get_src_dir(self) -> str:
        return self.src_dir


class VendorCmdTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.output_dir = os.path.join(self.tmp, "vendor")

    def make_src_tree(self, name: str) -> str:
        src = os.path.join(self.tmp, "src", name)
        os.makedirs(os.path.join(src, ".git"))
        os.makedirs(os.path.join(src, "sub"))
        with open(os.path.join(src, ".git", "HEAD"), "w") as f:
            f.write("ref: refs/heads/main\n")
        with open(os.path.join(src, "sub", "code.cpp"), "w") as f:
            f.write("int x;\n")
        # a symlink into a directory that won't exist on an offline builder
        os.symlink(os.path.join(src, "sub", "code.cpp"), os.path.join(src, "link.cpp"))
        return src

    def run_vendor(self, manifests, fetchers) -> None:
        loader = MagicMock()
        loader.manifests_in_dependency_order.return_value = manifests
        loader.create_fetcher.side_effect = lambda m: fetchers[m.name]
        args = argparse.Namespace(output_dir=self.output_dir)
        VendorCmd().run_project_cmd(args, loader, manifests[-1])

    def test_vendors_source_deps_and_skips_system_and_top_level(self) -> None:
        dep_src = FakeSourceFetcher(self.make_src_tree("depa"), "a" * 40)
        top_src = FakeSourceFetcher(self.make_src_tree("top"), "t" * 40)
        manifests = [
            make_manifest("depa"),
            make_manifest("sysdep"),
            make_manifest("top"),
        ]
        fetchers = {
            "depa": dep_src,
            "sysdep": PreinstalledNopFetcher(),
            "top": top_src,
        }

        self.run_vendor(manifests, fetchers)

        self.assertTrue(dep_src.updated, "source dep must be fetched before copying")
        self.assertFalse(top_src.updated, "the project itself is not vendored")
        self.assertEqual(
            sorted(os.listdir(self.output_dir)), ["depa", "getdeps-vendor.txt"]
        )

        vendored = os.path.join(self.output_dir, "depa")
        self.assertTrue(os.path.isfile(os.path.join(vendored, "sub", "code.cpp")))
        self.assertFalse(os.path.exists(os.path.join(vendored, ".git")))
        # symlinks are materialised so the tree is self-contained
        self.assertTrue(os.path.isfile(os.path.join(vendored, "link.cpp")))
        self.assertFalse(os.path.islink(os.path.join(vendored, "link.cpp")))

        with open(os.path.join(self.output_dir, "getdeps-vendor.txt")) as f:
            self.assertEqual(f.read(), "depa %s\n" % ("a" * 40))

    def test_replaces_stale_vendored_tree(self) -> None:
        stale = os.path.join(self.output_dir, "depa", "stale.txt")
        os.makedirs(os.path.dirname(stale))
        with open(stale, "w") as f:
            f.write("old\n")
        dep_src = FakeSourceFetcher(self.make_src_tree("depa"), "b" * 40)
        manifests = [make_manifest("depa"), make_manifest("top")]
        fetchers = {"depa": dep_src, "top": FakeSourceFetcher(self.tmp, "t" * 40)}

        self.run_vendor(manifests, fetchers)

        self.assertFalse(os.path.exists(stale))
        self.assertTrue(
            os.path.isfile(os.path.join(self.output_dir, "depa", "sub", "code.cpp"))
        )

    def test_removes_trees_no_longer_vendored(self) -> None:
        stale = os.path.join(self.output_dir, "olddep", "sub")
        os.makedirs(stale)
        with open(os.path.join(stale, "old.cpp"), "w") as f:
            f.write("old\n")
        # A previous run vendored depa and olddep; only depa still is one.
        with open(os.path.join(self.output_dir, "getdeps-vendor.txt"), "w") as f:
            f.write("depa %s\nolddep %s\n" % ("b" * 40, "c" * 40))
        dep_src = FakeSourceFetcher(self.make_src_tree("depa"), "a" * 40)
        manifests = [make_manifest("depa"), make_manifest("top")]
        fetchers = {"depa": dep_src, "top": FakeSourceFetcher(self.tmp, "t" * 40)}

        self.run_vendor(manifests, fetchers)

        self.assertFalse(os.path.exists(os.path.join(self.output_dir, "olddep")))
        self.assertTrue(
            os.path.isfile(os.path.join(self.output_dir, "depa", "sub", "code.cpp"))
        )

    def test_keeps_entries_missing_from_previous_manifest(self) -> None:
        # Unrelated data (a misdirected --output-dir, or the user's own
        # files) must never be deleted: only names from the previous
        # manifest are pruned.
        keep_file = os.path.join(self.output_dir, "my-notes.txt")
        os.makedirs(self.output_dir)
        with open(keep_file, "w") as f:
            f.write("do not delete\n")
        keep_dir = os.path.join(self.output_dir, "scratch")
        os.makedirs(os.path.join(keep_dir, "sub"))
        dep_src = FakeSourceFetcher(self.make_src_tree("depa"), "a" * 40)
        manifests = [make_manifest("depa"), make_manifest("top")]
        fetchers = {"depa": dep_src, "top": FakeSourceFetcher(self.tmp, "t" * 40)}

        self.run_vendor(manifests, fetchers)

        self.assertTrue(os.path.isfile(keep_file))
        self.assertTrue(os.path.isdir(keep_dir))
        self.assertTrue(
            os.path.isfile(os.path.join(self.output_dir, "depa", "sub", "code.cpp"))
        )

    def test_warns_about_dangling_symlinks(self) -> None:
        src = self.make_src_tree("depa")
        os.symlink(
            os.path.join(src, "no-such-file.cpp"), os.path.join(src, "dangling.cpp")
        )
        dep_src = FakeSourceFetcher(src, "a" * 40)
        manifests = [make_manifest("depa"), make_manifest("top")]
        fetchers = {"depa": dep_src, "top": FakeSourceFetcher(self.tmp, "t" * 40)}

        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            self.run_vendor(manifests, fetchers)

        self.assertIn("dangling", buf.getvalue())
        vendored = os.path.join(self.output_dir, "depa")
        self.assertFalse(os.path.lexists(os.path.join(vendored, "dangling.cpp")))
        self.assertTrue(os.path.isfile(os.path.join(vendored, "sub", "code.cpp")))


class VendorDirFetcherTest(unittest.TestCase):
    """--vendor-dir routes third-party deps through LocalDirFetcher, or fails."""

    DOWNLOAD_MANIFEST = """
[download]
url = https://example.com/dep-1.0.tar.gz
sha256 = 0000000000000000000000000000000000000000000000000000000000000000
"""

    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.build_opts = MagicMock()
        self.build_opts.use_shipit = False
        self.build_opts.fbsource_dir = None
        self.build_opts.allow_system_packages = False
        self.build_opts.vendor_dir = os.path.join(self.tmp, "vendor")
        patcher = patch.object(
            ShipitTransformerFetcher, "available", return_value=False
        )
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_vendored_project_uses_local_dir_fetcher(self) -> None:
        vendored = os.path.join(self.build_opts.vendor_dir, "dep")
        os.makedirs(vendored)
        manifest = make_manifest("dep", self.DOWNLOAD_MANIFEST)

        fetcher = manifest._create_fetcher(self.build_opts, make_ctx())

        self.assertIsInstance(fetcher, LocalDirFetcher)
        self.assertEqual(fetcher.get_src_dir(), os.path.realpath(vendored))

    def test_missing_vendored_project_fails_instead_of_downloading(self) -> None:
        os.makedirs(self.build_opts.vendor_dir)
        manifest = make_manifest("dep", self.DOWNLOAD_MANIFEST)

        with self.assertRaisesRegex(
            Exception, "project dep is not present in .*vendor"
        ):
            manifest._create_fetcher(self.build_opts, make_ctx())

    def test_no_vendor_dir_keeps_normal_fetcher(self) -> None:
        self.build_opts.vendor_dir = None
        manifest = make_manifest("dep", self.DOWNLOAD_MANIFEST)

        fetcher = manifest._create_fetcher(self.build_opts, make_ctx())

        self.assertNotIsInstance(fetcher, LocalDirFetcher)
        self.assertEqual(fetcher.hash(), "0" * 64)

    def test_vendor_dir_ignored_when_no_fetch_config(self) -> None:
        # A project with no [git]/[download] config is meant to come from
        # the environment, and `vendor` skips it too, so --vendor-dir must
        # not fail claiming it is missing from the vendor dir.
        self.build_opts.host_type.get_package_manager.return_value = None
        manifest = make_manifest("dep")

        with self.assertRaisesRegex(KeyError, "no fetcher configuration"):
            manifest._create_fetcher(self.build_opts, make_ctx())


class VendorDirRealpathTest(unittest.TestCase):
    """BuildOptions stores vendor_dir realpath'd so prefix checks against
    LocalDirFetcher's realpath'd source paths line up."""

    def test_vendor_dir_stored_as_realpath(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            link = os.path.join(tmp, "link")
            os.symlink(tmp, link)
            aliased = os.path.join(link, "vendor")
            # Sanity: the aliased input genuinely differs from its realpath,
            # so this test would fail if realpath were not applied.
            self.assertNotEqual(aliased, os.path.realpath(aliased))

            opts = BuildOptions(
                fbcode_builder_dir=tmp,
                scratch_dir=os.path.join(tmp, "scratch"),
                host_type=HostType(),
                vendor_dir=aliased,
            )

            self.assertEqual(opts.vendor_dir, os.path.realpath(aliased))

    def test_vendor_dir_none_stays_none(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            opts = BuildOptions(
                fbcode_builder_dir=tmp,
                scratch_dir=os.path.join(tmp, "scratch"),
                host_type=HostType(),
            )

            self.assertIsNone(opts.vendor_dir)
