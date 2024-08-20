# Copyright 2018 The Bazel Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Skylib module containing common hash-set algorithms.

  An empty set can be created using: `sets.make()`, or it can be created with some starting values
  if you pass it an sequence: `sets.make([1, 2, 3])`. This returns a struct containing all of the
  values as keys in a dictionary - this means that all passed in values must be hashable.  The
  values in the set can be retrieved using `sets.to_list(my_set)`.
"""

load(":dicts.bzl", "dicts")

def _make(elements = None):
    """Creates a new set.

    All elements must be hashable.

    Args:
      elements: Optional sequence to construct the set out of.

    Returns:
      A set containing the passed in values.
    """
    elements = elements if elements else []
    return struct(_values = {e: None for e in elements})

def _copy(s):
    """Creates a new set from another set.

    Args:
      s: A set, as returned by `sets.make()`.

    Returns:
      A new set containing the same elements as `s`.
    """
    return struct(_values = dict(s._values))

def _to_list(s):
    """Creates a list from the values in the set.

    Args:
      s: A set, as returned by `sets.make()`.

    Returns:
      A list of values inserted into the set.
    """
    return list(s._values.keys())

def _insert(s, e):
    """Inserts an element into the set.

    Element must be hashable.  This mutates the orginal set.

    Args:
      s: A set, as returned by `sets.make()`.
      e: The element to be inserted.

    Returns:
       The set `s` with `e` included.
    """
    s._values[e] = None
    return s

def _remove(s, e):
    """Removes an element from the set.

    Element must be hashable.  This mutates the orginal set.

    Args:
      s: A set, as returned by `sets.make()`.
      e: The element to be removed.

    Returns:
       The set `s` with `e` removed.
    """
    s._values.pop(e)
    return s

def _contains(a, e):
    """Checks for the existence of an element in a set.

    Args:
      a: A set, as returned by `sets.make()`.
      e: The element to look for.

    Returns:
      True if the element exists in the set, False if the element does not.
    """
    return e in a._values

def _get_shorter_and_longer(a, b):
    """Returns two sets in the order of shortest and longest.

    Args:
      a: A set, as returned by `sets.make()`.
      b: A set, as returned by `sets.make()`.

    Returns:
      `a`, `b` if `a` is shorter than `b` - or `b`, `a` if `b` is shorter than `a`.
    """
    if _length(a) < _length(b):
        return a, b
    return b, a

def _is_equal(a, b):
    """Returns whether two sets are equal.

    Args:
      a: A set, as returned by `sets.make()`.
      b: A set, as returned by `sets.make()`.

    Returns:
      True if `a` is equal to `b`, False otherwise.
    """
    return a._values == b._values

def _is_subset(a, b):
    """Returns whether `a` is a subset of `b`.

    Args:
      a: A set, as returned by `sets.make()`.
      b: A set, as returned by `sets.make()`.

    Returns:
      True if `a` is a subset of `b`, False otherwise.
    """
    for e in a._values.keys():
        if e not in b._values:
            return False
    return True

def _disjoint(a, b):
    """Returns whether two sets are disjoint.

    Two sets are disjoint if they have no elements in common.

    Args:
      a: A set, as returned by `sets.make()`.
      b: A set, as returned by `sets.make()`.

    Returns:
      True if `a` and `b` are disjoint, False otherwise.
    """
    shorter, longer = _get_shorter_and_longer(a, b)
    for e in shorter._values.keys():
        if e in longer._values:
            return False
    return True

def _intersection(a, b):
    """Returns the intersection of two sets.

    Args:
      a: A set, as returned by `sets.make()`.
      b: A set, as returned by `sets.make()`.

    Returns:
      A set containing the elements that are in both `a` and `b`.
    """
    shorter, longer = _get_shorter_and_longer(a, b)
    return struct(_values = {e: None for e in shorter._values.keys() if e in longer._values})

def _union(*args):
    """Returns the union of several sets.

    Args:
      *args: An arbitrary number of sets or lists.

    Returns:
      The set union of all sets or lists in `*args`.
    """
    return struct(_values = dicts.add(*[s._values for s in args]))

def _difference(a, b):
    """Returns the elements in `a` that are not in `b`.

    Args:
      a: A set, as returned by `sets.make()`.
      b: A set, as returned by `sets.make()`.

    Returns:
      A set containing the elements that are in `a` but not in `b`.
    """
    return struct(_values = {e: None for e in a._values.keys() if e not in b._values})

def _length(s):
    """Returns the number of elements in a set.

    Args:
      s: A set, as returned by `sets.make()`.

    Returns:
      An integer representing the number of elements in the set.
    """
    return len(s._values)

def _repr(s):
    """Returns a string value representing the set.

    Args:
      s: A set, as returned by `sets.make()`.

    Returns:
      A string representing the set.
    """
    return repr(s._values.keys())

sets = struct(
    make = _make,
    copy = _copy,
    to_list = _to_list,
    insert = _insert,
    contains = _contains,
    is_equal = _is_equal,
    is_subset = _is_subset,
    disjoint = _disjoint,
    intersection = _intersection,
    union = _union,
    difference = _difference,
    length = _length,
    remove = _remove,
    repr = _repr,
    str = _repr,
)
