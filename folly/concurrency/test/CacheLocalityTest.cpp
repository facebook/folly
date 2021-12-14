/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/concurrency/CacheLocality.h>

#include <memory>
#include <thread>
#include <unordered_map>

#include <folly/portability/GTest.h>
#include <folly/portability/SysResource.h>

#include <glog/logging.h>

using namespace folly;

/// This is the relevant nodes from a production box's sysfs tree.  If you
/// think this map is ugly you should see the version of this test that
/// used a real directory tree.  To reduce the chance of testing error
/// I haven't tried to remove the common prefix
static std::unordered_map<std::string, std::string> fakeSysfsTree = {
    {"/sys/devices/system/cpu/cpu0/cache/index0/shared_cpu_list", "0,17"},
    {"/sys/devices/system/cpu/cpu0/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu0/cache/index1/shared_cpu_list", "0,17"},
    {"/sys/devices/system/cpu/cpu0/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu0/cache/index2/shared_cpu_list", "0,17"},
    {"/sys/devices/system/cpu/cpu0/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu0/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu0/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu1/cache/index0/shared_cpu_list", "1,18"},
    {"/sys/devices/system/cpu/cpu1/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu1/cache/index1/shared_cpu_list", "1,18"},
    {"/sys/devices/system/cpu/cpu1/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu1/cache/index2/shared_cpu_list", "1,18"},
    {"/sys/devices/system/cpu/cpu1/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu1/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu1/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu2/cache/index0/shared_cpu_list", "2,19"},
    {"/sys/devices/system/cpu/cpu2/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu2/cache/index1/shared_cpu_list", "2,19"},
    {"/sys/devices/system/cpu/cpu2/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu2/cache/index2/shared_cpu_list", "2,19"},
    {"/sys/devices/system/cpu/cpu2/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu2/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu2/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu3/cache/index0/shared_cpu_list", "3,20"},
    {"/sys/devices/system/cpu/cpu3/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu3/cache/index1/shared_cpu_list", "3,20"},
    {"/sys/devices/system/cpu/cpu3/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu3/cache/index2/shared_cpu_list", "3,20"},
    {"/sys/devices/system/cpu/cpu3/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu3/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu3/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu4/cache/index0/shared_cpu_list", "4,21"},
    {"/sys/devices/system/cpu/cpu4/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu4/cache/index1/shared_cpu_list", "4,21"},
    {"/sys/devices/system/cpu/cpu4/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu4/cache/index2/shared_cpu_list", "4,21"},
    {"/sys/devices/system/cpu/cpu4/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu4/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu4/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu5/cache/index0/shared_cpu_list", "5-6"},
    {"/sys/devices/system/cpu/cpu5/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu5/cache/index1/shared_cpu_list", "5-6"},
    {"/sys/devices/system/cpu/cpu5/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu5/cache/index2/shared_cpu_list", "5-6"},
    {"/sys/devices/system/cpu/cpu5/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu5/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu5/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu6/cache/index0/shared_cpu_list", "5-6"},
    {"/sys/devices/system/cpu/cpu6/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu6/cache/index1/shared_cpu_list", "5-6"},
    {"/sys/devices/system/cpu/cpu6/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu6/cache/index2/shared_cpu_list", "5-6"},
    {"/sys/devices/system/cpu/cpu6/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu6/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu6/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu7/cache/index0/shared_cpu_list", "7,22"},
    {"/sys/devices/system/cpu/cpu7/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu7/cache/index1/shared_cpu_list", "7,22"},
    {"/sys/devices/system/cpu/cpu7/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu7/cache/index2/shared_cpu_list", "7,22"},
    {"/sys/devices/system/cpu/cpu7/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu7/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu7/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu8/cache/index0/shared_cpu_list", "8,23"},
    {"/sys/devices/system/cpu/cpu8/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu8/cache/index1/shared_cpu_list", "8,23"},
    {"/sys/devices/system/cpu/cpu8/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu8/cache/index2/shared_cpu_list", "8,23"},
    {"/sys/devices/system/cpu/cpu8/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu8/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu8/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu9/cache/index0/shared_cpu_list", "9,24"},
    {"/sys/devices/system/cpu/cpu9/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu9/cache/index1/shared_cpu_list", "9,24"},
    {"/sys/devices/system/cpu/cpu9/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu9/cache/index2/shared_cpu_list", "9,24"},
    {"/sys/devices/system/cpu/cpu9/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu9/cache/index3/shared_cpu_list", "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu9/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu10/cache/index0/shared_cpu_list", "10,25"},
    {"/sys/devices/system/cpu/cpu10/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu10/cache/index1/shared_cpu_list", "10,25"},
    {"/sys/devices/system/cpu/cpu10/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu10/cache/index2/shared_cpu_list", "10,25"},
    {"/sys/devices/system/cpu/cpu10/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu10/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu10/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu11/cache/index0/shared_cpu_list", "11,26"},
    {"/sys/devices/system/cpu/cpu11/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu11/cache/index1/shared_cpu_list", "11,26"},
    {"/sys/devices/system/cpu/cpu11/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu11/cache/index2/shared_cpu_list", "11,26"},
    {"/sys/devices/system/cpu/cpu11/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu11/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu11/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu12/cache/index0/shared_cpu_list", "12,27"},
    {"/sys/devices/system/cpu/cpu12/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu12/cache/index1/shared_cpu_list", "12,27"},
    {"/sys/devices/system/cpu/cpu12/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu12/cache/index2/shared_cpu_list", "12,27"},
    {"/sys/devices/system/cpu/cpu12/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu12/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu12/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu13/cache/index0/shared_cpu_list", "13,28"},
    {"/sys/devices/system/cpu/cpu13/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu13/cache/index1/shared_cpu_list", "13,28"},
    {"/sys/devices/system/cpu/cpu13/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu13/cache/index2/shared_cpu_list", "13,28"},
    {"/sys/devices/system/cpu/cpu13/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu13/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu13/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu14/cache/index0/shared_cpu_list", "14,29"},
    {"/sys/devices/system/cpu/cpu14/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu14/cache/index1/shared_cpu_list", "14,29"},
    {"/sys/devices/system/cpu/cpu14/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu14/cache/index2/shared_cpu_list", "14,29"},
    {"/sys/devices/system/cpu/cpu14/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu14/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu14/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu15/cache/index0/shared_cpu_list", "15,30"},
    {"/sys/devices/system/cpu/cpu15/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu15/cache/index1/shared_cpu_list", "15,30"},
    {"/sys/devices/system/cpu/cpu15/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu15/cache/index2/shared_cpu_list", "15,30"},
    {"/sys/devices/system/cpu/cpu15/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu15/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu15/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu16/cache/index0/shared_cpu_list", "16,31"},
    {"/sys/devices/system/cpu/cpu16/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu16/cache/index1/shared_cpu_list", "16,31"},
    {"/sys/devices/system/cpu/cpu16/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu16/cache/index2/shared_cpu_list", "16,31"},
    {"/sys/devices/system/cpu/cpu16/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu16/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu16/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu17/cache/index0/shared_cpu_list", "0,17"},
    {"/sys/devices/system/cpu/cpu17/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu17/cache/index1/shared_cpu_list", "0,17"},
    {"/sys/devices/system/cpu/cpu17/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu17/cache/index2/shared_cpu_list", "0,17"},
    {"/sys/devices/system/cpu/cpu17/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu17/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu17/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu18/cache/index0/shared_cpu_list", "1,18"},
    {"/sys/devices/system/cpu/cpu18/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu18/cache/index1/shared_cpu_list", "1,18"},
    {"/sys/devices/system/cpu/cpu18/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu18/cache/index2/shared_cpu_list", "1,18"},
    {"/sys/devices/system/cpu/cpu18/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu18/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu18/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu19/cache/index0/shared_cpu_list", "2,19"},
    {"/sys/devices/system/cpu/cpu19/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu19/cache/index1/shared_cpu_list", "2,19"},
    {"/sys/devices/system/cpu/cpu19/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu19/cache/index2/shared_cpu_list", "2,19"},
    {"/sys/devices/system/cpu/cpu19/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu19/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu19/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu20/cache/index0/shared_cpu_list", "3,20"},
    {"/sys/devices/system/cpu/cpu20/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu20/cache/index1/shared_cpu_list", "3,20"},
    {"/sys/devices/system/cpu/cpu20/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu20/cache/index2/shared_cpu_list", "3,20"},
    {"/sys/devices/system/cpu/cpu20/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu20/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu20/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu21/cache/index0/shared_cpu_list", "4,21"},
    {"/sys/devices/system/cpu/cpu21/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu21/cache/index1/shared_cpu_list", "4,21"},
    {"/sys/devices/system/cpu/cpu21/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu21/cache/index2/shared_cpu_list", "4,21"},
    {"/sys/devices/system/cpu/cpu21/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu21/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu21/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu22/cache/index0/shared_cpu_list", "7,22"},
    {"/sys/devices/system/cpu/cpu22/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu22/cache/index1/shared_cpu_list", "7,22"},
    {"/sys/devices/system/cpu/cpu22/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu22/cache/index2/shared_cpu_list", "7,22"},
    {"/sys/devices/system/cpu/cpu22/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu22/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu22/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu23/cache/index0/shared_cpu_list", "8,23"},
    {"/sys/devices/system/cpu/cpu23/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu23/cache/index1/shared_cpu_list", "8,23"},
    {"/sys/devices/system/cpu/cpu23/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu23/cache/index2/shared_cpu_list", "8,23"},
    {"/sys/devices/system/cpu/cpu23/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu23/cache/index3/shared_cpu_list", "0-8,17-23"},
    {"/sys/devices/system/cpu/cpu23/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu24/cache/index0/shared_cpu_list", "9,24"},
    {"/sys/devices/system/cpu/cpu24/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu24/cache/index1/shared_cpu_list", "9,24"},
    {"/sys/devices/system/cpu/cpu24/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu24/cache/index2/shared_cpu_list", "9,24"},
    {"/sys/devices/system/cpu/cpu24/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu24/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu24/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu25/cache/index0/shared_cpu_list", "10,25"},
    {"/sys/devices/system/cpu/cpu25/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu25/cache/index1/shared_cpu_list", "10,25"},
    {"/sys/devices/system/cpu/cpu25/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu25/cache/index2/shared_cpu_list", "10,25"},
    {"/sys/devices/system/cpu/cpu25/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu25/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu25/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu26/cache/index0/shared_cpu_list", "11,26"},
    {"/sys/devices/system/cpu/cpu26/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu26/cache/index1/shared_cpu_list", "11,26"},
    {"/sys/devices/system/cpu/cpu26/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu26/cache/index2/shared_cpu_list", "11,26"},
    {"/sys/devices/system/cpu/cpu26/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu26/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu26/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu27/cache/index0/shared_cpu_list", "12,27"},
    {"/sys/devices/system/cpu/cpu27/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu27/cache/index1/shared_cpu_list", "12,27"},
    {"/sys/devices/system/cpu/cpu27/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu27/cache/index2/shared_cpu_list", "12,27"},
    {"/sys/devices/system/cpu/cpu27/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu27/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu27/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu28/cache/index0/shared_cpu_list", "13,28"},
    {"/sys/devices/system/cpu/cpu28/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu28/cache/index1/shared_cpu_list", "13,28"},
    {"/sys/devices/system/cpu/cpu28/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu28/cache/index2/shared_cpu_list", "13,28"},
    {"/sys/devices/system/cpu/cpu28/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu28/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu28/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu29/cache/index0/shared_cpu_list", "14,29"},
    {"/sys/devices/system/cpu/cpu29/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu29/cache/index1/shared_cpu_list", "14,29"},
    {"/sys/devices/system/cpu/cpu29/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu29/cache/index2/shared_cpu_list", "14,29"},
    {"/sys/devices/system/cpu/cpu29/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu29/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu29/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu30/cache/index0/shared_cpu_list", "15,30"},
    {"/sys/devices/system/cpu/cpu30/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu30/cache/index1/shared_cpu_list", "15,30"},
    {"/sys/devices/system/cpu/cpu30/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu30/cache/index2/shared_cpu_list", "15,30"},
    {"/sys/devices/system/cpu/cpu30/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu30/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu30/cache/index3/type", "Unified"},
    {"/sys/devices/system/cpu/cpu31/cache/index0/shared_cpu_list", "16,31"},
    {"/sys/devices/system/cpu/cpu31/cache/index0/type", "Data"},
    {"/sys/devices/system/cpu/cpu31/cache/index1/shared_cpu_list", "16,31"},
    {"/sys/devices/system/cpu/cpu31/cache/index1/type", "Instruction"},
    {"/sys/devices/system/cpu/cpu31/cache/index2/shared_cpu_list", "16,31"},
    {"/sys/devices/system/cpu/cpu31/cache/index2/type", "Unified"},
    {"/sys/devices/system/cpu/cpu31/cache/index3/shared_cpu_list",
     "9-16,24-31"},
    {"/sys/devices/system/cpu/cpu31/cache/index3/type", "Unified"}};

