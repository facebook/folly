# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import io
import os

from .builder import (
    AutoconfBuilder,
    Boost,
    CargoBuilder,
    CMakeBuilder,
    Iproute2Builder,
    MakeBuilder,
    NinjaBootstrap,
    NopBuilder,
    OpenNSABuilder,
    OpenSSLBuilder,
    SqliteBuilder,
)
from .expr import parse_expr
from .fetcher import (
    ArchiveFetcher,
    GitFetcher,
    PreinstalledNopFetcher,
    ShipitTransformerFetcher,
    SimpleShipitTransformerFetcher,
    SystemPackageFetcher,
)
from .py_wheel_builder import PythonWheelBuilder


try:
    import configparser
except ImportError:
    import ConfigParser as configparser

REQUIRED = "REQUIRED"
OPTIONAL = "OPTIONAL"

SCHEMA = {
    "manifest": {
        "optional_section": False,
        "fields": {
            "name": REQUIRED,
            "fbsource_path": OPTIONAL,
            "shipit_project": OPTIONAL,
            "shipit_fbcode_builder": OPTIONAL,
        },
    },
    "dependencies": {"optional_section": True, "allow_values": False},
    "depends.environment": {"optional_section": True},
    "git": {
        "optional_section": True,
        "fields": {"repo_url": REQUIRED, "rev": OPTIONAL, "depth": OPTIONAL},
    },
    "download": {
        "optional_section": True,
        "fields": {"url": REQUIRED, "sha256": REQUIRED},
    },
    "build": {
        "optional_section": True,
        "fields": {
            "builder": REQUIRED,
            "subdir": OPTIONAL,
            "build_in_src_dir": OPTIONAL,
            "disable_env_override_pkgconfig": OPTIONAL,
            "disable_env_override_path": OPTIONAL,
        },
    },
    "msbuild": {"optional_section": True, "fields": {"project": REQUIRED}},
    "cargo": {
        "optional_section": True,
        "fields": {"build_doc": OPTIONAL, "workspace_dir": OPTIONAL},
    },
    "cmake.defines": {"optional_section": True},
    "autoconf.args": {"optional_section": True},
    "rpms": {"optional_section": True},
    "debs": {"optional_section": True},
    "preinstalled.env": {"optional_section": True},
    "b2.args": {"optional_section": True},
    "make.build_args": {"optional_section": True},
    "make.install_args": {"optional_section": True},
    "header-only": {"optional_section": True, "fields": {"includedir": REQUIRED}},
    "shipit.pathmap": {"optional_section": True},
    "shipit.strip": {"optional_section": True},
    "install.files": {"optional_section": True},
}

# These sections are allowed to vary for different platforms
# using the expression syntax to enable/disable sections
ALLOWED_EXPR_SECTIONS = [
    "autoconf.args",
    "build",
    "cmake.defines",
    "dependencies",
    "make.build_args",
    "make.install_args",
    "b2.args",
    "download",
    "git",
    "install.files",
]


def parse_conditional_section_name(name, section_def):
    expr = name[len(section_def) + 1 :]
    return parse_expr(expr, ManifestContext.ALLOWED_VARIABLES)


def validate_allowed_fields(file_name, section, config, allowed_fields):
    for field in config.options(section):
        if not allowed_fields.get(field):
            raise Exception(
                ("manifest file %s section '%s' contains " "unknown field '%s'")
                % (file_name, section, field)
            )

    for field in allowed_fields:
        if allowed_fields[field] == REQUIRED and not config.has_option(section, field):
            raise Exception(
                ("manifest file %s section '%s' is missing " "required field '%s'")
                % (file_name, section, field)
            )


def validate_allow_values(file_name, section, config):
    for field in config.options(section):
        value = config.get(section, field)
        if value is not None:
            raise Exception(
                (
                    "manifest file %s section '%s' has '%s = %s' but "
                    "this section doesn't allow specifying values "
                    "for its entries"
                )
                % (file_name, section, field, value)
            )


