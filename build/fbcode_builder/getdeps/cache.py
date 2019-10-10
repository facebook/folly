# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals


class ArtifactCache(object):
    """ The ArtifactCache is a small abstraction that allows caching
    named things in some external storage mechanism.
    The primary use case is for storing the build products on CI
    systems to accelerate the build """

    def download_to_file(self, name, dest_file_name):
        """ If `name` exists in the cache, download it and place it
        in the specified `dest_file_name` location on the filesystem.
        If a transient issue was encountered a TransientFailure shall
        be raised.
        If `name` doesn't exist in the cache `False` shall be returned.
        If `dest_file_name` was successfully updated `True` shall be
        returned.
        All other conditions shall raise an appropriate exception. """
        return False

    def upload_from_file(self, name, source_file_name):
        """ Causes `name` to be populated in the cache by uploading
        the contents of `source_file_name` to the storage system.
        If a transient issue was encountered a TransientFailure shall
        be raised.
        If the upload failed for some other reason, an appropriate
        exception shall be raised. """
        pass


def create_cache():
    """ This function is monkey patchable to provide an actual
    implementation """
    return None