/// This is the expected CacheLocality structure for fakeSysfsTree
static const CacheLocality nonUniformExampleLocality = {
    32, {16, 16, 2}, {0,  2,  4,  6,  8,  10, 11, 12, 14, 16, 18,
                      20, 22, 24, 26, 28, 30, 1,  3,  5,  7,  9,
                      13, 15, 17, 19, 21, 23, 25, 27, 29, 31}};

TEST(CacheLocality, FakeSysfs) {
  auto parsed = CacheLocality::readFromSysfsTree([](std::string name) {
    auto iter = fakeSysfsTree.find(name);
    return iter == fakeSysfsTree.end() ? std::string() : iter->second;
  });

  auto& expected = nonUniformExampleLocality;
  EXPECT_EQ(expected.numCpus, parsed.numCpus);
  EXPECT_EQ(expected.numCachesByLevel, parsed.numCachesByLevel);
  EXPECT_EQ(expected.localityIndexByCpu, parsed.localityIndexByCpu);
}

static const std::vector<std::string> fakeProcCpuinfo = {
    "processor	: 0",
    "vendor_id	: GenuineIntel",
    "cpu family	: 6",
    "model		: 79",
    "model name	: Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GHz",
    "stepping	: 1",
    "microcode	: 0xb00001b",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "siblings	: 28",
    "core id		: 0",
    "cpu cores	: 14",
    "apicid		: 0",
    "initial apicid	: 0",
    "fpu		: yes",
    "fpu_exception	: yes",
    "cpuid level	: 20",
    "wp		: yes",
    "flags		: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp lm constant_tsc arch_perfmon pebs bts rep_good nopl xtopology nonstop_tsc cpuid aperfmperf pni pclmulqdq dtes64 monitor ds_cpl vmx smx est tm2 ssse3 sdbg fma cx16 xtpr pdcm pcid dca sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand lahf_lm abm 3dnowprefetch epb cat_l3 cdp_l3 intel_ppin intel_pt tpr_shadow vnmi flexpriority ept vpid fsgsbase tsc_adjust bmi1 hle avx2 smep bmi2 erms invpcid rtm cqm rdt_a rdseed adx smap xsaveopt cqm_llc cqm_occup_llc cqm_mbm_total cqm_mbm_local dtherm ida arat pln pts",
    "bugs		:",
    "bogomips	: 4788.90",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "address sizes	: 46 bits physical, 48 bits virtual",
    "power management:",
    "",
    "processor	: 1",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 1",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 2",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 2",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 3",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 3",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 4",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 4",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 5",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 5",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 6",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 6",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 7",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 8",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 8",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 9",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 9",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 10",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 10",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 11",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 11",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 12",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 12",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 13",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 13",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 14",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 14",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 0",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 15",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 1",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 16",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 2",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 17",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 3",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 18",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 4",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 19",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 5",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 20",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 6",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 21",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 8",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 22",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 9",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 23",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 10",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 24",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 11",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 25",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 12",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 26",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 13",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 27",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 14",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 28",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 0",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 29",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 1",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 30",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 2",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 31",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 3",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 32",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 4",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 33",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 5",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 34",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 6",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 35",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 8",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 36",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 9",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 37",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 10",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 38",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 11",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 39",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 12",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 40",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 13",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 41",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 0",
    "core id		: 14",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 42",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 0",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 43",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 1",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 44",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 2",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 45",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 3",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 46",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 4",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 47",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 5",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 48",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 6",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 49",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 8",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 50",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 9",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 51",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 10",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 52",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 11",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 53",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 12",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 54",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 13",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
    "processor	: 55",
    "cpu family	: 6",
    "cpu MHz		: 2401.000",
    "cache size	: 35840 KB",
    "physical id	: 1",
    "core id		: 14",
    "cpu cores	: 14",
    "cpuid level	: 20",
    "clflush size	: 64",
    "cache_alignment	: 64",
    "power management:",
};

