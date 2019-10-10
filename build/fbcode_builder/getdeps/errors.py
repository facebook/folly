# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals


class TransientFailure(Exception):
    """ Raising this error causes getdeps to return with an error code
    that Sandcastle will consider to be a retryable transient
    infrastructure error """

    pass


class ManifestNotFound(Exception):
    def __init__(self, manifest_name):
        super(Exception, self).__init__("Unable to find manifest '%s'" % manifest_name)
