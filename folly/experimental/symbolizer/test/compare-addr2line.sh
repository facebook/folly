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


FOLLY_ADDR2LINE="$1"
LLVM_ADDR2LINE="$2"
BINARY_TO_SYMBOLIZE="$3"

N=100
ADDRS=("$@")
ADDRS=("${ADDRS[@]:3}")

if [[ ${#ADDRS[@]} == 0 ]]; then
    SYMBOLS=$(readelf -s "$BINARY_TO_SYMBOLIZE")
    # An address may be part of multiple functions.
    # folly-addr2line and llvm-addr2line may choose either version leading to a comparison failure.
    # Only compare symbolization output if a single function matches the address.
    FUNCTION_ADDR_RANGES=$(echo "$SYMBOLS" | grep -E 'FUNC +GLOBAL +DEFAULT +[0-9]+' | awk '{print $2 " " $3}' | sort)
    UNIQUE_ADDR_RANGES=$(echo "$SYMBOLS" | awk '{print $2 " " $3}' | sort | uniq -u)
    FUNCTION_UNIQUE_ADDR_RANGES=$(comm -12 <(echo "$FUNCTION_ADDR_RANGES") <(echo "$UNIQUE_ADDR_RANGES") | shuf -n "$N")
    if [[ -z "$FUNCTION_UNIQUE_ADDR_RANGES" ]]; then
        exit 0
    fi
    readarray -t ADDRS < <(
        echo "$FUNCTION_UNIQUE_ADDR_RANGES" | shuf -n "$N" | while read -r start_hex size; do
            start="$((16#$start_hex))"
            end="$((start + size))"
            printf '0x%x\n' "$(seq "$start" "$end" | shuf -n 1)"
        done
    )
fi

echo ADDRS: "${ADDRS[@]}"

set -x
set -o pipefail

if command -v wdiff &> /dev/null
then
    DIFF=wdiff
else
    DIFF="diff -U 10"
fi

if command -v colordiff &> /dev/null
then
    COLORDIFF="colordiff"
else
    COLORDIFF="cat"
fi

$DIFF <("$FOLLY_ADDR2LINE" -e "$BINARY_TO_SYMBOLIZE" -i -f -a "${ADDRS[@]}") <("$LLVM_ADDR2LINE" -e "$BINARY_TO_SYMBOLIZE" -i -f -a "${ADDRS[@]}" | sed -E 's| \(discriminator [0-9]+\)||g') | "$COLORDIFF"

# Test that reading from stdin and args yields the same result.
$DIFF \
    <("$FOLLY_ADDR2LINE" -e "$BINARY_TO_SYMBOLIZE" -i -f -a "${ADDRS[@]}") \
    <(printf '%s\n' "${ADDRS[@]}" | "$FOLLY_ADDR2LINE" -e "$BINARY_TO_SYMBOLIZE" -i -f -a)
