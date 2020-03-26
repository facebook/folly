# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import os
import shlex
import sys


class Env(object):
    def __init__(self, src=None):
        self._dict = {}
        if src is None:
            self.update(os.environ)
        else:
            self.update(src)

    def update(self, src):
        for k, v in src.items():
            self.set(k, v)

    def copy(self):
        return Env(self._dict)

    def _key(self, key):
        # The `str` cast may not appear to be needed, but without it we run
        # into issues when passing the environment to subprocess.  The main
        # issue is that in python2 `os.environ` (which is the initial source
        # of data for the environment) uses byte based strings, but this
        # project uses `unicode_literals`.  `subprocess` will raise an error
        # if the environment that it is passed has a mixture of byte and
        # unicode strings.
        # It is simplest to force everthing to be `str` for the sake of
        # consistency.
        key = str(key)
        if sys.platform.startswith("win"):
            # Windows env var names are case insensitive but case preserving.
            # An implementation of PAR files on windows gets confused if
            # the env block contains keys with conflicting case, so make a
            # pass over the contents to remove any.
            # While this O(n) scan is technically expensive and gross, it
            # is practically not a problem because the volume of calls is
            # relatively low and the cost of manipulating the env is dwarfed
            # by the cost of spawning a process on windows.  In addition,
            # since the processes that we run are expensive anyway, this
            # overhead is not the worst thing to worry about.
            for k in list(self._dict.keys()):
                if str(k).lower() == key.lower():
                    return k
        elif key in self._dict:
            return key
        return None

    def get(self, key, defval=None):
        key = self._key(key)
        if key is None:
            return defval
        return self._dict[key]

    def __getitem__(self, key):
        val = self.get(key)
        if key is None:
            raise KeyError(key)
        return val

    def unset(self, key):
        if key is None:
            raise KeyError("attempting to unset env[None]")

        key = self._key(key)
        if key:
            del self._dict[key]

    def __delitem__(self, key):
        self.unset(key)

    def __repr__(self):
        return repr(self._dict)

    def set(self, key, value):
        if key is None:
            raise KeyError("attempting to assign env[None] = %r" % value)

        if value is None:
            raise ValueError("attempting to assign env[%s] = None" % key)

        # The `str` conversion is important to avoid triggering errors
        # with subprocess if we pass in a unicode value; see commentary
        # in the `_key` method.
        key = str(key)
        value = str(value)

        # The `unset` call is necessary on windows where the keys are
        # case insensitive.   Since this dict is case sensitive, simply
        # assigning the value to the new key is not sufficient to remove
        # the old value.  The `unset` call knows how to match keys and
        # remove any potential duplicates.
        self.unset(key)
        self._dict[key] = value

    def __setitem__(self, key, value):
        self.set(key, value)

    def __iter__(self):
        return self._dict.__iter__()

    def __len__(self):
        return len(self._dict)

    def keys(self):
        return self._dict.keys()

    def values(self):
        return self._dict.values()

    def items(self):
        return self._dict.items()


def add_path_entry(env, name, item, append=True, separator=os.pathsep):
    """ Cause `item` to be added to the path style env var named
    `name` held in the `env` dict.  `append` specifies whether
    the item is added to the end (the default) or should be
    prepended if `name` already exists. """
    val = env.get(name, "")
    if len(val) > 0:
        val = val.split(separator)
    else:
        val = []
    if append:
        val.append(item)
    else:
        val.insert(0, item)
    env.set(name, separator.join(val))


def add_flag(env, name, flag, append=True):
    """ Cause `flag` to be added to the CXXFLAGS-style env var named
    `name` held in the `env` dict.  `append` specifies whether the
    flag is added to the end (the default) or should be prepended if
    `name` already exists. """
    val = shlex.split(env.get(name, ""))
    if append:
        val.append(flag)
    else:
        val.insert(0, flag)
    env.set(name, " ".join(val))


_path_search_cache = {}
_not_found = object()


def path_search(env, exename, defval=None):
    """ Search for exename in the PATH specified in env.
    exename is eg: `ninja` and this function knows to append a .exe
    to the end on windows.
    Returns the path to the exe if found, or None if either no
    PATH is set in env or no executable is found. """

    path = env.get("PATH", None)
    if path is None:
        return defval

    # The project hash computation code searches for C++ compilers (g++, clang, etc)
    # repeatedly.  Cache the result so we don't end up searching for these over and over
    # again.
    cache_key = (path, exename)
    result = _path_search_cache.get(cache_key, _not_found)
    if result is _not_found:
        result = _perform_path_search(path, exename)
        _path_search_cache[cache_key] = result
    return result


def _perform_path_search(path, exename):
    is_win = sys.platform.startswith("win")
    if is_win:
        exename = "%s.exe" % exename

    for bindir in path.split(os.pathsep):
        full_name = os.path.join(bindir, exename)
        if os.path.exists(full_name) and os.path.isfile(full_name):
            if not is_win and not os.access(full_name, os.X_OK):
                continue
            return full_name

    return None
