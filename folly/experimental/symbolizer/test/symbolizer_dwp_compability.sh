#!/bin/bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


BINARY="$1"
DWP="$2"
IS_DEV_MODE="$3"

# Only test in opt mode.
# In dev mode, the test still depends on the shared libraries except
# binary + dwp file.
if [ "$IS_DEV_MODE" == "True" ]; then
    exit 0
fi

NEW_BINARY=$(dirname "$DWP")
# Keep the binary file with the dwp file in the same directory.
cp "$BINARY" "$NEW_BINARY"
NEW_BINARY+="/$(basename "$BINARY")"

echo "$NEW_BINARY" "$DWP"

OUTPUT=$(tr -d '[:space:]' <<<$($NEW_BINARY))
echo "Unit test output is " "$OUTPUT"
[[ $OUTPUT =~ "[PASSED]16tests" ]] && exit 0 || exit 5