/// This is the expected CacheLocality structure for fakeProcCpuinfo
static const CacheLocality fakeProcCpuinfoLocality = {
    56, {28, 28, 2}, {0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26,
                      28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54,
                      1,  3,  5,  7,  9,  11, 13, 15, 17, 19, 21, 23, 25, 27,
                      29, 31, 33, 35, 37, 39, 41, 43, 45, 47, 49, 51, 53, 55}};

TEST(CacheLocality, ProcCpu) {
  auto parsed = CacheLocality::readFromProcCpuinfoLines(fakeProcCpuinfo);
  auto& expected = fakeProcCpuinfoLocality;
  EXPECT_EQ(expected.numCpus, parsed.numCpus);
  EXPECT_EQ(expected.numCachesByLevel, parsed.numCachesByLevel);
  EXPECT_EQ(expected.localityIndexByCpu, parsed.localityIndexByCpu);
}

TEST(CacheLocality, LinuxActual) {
  if (!kIsLinux) {
    return;
  }

  auto parsed1 = CacheLocality::readFromProcCpuinfo();
  EXPECT_EQ(parsed1.numCpus, std::thread::hardware_concurrency());

  auto parsed2 = CacheLocality::readFromSysfs();
  EXPECT_EQ(parsed2.numCpus, std::thread::hardware_concurrency());

  EXPECT_EQ(parsed1.localityIndexByCpu, parsed2.localityIndexByCpu);
}

