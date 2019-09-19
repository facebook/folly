# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals

import base64
import glob
import hashlib
import os

from . import fetcher
from .envfuncs import path_search
from .manifest import ManifestParser


class Loader(object):
    """ The loader allows our tests to patch the load operation """

    def load_project(self, build_opts, project_name):
        manifest_path = resolve_manifest_path(build_opts, project_name)
        return ManifestParser(manifest_path)

    def load_all(self, build_opts):
        manifests_by_name = {}
        manifests_dir = os.path.join(build_opts.fbcode_builder_dir, "manifests")
        # We use glob rather than os.listdir because glob won't include
        # eg: vim swap files that a maintainer might happen to have
        # for manifests that they are editing
        for name in glob.glob("%s/*" % manifests_dir):
            m = ManifestParser(name)
            manifests_by_name[m.name] = m

        return manifests_by_name


class ResourceLoader(Loader):
    def __init__(self, namespace):
        self.namespace = namespace

    def load_project(self, build_opts, project_name):
        import pkg_resources

        contents = pkg_resources.resource_string(
            self.namespace, "manifests/%s" % project_name
        ).decode("utf8")
        m = ManifestParser(file_name=project_name, fp=contents)
        return m

    def load_all(self, build_opts):
        import pkg_resources

        manifest_by_name = {}
        for name in pkg_resources.resource_listdir(self.namespace, "manifests"):
            m = self.load_project(build_opts, name)
            manifest_by_name[m.name] = m
        return manifest_by_name


LOADER = Loader()


def patch_loader(namespace):
    global LOADER
    LOADER = ResourceLoader(namespace)


def resolve_manifest_path(build_opts, project_name):
    if "/" in project_name or "\\" in project_name:
        # Assume this is a path already
        return project_name

    # Otherwise, resolve it relative to the manifests dir
    return os.path.join(build_opts.fbcode_builder_dir, "manifests", project_name)


def load_project(build_opts, project_name):
    """ given the name of a project or a path to a manifest file,
    load up the ManifestParser instance for it and return it """
    return LOADER.load_project(build_opts, project_name)


def load_all_manifests(build_opts):
    return LOADER.load_all(build_opts)


