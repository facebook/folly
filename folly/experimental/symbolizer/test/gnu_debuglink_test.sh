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

crash="$1"

uuid=$(cat /proc/sys/kernel/random/uuid)
for sig in 1 2 3 13 15; do eval "trap 'exit $((sig + 128))' $sig"; done
trap 'rm -f "$crash.debuginfo.$uuid" "$crash.strip.$uuid"' 0

${OBJCOPY} --only-keep-debug "$crash" "$crash.debuginfo.$uuid"
${OBJCOPY} --strip-debug --add-gnu-debuglink="$crash.debuginfo.$uuid" "$crash" "$crash.strip.$uuid"

echo '{"op":"start","test":"gnu_debuglink_test"}';
start=$(date +%s)
if "$crash.strip.$uuid" 2>&1 | grep -q 'Crash.cpp:[0-9]*$'; then
    result='"status":"passed"';
else
    result='"status":"failed"';
fi
end=$(date +%s)
echo '{"op":"test_done","test":"gnu_debuglink_test",'"$result"'}'
echo '{"op":"all_done","results":[{"name":"gnu_debuglink_test",'"$result"',"start_time":'"$start"',"end_time":'"$end"',"details":"nothing"}]}'