TEST(CacheLocality, LogSystem) {
  auto& sys = CacheLocality::system<>();
  LOG(INFO) << "numCpus= " << sys.numCpus;
  LOG(INFO) << "numCachesByLevel= ";
  for (std::size_t i = 0; i < sys.numCachesByLevel.size(); ++i) {
    LOG(INFO) << "  [" << i << "]= " << sys.numCachesByLevel[i];
  }
  LOG(INFO) << "localityIndexByCpu= ";
  for (std::size_t i = 0; i < sys.localityIndexByCpu.size(); ++i) {
    LOG(INFO) << "  [" << i << "]= " << sys.localityIndexByCpu[i];
  }
}

#ifdef RUSAGE_THREAD
static uint64_t micros(struct timeval& tv) {
  return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

template <typename F>
static void logRusageFor(std::string name, F func) {
  struct rusage before;
  getrusage(RUSAGE_THREAD, &before);
  auto beforeNow = std::chrono::steady_clock::now();
  func();
  auto afterNow = std::chrono::steady_clock::now();
  struct rusage after;
  getrusage(RUSAGE_THREAD, &after);
  LOG(INFO) << name << ": real: "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   afterNow - beforeNow)
                   .count()
            << " usec, user: "
            << (micros(after.ru_utime) - micros(before.ru_utime))
            << " usec, sys: "
            << (micros(after.ru_stime) - micros(before.ru_stime)) << " usec";
}

