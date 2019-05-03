#!/usr/bin/env python
# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals

import io

from .expr import parse_expr


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
    "git": {
        "optional_section": True,
        "fields": {"repo_url": REQUIRED, "rev": OPTIONAL},
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
        },
    },
    "msbuild": {"optional_section": True, "fields": {"project": REQUIRED}},
    "cmake.defines": {"optional_section": True},
    "autoconf.args": {"optional_section": True},
    "make.args": {"optional_section": True},
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
    "make.args",
    "download",
    "git",
    "install.files",
]


def parse_conditional_section_name(name, section_def):
    expr = name[len(section_def) + 1 :]
    return parse_expr(expr)


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
                config.readfp(fp)
        elif isinstance(fp, str):
            # For testing purposes, parse from a string
            config.readfp(io.StringIO(fp))
        else:
            config.readfp(fp)

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
        """ Intended for use with the make.args and autoconf.args
        sections, this method collects the entries and returns an
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

    def create_fetcher(self, build_options, ctx):
        raise KeyError(
            "project %s has no fetcher configuration matching %r" % (self.name, ctx)
        )