def validate_section(file_name, section, config):
    section_def = SCHEMA.get(section)
    if not section_def:
        for name in ALLOWED_EXPR_SECTIONS:
            if section.startswith(name + "."):
                # Verify that the conditional parses, but discard it
                try:
                    parse_conditional_section_name(section, name)
                except Exception as exc:
                    raise Exception(
                        ("manifest file %s section '%s' has invalid " "conditional: %s")
                        % (file_name, section, str(exc))
                    )
                section_def = SCHEMA.get(name)
                canonical_section_name = name
                break
        if not section_def:
            raise Exception(
                "manifest file %s contains unknown section '%s'" % (file_name, section)
            )
    else:
        canonical_section_name = section

    allowed_fields = section_def.get("fields")
    if allowed_fields:
        validate_allowed_fields(file_name, section, config, allowed_fields)
    elif not section_def.get("allow_values", True):
        validate_allow_values(file_name, section, config)
    return canonical_section_name


class ManifestParser(object):
    def __init__(self, file_name, fp=None):
        # allow_no_value enables listing parameters in the
        # autoconf.args section one per line
        config = configparser.RawConfigParser(allow_no_value=True)
        config.optionxform = str  # make it case sensitive

        if fp is None:
            with open(file_name, "r") as fp:
                config.read_file(fp)
        elif isinstance(fp, type("")):
            # For testing purposes, parse from a string (str
            # or unicode)
            config.read_file(io.StringIO(fp))
        else:
            config.read_file(fp)

        # validate against the schema
        seen_sections = set()

        for section in config.sections():
            seen_sections.add(validate_section(file_name, section, config))

        for section in SCHEMA.keys():
            section_def = SCHEMA[section]
            if (
                not section_def.get("optional_section", False)
                and section not in seen_sections
            ):
                raise Exception(
                    "manifest file %s is missing required section %s"
                    % (file_name, section)
                )

        self._config = config
        self.name = config.get("manifest", "name")
        self.fbsource_path = self.get("manifest", "fbsource_path")
        self.shipit_project = self.get("manifest", "shipit_project")
        self.shipit_fbcode_builder = self.get("manifest", "shipit_fbcode_builder")

        if self.name != os.path.basename(file_name):
            raise Exception(
                "filename of the manifest '%s' does not match the manifest name '%s'"
                % (file_name, self.name)
            )

    def get(self, section, key, defval=None, ctx=None):
        ctx = ctx or {}

        for s in self._config.sections():
            if s == section:
                if self._config.has_option(s, key):
                    return self._config.get(s, key)
                return defval

            if s.startswith(section + "."):
                expr = parse_conditional_section_name(s, section)
                if not expr.eval(ctx):
                    continue

                if self._config.has_option(s, key):
                    return self._config.get(s, key)

        return defval

    def get_section_as_args(self, section, ctx=None):
        """ Intended for use with the make.[build_args/install_args] and
        autoconf.args sections, this method collects the entries and returns an
        array of strings.
        If the manifest contains conditional sections, ctx is used to
        evaluate the condition and merge in the values.
        """
        args = []
        ctx = ctx or {}

        for s in self._config.sections():
            if s != section:
                if not s.startswith(section + "."):
                    continue
                expr = parse_conditional_section_name(s, section)
                if not expr.eval(ctx):
                    continue
            for field in self._config.options(s):
                value = self._config.get(s, field)
                if value is None:
                    args.append(field)
                else:
                    args.append("%s=%s" % (field, value))
        return args

    def get_section_as_ordered_pairs(self, section, ctx=None):
        """ Used for eg: shipit.pathmap which has strong
        ordering requirements """
        res = []
        ctx = ctx or {}

        for s in self._config.sections():
            if s != section:
                if not s.startswith(section + "."):
                    continue
                expr = parse_conditional_section_name(s, section)
                if not expr.eval(ctx):
                    continue

            for key in self._config.options(s):
                value = self._config.get(s, key)
                res.append((key, value))
        return res

    def get_section_as_dict(self, section, ctx=None):
        d = {}
        ctx = ctx or {}

        for s in self._config.sections():
            if s != section:
                if not s.startswith(section + "."):
                    continue
                expr = parse_conditional_section_name(s, section)
                if not expr.eval(ctx):
                    continue
            for field in self._config.options(s):
                value = self._config.get(s, field)
                d[field] = value
        return d

    def update_hash(self, hasher, ctx):
        """ Compute a hash over the configuration for the given
        context.  The goal is for the hash to change if the config
        for that context changes, but not if a change is made to
        the config only for a different platform than that expressed
        by ctx.  The hash is intended to be used to help invalidate
        a future cache for the third party build products.
        The hasher argument is a hash object returned from hashlib. """
        for section in sorted(SCHEMA.keys()):
            hasher.update(section.encode("utf-8"))

            # Note: at the time of writing, nothing in the implementation
            # relies on keys in any config section being ordered.
            # In theory we could have conflicting flags in different
            # config sections and later flags override earlier flags.
            # For the purposes of computing a hash we're not super
            # concerned about this: manifest changes should be rare
            # enough and we'd rather that this trigger an invalidation
            # than strive for a cache hit at this time.
            pairs = self.get_section_as_ordered_pairs(section, ctx)
            pairs.sort(key=lambda pair: pair[0])
            for key, value in pairs:
                hasher.update(key.encode("utf-8"))
                if value is not None:
                    hasher.update(value.encode("utf-8"))

    def is_first_party_project(self):
        """ returns true if this is an FB first-party project """
        return self.shipit_project is not None

    def get_required_system_packages(self, ctx):
        """ Returns dictionary of packager system -> list of packages """
        return {
            "rpm": self.get_section_as_args("rpms", ctx),
            "deb": self.get_section_as_args("debs", ctx),
        }

    def _is_satisfied_by_preinstalled_environment(self, ctx):
        envs = self.get_section_as_args("preinstalled.env", ctx)
        if not envs:
            return False
        for key in envs:
            val = os.environ.get(key, None)
            print(f"Testing ENV[{key}]: {repr(val)}")
            if val is None:
                return False
            if len(val) == 0:
                return False

        return True

    def create_fetcher(self, build_options, ctx):
        use_real_shipit = (
            ShipitTransformerFetcher.available() and build_options.use_shipit
        )
        if (
            not use_real_shipit
            and self.fbsource_path
            and build_options.fbsource_dir
            and self.shipit_project
        ):
            return SimpleShipitTransformerFetcher(build_options, self)

        if (
            self.fbsource_path
            and build_options.fbsource_dir
            and self.shipit_project
            and ShipitTransformerFetcher.available()
        ):
            # We can use the code from fbsource
            return ShipitTransformerFetcher(build_options, self.shipit_project)

        # Can we satisfy this dep with system packages?
        if build_options.allow_system_packages:
            if self._is_satisfied_by_preinstalled_environment(ctx):
                return PreinstalledNopFetcher()

            packages = self.get_required_system_packages(ctx)
            package_fetcher = SystemPackageFetcher(build_options, packages)
            if package_fetcher.packages_are_installed():
                return package_fetcher

        repo_url = self.get("git", "repo_url", ctx=ctx)
        if repo_url:
            rev = self.get("git", "rev")
            depth = self.get("git", "depth")
            return GitFetcher(build_options, self, repo_url, rev, depth)

        url = self.get("download", "url", ctx=ctx)
        if url:
            # We need to defer this import until now to avoid triggering
            # a cycle when the facebook/__init__.py is loaded.
            try:
                from getdeps.facebook.lfs import LFSCachingArchiveFetcher

                return LFSCachingArchiveFetcher(
                    build_options, self, url, self.get("download", "sha256", ctx=ctx)
                )
            except ImportError:
                # This FB internal module isn't shippped to github,
                # so just use its base class
                return ArchiveFetcher(
                    build_options, self, url, self.get("download", "sha256", ctx=ctx)
                )

        raise KeyError(
            "project %s has no fetcher configuration matching %s" % (self.name, ctx)
        )

    def create_builder(  # noqa:C901
        self,
        build_options,
        src_dir,
        build_dir,
        inst_dir,
        ctx,
        loader,
        final_install_prefix=None,
    ):
        builder = self.get("build", "builder", ctx=ctx)
        if not builder:
            raise Exception("project %s has no builder for %r" % (self.name, ctx))
        build_in_src_dir = self.get("build", "build_in_src_dir", "false", ctx=ctx)
        if build_in_src_dir == "true":
            build_dir = src_dir
            print("build_dir is %s" % build_dir)  # just to quiet lint

        if builder == "make":
            build_args = self.get_section_as_args("make.build_args", ctx)
            install_args = self.get_section_as_args("make.install_args", ctx)
            return MakeBuilder(
                build_options,
                ctx,
                self,
                src_dir,
                None,
                inst_dir,
                build_args,
                install_args,
            )

        if builder == "autoconf":
            args = self.get_section_as_args("autoconf.args", ctx)
            return AutoconfBuilder(
                build_options, ctx, self, src_dir, build_dir, inst_dir, args
            )

        if builder == "boost":
            args = self.get_section_as_args("b2.args", ctx)
            return Boost(build_options, ctx, self, src_dir, build_dir, inst_dir, args)

        if builder == "cmake":
            defines = self.get_section_as_dict("cmake.defines", ctx)
            return CMakeBuilder(
                build_options,
                ctx,
                self,
                src_dir,
                build_dir,
                inst_dir,
                defines,
                final_install_prefix,
            )

        if builder == "python-wheel":
            return PythonWheelBuilder(
                build_options, ctx, self, src_dir, build_dir, inst_dir
            )

        if builder == "sqlite":
            return SqliteBuilder(build_options, ctx, self, src_dir, build_dir, inst_dir)

        if builder == "ninja_bootstrap":
            return NinjaBootstrap(
                build_options, ctx, self, build_dir, src_dir, inst_dir
            )

        if builder == "nop":
            return NopBuilder(build_options, ctx, self, src_dir, inst_dir)

        if builder == "openssl":
            return OpenSSLBuilder(
                build_options, ctx, self, build_dir, src_dir, inst_dir
            )

        if builder == "iproute2":
            return Iproute2Builder(
                build_options, ctx, self, src_dir, build_dir, inst_dir
            )

        if builder == "cargo":
            build_doc = self.get("cargo", "build_doc", False, ctx)
            workspace_dir = self.get("cargo", "workspace_dir", "", ctx)
            return CargoBuilder(
                build_options,
                ctx,
                self,
                src_dir,
                build_dir,
                inst_dir,
                build_doc,
                workspace_dir,
                loader,
            )

        if builder == "OpenNSA":
            return OpenNSABuilder(build_options, ctx, self, src_dir, inst_dir)

        raise KeyError("project %s has no known builder" % (self.name))