TEST(CacheLocality, BenchmarkProcCpuinfo) {
  logRusageFor("readFromProcCpuinfo", CacheLocality::readFromProcCpuinfo);
}

TEST(CacheLocality, BenchmarkSysfs) {
  logRusageFor("readFromSysfs", CacheLocality::readFromSysfs);
}
#endif

#if defined(FOLLY_HAVE_LINUX_VDSO) && !defined(FOLLY_SANITIZE_MEMORY)
TEST(Getcpu, VdsoGetcpu) {
  unsigned cpu;
  Getcpu::resolveVdsoFunc()(&cpu, nullptr, nullptr);

  EXPECT_TRUE(cpu < CPU_SETSIZE);
}
#endif

TEST(ThreadId, SimpleTls) {
  unsigned cpu = 0;
  auto rv =
      folly::FallbackGetcpu<SequentialThreadId>::getcpu(&cpu, nullptr, nullptr);
  EXPECT_EQ(rv, 0);
  EXPECT_TRUE(cpu > 0);
  unsigned again;
  folly::FallbackGetcpu<SequentialThreadId>::getcpu(&again, nullptr, nullptr);
  EXPECT_EQ(cpu, again);
}

TEST(ThreadId, SimplePthread) {
  unsigned cpu = 0;
  auto rv =
      folly::FallbackGetcpu<HashingThreadId>::getcpu(&cpu, nullptr, nullptr);
  EXPECT_EQ(rv, 0);
  EXPECT_TRUE(cpu > 0);
  unsigned again;
  folly::FallbackGetcpu<HashingThreadId>::getcpu(&again, nullptr, nullptr);
  EXPECT_EQ(cpu, again);
}