class ManifestLoader(object):
    """ ManifestLoader stores information about project manifest relationships for a
    given set of (build options + platform) configuration.

    The ManifestLoader class primarily serves as a location to cache project dependency
    relationships and project hash values for this build configuration.
    """

    def __init__(self, build_opts, ctx_gen=None):
        self._loader = LOADER
        self.build_opts = build_opts
        if ctx_gen is None:
            self.ctx_gen = self.build_opts.get_context_generator()
        else:
            self.ctx_gen = ctx_gen

        self.manifests_by_name = {}
        self._loaded_all = False
        self._project_hashes = {}
        self._fetcher_overrides = {}
        self._build_dir_overrides = {}
        self._install_dir_overrides = {}

    def load_manifest(self, name):
        manifest = self.manifests_by_name.get(name)
        if manifest is None:
            manifest = self._loader.load_project(self.build_opts, name)
            self.manifests_by_name[name] = manifest
        return manifest

    def load_all_manifests(self):
        if not self._loaded_all:
            all_manifests_by_name = self._loader.load_all(self.build_opts)
            if self.manifests_by_name:
                # To help ensure that we only ever have a single manifest object for a
                # given project, and that it can't change once we have loaded it,
                # only update our mapping for projects that weren't already loaded.
                for name, manifest in all_manifests_by_name.items():
                    self.manifests_by_name.setdefault(name, manifest)
            else:
                self.manifests_by_name = all_manifests_by_name
            self._loaded_all = True

        return self.manifests_by_name

    def manifests_in_dependency_order(self, manifest=None):
        """ Compute all dependencies of the specified project.  Returns a list of the
        dependencies plus the project itself, in topologically sorted order.

        Each entry in the returned list only depends on projects that appear before it
        in the list.

        If the input manifest is None, the dependencies for all currently loaded
        projects will be computed.  i.e., if you call load_all_manifests() followed by
        manifests_in_dependency_order() this will return a global dependency ordering of
        all projects.  """
        # The list of deps that have been fully processed
        seen = set()
        # The list of deps which have yet to be evaluated.  This
        # can potentially contain duplicates.
        if manifest is None:
            deps = list(self.manifests_by_name.values())
        else:
            assert manifest.name in self.manifests_by_name
            deps = [manifest]
        # The list of manifests in dependency order
        dep_order = []

        while len(deps) > 0:
            m = deps.pop(0)
            if m.name in seen:
                continue

            # Consider its deps, if any.
            # We sort them for increased determinism; we'll produce
            # a correct order even if they aren't sorted, but we prefer
            # to produce the same order regardless of how they are listed
            # in the project manifest files.
            ctx = self.ctx_gen.get_context(m.name)
            dep_list = sorted(m.get_section_as_dict("dependencies", ctx).keys())
            builder = m.get("build", "builder", ctx=ctx)
            if builder in ("cmake", "python-wheel"):
                dep_list.append("cmake")
            elif builder == "autoconf" and m.name not in (
                "autoconf",
                "libtool",
                "automake",
            ):
                # they need libtool and its deps (automake, autoconf) so add
                # those as deps (but obviously not if we're building those
                # projects themselves)
                dep_list.append("libtool")

            dep_count = 0
            for dep_name in dep_list:
                # If we're not sure whether it is done, queue it up
                if dep_name not in seen:
                    dep = self.manifests_by_name.get(dep_name)
                    if dep is None:
                        dep = self._loader.load_project(self.build_opts, dep_name)
                        self.manifests_by_name[dep.name] = dep

                    deps.append(dep)
                    dep_count += 1

            if dep_count > 0:
                # If we queued anything, re-queue this item, as it depends
                # those new item(s) and their transitive deps.
                deps.append(m)
                continue

            # Its deps are done, so we can emit it
            seen.add(m.name)
            dep_order.append(m)

        return dep_order

    def set_project_src_dir(self, project_name, path):
        self._fetcher_overrides[project_name] = fetcher.LocalDirFetcher(path)

    def set_project_build_dir(self, project_name, path):
        self._build_dir_overrides[project_name] = path

    def set_project_install_dir(self, project_name, path):
        self._install_dir_overrides[project_name] = path

    def create_fetcher(self, manifest):
        override = self._fetcher_overrides.get(manifest.name)
        if override is not None:
            return override

        ctx = self.ctx_gen.get_context(manifest.name)
        return manifest.create_fetcher(self.build_opts, ctx)

    def get_project_hash(self, manifest):
        h = self._project_hashes.get(manifest.name)
        if h is None:
            h = self._compute_project_hash(manifest)
            self._project_hashes[manifest.name] = h
        return h

    def _compute_project_hash(self, manifest):
        """ This recursive function computes a hash for a given manifest.
        The hash takes into account some environmental factors on the
        host machine and includes the hashes of its dependencies.
        No caching of the computation is performed, which is theoretically
        wasteful but the computation is fast enough that it is not required
        to cache across multiple invocations. """
        hasher = hashlib.sha256()
        # Some environmental and configuration things matter
        env = {}
        env["install_dir"] = self.build_opts.install_dir
        env["scratch_dir"] = self.build_opts.scratch_dir
        env["os"] = self.build_opts.host_type.ostype
        env["distro"] = self.build_opts.host_type.distro
        env["distro_vers"] = self.build_opts.host_type.distrovers
        for name in ["CXXFLAGS", "CPPFLAGS", "LDFLAGS", "CXX", "CC"]:
            env[name] = os.environ.get(name)
        for tool in ["cc", "c++", "gcc", "g++", "clang", "clang++"]:
            env["tool-%s" % tool] = path_search(os.environ, tool)

        fetcher = self.create_fetcher(manifest)
        env["fetcher.hash"] = fetcher.hash()

        for name in sorted(env.keys()):
            hasher.update(name.encode("utf-8"))
            value = env.get(name)
            if value is not None:
                hasher.update(value.encode("utf-8"))

        ctx = self.ctx_gen.get_context(manifest.name)
        manifest.update_hash(hasher, ctx)

        dep_list = sorted(manifest.get_section_as_dict("dependencies", ctx).keys())
        for dep in dep_list:
            dep_manifest = self.load_manifest(dep)
            dep_hash = self.get_project_hash(dep_manifest)
            hasher.update(dep_hash.encode("utf-8"))

        # Use base64 to represent the hash, rather than the simple hex digest,
        # so that the string is shorter.  Use the URL-safe encoding so that
        # the hash can also be safely used as a filename component.
        h = base64.urlsafe_b64encode(hasher.digest()).decode("ascii")
        # ... and because cmd.exe is troublesome with `=` signs, nerf those.
        # They tend to be padding characters at the end anyway, so we can
        # safely discard them.
        h = h.replace("=", "")

        return h

    def _get_project_dir_name(self, manifest):
        if manifest.is_first_party_project():
            return manifest.name
        else:
            project_hash = self.get_project_hash(manifest)
            return "%s-%s" % (manifest.name, project_hash)

    def get_project_install_dir(self, manifest):
        override = self._install_dir_overrides.get(manifest.name)
        if override:
            return override

        project_dir_name = self._get_project_dir_name(manifest)
        return os.path.join(self.build_opts.install_dir, project_dir_name)

    def get_project_build_dir(self, manifest):
        override = self._build_dir_overrides.get(manifest.name)
        if override:
            return override

        project_dir_name = self._get_project_dir_name(manifest)
        return os.path.join(self.build_opts.scratch_dir, "build", project_dir_name)