class ManifestContext(object):
    """ ProjectContext contains a dictionary of values to use when evaluating boolean
    expressions in a project manifest.

    This object should be passed as the `ctx` parameter in ManifestParser.get() calls.
    """

    ALLOWED_VARIABLES = {"os", "distro", "distro_vers", "fb", "test"}

    def __init__(self, ctx_dict):
        assert set(ctx_dict.keys()) == self.ALLOWED_VARIABLES
        self.ctx_dict = ctx_dict

    def get(self, key):
        return self.ctx_dict[key]

    def set(self, key, value):
        assert key in self.ALLOWED_VARIABLES
        self.ctx_dict[key] = value

    def copy(self):
        return ManifestContext(dict(self.ctx_dict))

    def __str__(self):
        s = ", ".join(
            "%s=%s" % (key, value) for key, value in sorted(self.ctx_dict.items())
        )
        return "{" + s + "}"


class ContextGenerator(object):
    """ ContextGenerator allows creating ManifestContext objects on a per-project basis.
    This allows us to evaluate different projects with slightly different contexts.

    For instance, this can be used to only enable tests for some projects. """

    def __init__(self, default_ctx):
        self.default_ctx = ManifestContext(default_ctx)
        self.ctx_by_project = {}

    def set_value_for_project(self, project_name, key, value):
        project_ctx = self.ctx_by_project.get(project_name)
        if project_ctx is None:
            project_ctx = self.default_ctx.copy()
            self.ctx_by_project[project_name] = project_ctx
        project_ctx.set(key, value)

    def set_value_for_all_projects(self, key, value):
        self.default_ctx.set(key, value)
        for ctx in self.ctx_by_project.values():
            ctx.set(key, value)

    def get_context(self, project_name):
        return self.ctx_by_project.get(project_name, self.default_ctx)