static thread_local unsigned testingCpu = 0;

static int testingGetcpu(unsigned* cpu, unsigned* node, void* /* unused */) {
  if (cpu != nullptr) {
    *cpu = testingCpu;
  }
  if (node != nullptr) {
    *node = testingCpu;
  }
  return 0;
}

TEST(AccessSpreader, Simple) {
  for (size_t s = 1; s < 200; ++s) {
    EXPECT_LT(AccessSpreader<>::current(s), s);
  }
}

TEST(AccessSpreader, SimpleCached) {
  for (size_t s = 1; s < 200; ++s) {
    EXPECT_LT(AccessSpreader<>::cachedCurrent(s), s);
  }
}

TEST(AccessSpreader, ConcurrentAccessCached) {
  std::vector<std::thread> threads;
  for (size_t i = 0; i < 4; ++i) {
    threads.emplace_back([]() {
      for (size_t s : {16, 32, 64}) {
        for (size_t j = 1; j < 200; ++j) {
          EXPECT_LT(AccessSpreader<>::cachedCurrent(s), s);
          EXPECT_LT(AccessSpreader<>::cachedCurrent(s), s);
        }
        std::this_thread::yield();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

#define DECLARE_SPREADER_TAG(tag, locality, func)      \
  namespace {                                          \
  template <typename dummy>                            \
  struct tag {};                                       \
  }                                                    \
  namespace folly {                                    \
  template <>                                          \
  const CacheLocality& CacheLocality::system<tag>() {  \
    static auto* inst = new CacheLocality(locality);   \
    return *inst;                                      \
  }                                                    \
  template <>                                          \
  Getcpu::Func AccessSpreader<tag>::pickGetcpuFunc() { \
    return func;                                       \
  }                                                    \
  template struct AccessSpreader<tag>;                 \
  }

DECLARE_SPREADER_TAG(ManualTag, CacheLocality::uniform(16), testingGetcpu)

TEST(AccessSpreader, Wrapping) {
  // this test won't pass unless locality.numCpus divides kMaxCpus
  auto numCpus = CacheLocality::system<ManualTag>().numCpus;
  EXPECT_EQ(0, 128 % numCpus);
  for (size_t s = 1; s < 200; ++s) {
    for (size_t c = 0; c < 400; ++c) {
      testingCpu = c;
      auto observed = AccessSpreader<ManualTag>::current(s);
      testingCpu = c % numCpus;
      auto expected = AccessSpreader<ManualTag>::current(s);
      EXPECT_EQ(expected, observed)
          << "numCpus=" << numCpus << ", s=" << s << ", c=" << c;
      EXPECT_LE(observed, AccessSpreader<ManualTag>::maxStripeValue());
    }
  }
}

TEST(CoreRawAllocator, Basic) {
  CoreRawAllocator<32> alloc;
  auto& a = alloc.get(0);
  auto res = a.allocate(8);
  memset(res, 0, 8);
  a.deallocate(res);
  res = a.allocate(8);
  EXPECT_TRUE((intptr_t)res % 8 == 0); // check alignment
  memset(res, 0, 8);
  a.deallocate(res);
  res = a.allocate(12);
  EXPECT_TRUE((intptr_t)res % 16 == 0); // check alignment
  memset(res, 0, 12);
  a.deallocate(res);
  res = a.allocate(257);
  memset(res, 0, 257);
  a.deallocate(res);

  std::vector<void*> mems;
  for (int i = 0; i < 10000; i++) {
    mems.push_back(a.allocate(1));
  }
  for (auto& mem : mems) {
    a.deallocate(mem);
  }
  mems.clear();
}
