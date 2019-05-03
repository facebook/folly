# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals


class ChangeStatus(object):
    """ Indicates the nature of changes that happened while updating
    the source directory.  There are two broad uses:
    * When extracting archives for third party software we want to
      know that we did something (eg: we either extracted code or
      we didn't do anything)
    * For 1st party code where we use shipit to transform the code,
      we want to know if we changed anything so that we can perform
      a build, but we generally want to be a little more nuanced
      and be able to distinguish between just changing a source file
      and whether we might need to reconfigure the build system.
    """

    def __init__(self, all_changed=False):
        """ Construct a ChangeStatus object.  The default is to create
        a status that indicates no changes, but passing all_changed=True
        will create one that indicates that everything changed """
        if all_changed:
            self.source_files = 1
            self.make_files = 1
        else:
            self.source_files = 0
            self.make_files = 0

    def record_change(self, file_name):
        """ Used by the shipit fetcher to record changes as it updates
        files in the destination.  If the file name might be one used
        in the cmake build system that we use for 1st party code, then
        record that as a "make file" change.  We could broaden this
        to match any file used by various build systems, but it is
        only really useful for our internal cmake stuff at this time.
        If the file isn't a build file and is under the `fbcode_builder`
        dir then we don't class that as an interesting change that we
        might need to rebuild, so we ignore it.
        Otherwise we record the file as a source file change. """

        if "cmake" in file_name.lower():
            self.make_files += 1
            return
        if "/fbcode_builder/" in file_name:
            return
        self.source_files += 1

    def sources_changed(self):
        """ Returns true if any source files were changed during
        an update operation.  This will typically be used to decide
        that the build system to be run on the source dir in an
        incremental mode """
        return self.source_files > 0

    def build_changed(self):
        """ Returns true if any build files were changed during
        an update operation.  This will typically be used to decidfe
        that the build system should be reconfigured and re-run
        as a full build """
        return self.make_files > 0


class Fetcher(object):
    """ The Fetcher is responsible for fetching and extracting the
    sources for project.  The Fetcher instance defines where the
    extracted data resides and reports this to the consumer via
    its `get_src_dir` method. """

    def update(self):
        """ Brings the src dir up to date, ideally minimizing
        changes so that a subsequent build doesn't over-build.
        Returns a ChangeStatus object that helps the caller to
        understand the nature of the changes required during
        the update. """
        return ChangeStatus()

    def clean(self):
        """ Reverts any changes that might have been made to
        the src dir """
        pass

    def hash(self):
        """ Returns a hash that identifies the version of the code in the
        working copy.  For a git repo this is commit hash for the working
        copy.  For other Fetchers this should relate to the version of
        the code in the src dir.  The intent is that if a manifest
        changes the version/rev of a project that the hash be different. """
        pass

    def get_src_dir(self):
        """ Returns the source directory that the project was
        extracted into """
        pass
