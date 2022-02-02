#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import os
import shutil

from .builder import BuilderBase


class CargoBuilder(BuilderBase):
    def __init__(
        self,
        build_opts,
        ctx,
        manifest,
        src_dir,
        build_dir,
        inst_dir,
        build_doc,
        workspace_dir,
        manifests_to_build,
        loader,
    ):
        super(CargoBuilder, self).__init__(
            build_opts, ctx, manifest, src_dir, build_dir, inst_dir
        )
        self.build_doc = build_doc
        self.ws_dir = workspace_dir
        self.manifests_to_build = manifests_to_build and manifests_to_build.split(",")
        self.loader = loader

    def run_cargo(self, install_dirs, operation, args=None):
        args = args or []
        env = self._compute_env(install_dirs)
        # Enable using nightly features with stable compiler
        env["RUSTC_BOOTSTRAP"] = "1"
        env["LIBZ_SYS_STATIC"] = "1"
        cmd = [
            "cargo",
            operation,
            "--workspace",
            "-j%s" % self.num_jobs,
        ] + args
        self._run_cmd(cmd, cwd=self.workspace_dir(), env=env)

    def build_source_dir(self):
        return os.path.join(self.build_dir, "source")

    def workspace_dir(self):
        return os.path.join(self.build_source_dir(), self.ws_dir or "")

    def manifest_dir(self, manifest):
        return os.path.join(self.build_source_dir(), manifest)

    def recreate_dir(self, src, dst):
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst)

    def _build(self, install_dirs, reconfigure):
        build_source_dir = self.build_source_dir()
        self.recreate_dir(self.src_dir, build_source_dir)

        dot_cargo_dir = os.path.join(build_source_dir, ".cargo")
        if not os.path.isdir(dot_cargo_dir):
            os.mkdir(dot_cargo_dir)

        with open(os.path.join(dot_cargo_dir, "config"), "w+") as f:
            f.write(
                """\
[build]
target-dir = '''{}'''

[net]
git-fetch-with-cli = true

[profile.dev]
debug = false
incremental = false
""".format(
                    self.build_dir.replace("\\", "\\\\")
                )
            )

        if self.ws_dir is not None:
            self._patchup_workspace()

        try:
            from .facebook.rust import vendored_crates

            vendored_crates(self.build_opts, build_source_dir)
        except ImportError:
            # This FB internal module isn't shippped to github,
            # so just rely on cargo downloading crates on it's own
            pass

        if self.manifests_to_build is None:
            self.run_cargo(
                install_dirs,
                "build",
                ["--out-dir", os.path.join(self.inst_dir, "bin"), "-Zunstable-options"],
            )
        else:
            for manifest in self.manifests_to_build:
                self.run_cargo(
                    install_dirs,
                    "build",
                    [
                        "--out-dir",
                        os.path.join(self.inst_dir, "bin"),
                        "-Zunstable-options",
                        "--manifest-path",
                        self.manifest_dir(manifest),
                    ],
                )

        self.recreate_dir(build_source_dir, os.path.join(self.inst_dir, "source"))

    def run_tests(
        self, install_dirs, schedule_type, owner, test_filter, retry, no_testpilot
    ):
        if test_filter:
            args = ["--", test_filter]
        else:
            args = []

        if self.manifests_to_build is None:
            self.run_cargo(install_dirs, "test", args)
            if self.build_doc:
                self.run_cargo(install_dirs, "doc", ["--no-deps"])
        else:
            for manifest in self.manifests_to_build:
                margs = ["--manifest-path", self.manifest_dir(manifest)]
                self.run_cargo(install_dirs, "test", args + margs)
                if self.build_doc:
                    self.run_cargo(install_dirs, "doc", ["--no-deps"] + margs)

    def _patchup_workspace(self):
        """
        This method makes some assumptions about the state of the project and
        its cargo dependendies:
        1. Crates from cargo dependencies can be extracted from Cargo.toml files
           using _extract_crates function. It is using a heuristic so check its
           code to understand how it is done.
        2. The extracted cargo dependencies crates can be found in the
           dependency's install dir using _resolve_crate_to_path function
           which again is using a heuristic.

        Notice that many things might go wrong here. E.g. if someone depends
        on another getdeps crate by writing in their Cargo.toml file:

            my-rename-of-crate = { package = "crate", git = "..." }

        they can count themselves lucky because the code will raise an
        Exception. There migh be more cases where the code will silently pass
        producing bad results.
        """
        workspace_dir = self.workspace_dir()
        config = self._resolve_config()
        if config:
            with open(os.path.join(workspace_dir, "Cargo.toml"), "r+") as f:
                manifest_content = f.read()
                if "[package]" not in manifest_content:
                    # A fake manifest has to be crated to change the virtual
                    # manifest into a non-virtual. The virtual manifests are limited
                    # in many ways and the inability to define patches on them is
                    # one. Check https://github.com/rust-lang/cargo/issues/4934 to
                    # see if it is resolved.
                    f.write(
                        """
    [package]
    name = "fake_manifest_of_{}"
    version = "0.0.0"
    [lib]
    path = "/dev/null"
    """.format(
                            self.manifest.name
                        )
                    )
                else:
                    f.write("\n")
                f.write(config)

    def _resolve_config(self):
        """
        Returns a configuration to be put inside root Cargo.toml file which
        patches the dependencies git code with local getdeps versions.
        See https://doc.rust-lang.org/cargo/reference/manifest.html#the-patch-section
        """
        dep_to_git = self._resolve_dep_to_git()
        dep_to_crates = CargoBuilder._resolve_dep_to_crates(
            self.build_source_dir(), dep_to_git
        )

        config = []
        for name in sorted(dep_to_git.keys()):
            git_conf = dep_to_git[name]
            crates = sorted(dep_to_crates.get(name, []))
            if not crates:
                continue  # nothing to patch, move along
            crates_patches = [
                '{} = {{ path = "{}" }}'.format(
                    crate,
                    CargoBuilder._resolve_crate_to_path(crate, git_conf).replace(
                        "\\", "\\\\"
                    ),
                )
                for crate in crates
            ]

            config.append(
                '[patch."{0}"]\n'.format(git_conf["repo_url"])
                + "\n".join(crates_patches)
            )
        return "\n".join(config)

    def _resolve_dep_to_git(self):
        """
        For each direct dependency of the currently build manifest check if it
        is also cargo-builded and if yes then extract it's git configs and
        install dir
        """
        dependencies = self.manifest.get_dependencies(self.ctx)
        if not dependencies:
            return []

        dep_to_git = {}
        for dep in dependencies:
            dep_manifest = self.loader.load_manifest(dep)
            dep_builder = dep_manifest.get("build", "builder", ctx=self.ctx)
            if dep_builder not in ["cargo", "nop"] or dep == "rust":
                # This is a direct dependency, but it is not build with cargo
                # and it is not simply copying files with nop, so ignore it.
                # The "rust" dependency is an exception since it contains the
                # toolchain.
                continue

            git_conf = dep_manifest.get_section_as_dict("git", self.ctx)
            if "repo_url" not in git_conf:
                raise Exception(
                    "A cargo dependency requires git.repo_url to be defined."
                )
            source_dir = self.loader.get_project_install_dir(dep_manifest)
            if dep_builder == "cargo":
                source_dir = os.path.join(source_dir, "source")
            git_conf["source_dir"] = source_dir
            dep_to_git[dep] = git_conf
        return dep_to_git

    @staticmethod
    def _resolve_dep_to_crates(build_source_dir, dep_to_git):
        """
        This function traverse the build_source_dir in search of Cargo.toml
        files, extracts the crate names from them using _extract_crates
        function and returns a merged result containing crate names per
        dependency name from all Cargo.toml files in the project.
        """
        if not dep_to_git:
            return {}  # no deps, so don't waste time traversing files

        dep_to_crates = {}
        for root, _, files in os.walk(build_source_dir):
            for f in files:
                if f == "Cargo.toml":
                    more_dep_to_crates = CargoBuilder._extract_crates(
                        os.path.join(root, f), dep_to_git
                    )
                    for name, crates in more_dep_to_crates.items():
                        dep_to_crates.setdefault(name, set()).update(crates)
        return dep_to_crates

    @staticmethod
    def _extract_crates(cargo_toml_file, dep_to_git):
        """
        This functions reads content of provided cargo toml file and extracts
        crate names per each dependency. The extraction is done by a heuristic
        so it might be incorrect.
        """
        deps_to_crates = {}
        with open(cargo_toml_file, "r") as f:
            for line in f.readlines():
                if line.startswith("#") or "git = " not in line:
                    continue  # filter out commented lines and ones without git deps
                for name, conf in dep_to_git.items():
                    if 'git = "{}"'.format(conf["repo_url"]) in line:
                        pkg_template = ' package = "'
                        if pkg_template in line:
                            crate_name, _, _ = line.partition(pkg_template)[
                                2
                            ].partition('"')
                        else:
                            crate_name, _, _ = line.partition("=")
                        deps_to_crates.setdefault(name, set()).add(crate_name.strip())
        return deps_to_crates

    @staticmethod
    def _resolve_crate_to_path(crate, git_conf):
        """
        Tries to find <crate> in git_conf["inst_dir"] by searching a [package]
        keyword followed by name = "<crate>".
        """
        source_dir = git_conf["source_dir"]
        search_pattern = '[package]\nname = "{}"'.format(crate)

        for root, _, files in os.walk(source_dir):
            for fname in files:
                if fname == "Cargo.toml":
                    with open(os.path.join(root, fname), "r") as f:
                        if search_pattern in f.read():
                            return root

        raise Exception("Failed to found crate {} in path {}".format(crate, source_dir))
