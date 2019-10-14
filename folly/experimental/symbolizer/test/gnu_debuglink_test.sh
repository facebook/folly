#!/bin/bash
# Copyright (c) Facebook, Inc. and its affiliates.
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

crash="$1"
rm -f "$crash."{debuginfo,strip}
objcopy --only-keep-debug "$crash" "$crash.debuginfo"
objcopy --strip-debug --add-gnu-debuglink="$crash.debuginfo" "$crash" "$crash.strip"

echo '{"op":"start","test":"gnu_debuglink_test"}';
start=$(date +%s)
if "$crash.strip" 2>&1 | grep 'Crash.cpp:[0-9]*$' > /dev/null; then
    result='"status":"passed"';
else
    result='"status":"failed"';
fi
end=$(date +%s)
echo '{"op":"test_done","test":"gnu_debuglink_test",'"$result"'}'
echo '{"op":"all_done","results":[{"name":"gnu_debuglink_test",'"$result"',"start_time":'"$start"',"end_time":'"$end"',"details":"nothing"}]}'
