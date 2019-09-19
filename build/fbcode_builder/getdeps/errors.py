# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals


class TransientFailure(Exception):
    """ Raising this error causes getdeps to return with an error code
    that Sandcastle will consider to be a retryable transient
    infrastructure error """

    pass


class ManifestNotFound(Exception):
    def __init__(self, manifest_name):
        super(Exception, self).__init__("Unable to find manifest '%s'" % manifest_name)
