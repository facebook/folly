/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "folly/detail/CacheLocality.h"

#include <sched.h>
#include <memory>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include "folly/Benchmark.h"

using namespace folly::detail;

/// This is the relevant nodes from a production box's sysfs tree.  If you
/// think this map is ugly you should see the version of this test that
/// used a real directory tree.  To reduce the chance of testing error
/// I haven't tried to remove the common prefix
static std::unordered_map<std::string,std::string> fakeSysfsTree = {
  { "/sys/devices/system/cpu/cpu0/cache/index0/shared_cpu_list", "0,17" },
  { "/sys/devices/system/cpu/cpu0/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu0/cache/index1/shared_cpu_list", "0,17" },
  { "/sys/devices/system/cpu/cpu0/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu0/cache/index2/shared_cpu_list", "0,17" },
  { "/sys/devices/system/cpu/cpu0/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu0/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu0/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu1/cache/index0/shared_cpu_list", "1,18" },
  { "/sys/devices/system/cpu/cpu1/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu1/cache/index1/shared_cpu_list", "1,18" },
  { "/sys/devices/system/cpu/cpu1/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu1/cache/index2/shared_cpu_list", "1,18" },
  { "/sys/devices/system/cpu/cpu1/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu1/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu1/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu2/cache/index0/shared_cpu_list", "2,19" },
  { "/sys/devices/system/cpu/cpu2/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu2/cache/index1/shared_cpu_list", "2,19" },
  { "/sys/devices/system/cpu/cpu2/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu2/cache/index2/shared_cpu_list", "2,19" },
  { "/sys/devices/system/cpu/cpu2/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu2/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu2/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu3/cache/index0/shared_cpu_list", "3,20" },
  { "/sys/devices/system/cpu/cpu3/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu3/cache/index1/shared_cpu_list", "3,20" },
  { "/sys/devices/system/cpu/cpu3/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu3/cache/index2/shared_cpu_list", "3,20" },
  { "/sys/devices/system/cpu/cpu3/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu3/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu3/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu4/cache/index0/shared_cpu_list", "4,21" },
  { "/sys/devices/system/cpu/cpu4/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu4/cache/index1/shared_cpu_list", "4,21" },
  { "/sys/devices/system/cpu/cpu4/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu4/cache/index2/shared_cpu_list", "4,21" },
  { "/sys/devices/system/cpu/cpu4/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu4/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu4/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu5/cache/index0/shared_cpu_list", "5-6" },
  { "/sys/devices/system/cpu/cpu5/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu5/cache/index1/shared_cpu_list", "5-6" },
  { "/sys/devices/system/cpu/cpu5/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu5/cache/index2/shared_cpu_list", "5-6" },
  { "/sys/devices/system/cpu/cpu5/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu5/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu5/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu6/cache/index0/shared_cpu_list", "5-6" },
  { "/sys/devices/system/cpu/cpu6/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu6/cache/index1/shared_cpu_list", "5-6" },
  { "/sys/devices/system/cpu/cpu6/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu6/cache/index2/shared_cpu_list", "5-6" },
  { "/sys/devices/system/cpu/cpu6/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu6/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu6/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu7/cache/index0/shared_cpu_list", "7,22" },
  { "/sys/devices/system/cpu/cpu7/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu7/cache/index1/shared_cpu_list", "7,22" },
  { "/sys/devices/system/cpu/cpu7/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu7/cache/index2/shared_cpu_list", "7,22" },
  { "/sys/devices/system/cpu/cpu7/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu7/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu7/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu8/cache/index0/shared_cpu_list", "8,23" },
  { "/sys/devices/system/cpu/cpu8/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu8/cache/index1/shared_cpu_list", "8,23" },
  { "/sys/devices/system/cpu/cpu8/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu8/cache/index2/shared_cpu_list", "8,23" },
  { "/sys/devices/system/cpu/cpu8/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu8/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu8/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu9/cache/index0/shared_cpu_list", "9,24" },
  { "/sys/devices/system/cpu/cpu9/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu9/cache/index1/shared_cpu_list", "9,24" },
  { "/sys/devices/system/cpu/cpu9/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu9/cache/index2/shared_cpu_list", "9,24" },
  { "/sys/devices/system/cpu/cpu9/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu9/cache/index3/shared_cpu_list", "9-16,24-31" },
  { "/sys/devices/system/cpu/cpu9/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu10/cache/index0/shared_cpu_list", "10,25" },
  { "/sys/devices/system/cpu/cpu10/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu10/cache/index1/shared_cpu_list", "10,25" },
  { "/sys/devices/system/cpu/cpu10/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu10/cache/index2/shared_cpu_list", "10,25" },
  { "/sys/devices/system/cpu/cpu10/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu10/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu10/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu11/cache/index0/shared_cpu_list", "11,26" },
  { "/sys/devices/system/cpu/cpu11/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu11/cache/index1/shared_cpu_list", "11,26" },
  { "/sys/devices/system/cpu/cpu11/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu11/cache/index2/shared_cpu_list", "11,26" },
  { "/sys/devices/system/cpu/cpu11/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu11/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu11/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu12/cache/index0/shared_cpu_list", "12,27" },
  { "/sys/devices/system/cpu/cpu12/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu12/cache/index1/shared_cpu_list", "12,27" },
  { "/sys/devices/system/cpu/cpu12/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu12/cache/index2/shared_cpu_list", "12,27" },
  { "/sys/devices/system/cpu/cpu12/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu12/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu12/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu13/cache/index0/shared_cpu_list", "13,28" },
  { "/sys/devices/system/cpu/cpu13/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu13/cache/index1/shared_cpu_list", "13,28" },
  { "/sys/devices/system/cpu/cpu13/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu13/cache/index2/shared_cpu_list", "13,28" },
  { "/sys/devices/system/cpu/cpu13/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu13/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu13/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu14/cache/index0/shared_cpu_list", "14,29" },
  { "/sys/devices/system/cpu/cpu14/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu14/cache/index1/shared_cpu_list", "14,29" },
  { "/sys/devices/system/cpu/cpu14/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu14/cache/index2/shared_cpu_list", "14,29" },
  { "/sys/devices/system/cpu/cpu14/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu14/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu14/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu15/cache/index0/shared_cpu_list", "15,30" },
  { "/sys/devices/system/cpu/cpu15/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu15/cache/index1/shared_cpu_list", "15,30" },
  { "/sys/devices/system/cpu/cpu15/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu15/cache/index2/shared_cpu_list", "15,30" },
  { "/sys/devices/system/cpu/cpu15/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu15/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu15/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu16/cache/index0/shared_cpu_list", "16,31" },
  { "/sys/devices/system/cpu/cpu16/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu16/cache/index1/shared_cpu_list", "16,31" },
  { "/sys/devices/system/cpu/cpu16/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu16/cache/index2/shared_cpu_list", "16,31" },
  { "/sys/devices/system/cpu/cpu16/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu16/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu16/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu17/cache/index0/shared_cpu_list", "0,17" },
  { "/sys/devices/system/cpu/cpu17/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu17/cache/index1/shared_cpu_list", "0,17" },
  { "/sys/devices/system/cpu/cpu17/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu17/cache/index2/shared_cpu_list", "0,17" },
  { "/sys/devices/system/cpu/cpu17/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu17/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu17/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu18/cache/index0/shared_cpu_list", "1,18" },
  { "/sys/devices/system/cpu/cpu18/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu18/cache/index1/shared_cpu_list", "1,18" },
  { "/sys/devices/system/cpu/cpu18/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu18/cache/index2/shared_cpu_list", "1,18" },
  { "/sys/devices/system/cpu/cpu18/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu18/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu18/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu19/cache/index0/shared_cpu_list", "2,19" },
  { "/sys/devices/system/cpu/cpu19/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu19/cache/index1/shared_cpu_list", "2,19" },
  { "/sys/devices/system/cpu/cpu19/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu19/cache/index2/shared_cpu_list", "2,19" },
  { "/sys/devices/system/cpu/cpu19/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu19/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu19/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu20/cache/index0/shared_cpu_list", "3,20" },
  { "/sys/devices/system/cpu/cpu20/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu20/cache/index1/shared_cpu_list", "3,20" },
  { "/sys/devices/system/cpu/cpu20/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu20/cache/index2/shared_cpu_list", "3,20" },
  { "/sys/devices/system/cpu/cpu20/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu20/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu20/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu21/cache/index0/shared_cpu_list", "4,21" },
  { "/sys/devices/system/cpu/cpu21/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu21/cache/index1/shared_cpu_list", "4,21" },
  { "/sys/devices/system/cpu/cpu21/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu21/cache/index2/shared_cpu_list", "4,21" },
  { "/sys/devices/system/cpu/cpu21/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu21/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu21/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu22/cache/index0/shared_cpu_list", "7,22" },
  { "/sys/devices/system/cpu/cpu22/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu22/cache/index1/shared_cpu_list", "7,22" },
  { "/sys/devices/system/cpu/cpu22/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu22/cache/index2/shared_cpu_list", "7,22" },
  { "/sys/devices/system/cpu/cpu22/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu22/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu22/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu23/cache/index0/shared_cpu_list", "8,23" },
  { "/sys/devices/system/cpu/cpu23/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu23/cache/index1/shared_cpu_list", "8,23" },
  { "/sys/devices/system/cpu/cpu23/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu23/cache/index2/shared_cpu_list", "8,23" },
  { "/sys/devices/system/cpu/cpu23/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu23/cache/index3/shared_cpu_list", "0-8,17-23" },
  { "/sys/devices/system/cpu/cpu23/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu24/cache/index0/shared_cpu_list", "9,24" },
  { "/sys/devices/system/cpu/cpu24/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu24/cache/index1/shared_cpu_list", "9,24" },
  { "/sys/devices/system/cpu/cpu24/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu24/cache/index2/shared_cpu_list", "9,24" },
  { "/sys/devices/system/cpu/cpu24/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu24/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu24/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu25/cache/index0/shared_cpu_list", "10,25" },
  { "/sys/devices/system/cpu/cpu25/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu25/cache/index1/shared_cpu_list", "10,25" },
  { "/sys/devices/system/cpu/cpu25/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu25/cache/index2/shared_cpu_list", "10,25" },
  { "/sys/devices/system/cpu/cpu25/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu25/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu25/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu26/cache/index0/shared_cpu_list", "11,26" },
  { "/sys/devices/system/cpu/cpu26/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu26/cache/index1/shared_cpu_list", "11,26" },
  { "/sys/devices/system/cpu/cpu26/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu26/cache/index2/shared_cpu_list", "11,26" },
  { "/sys/devices/system/cpu/cpu26/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu26/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu26/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu27/cache/index0/shared_cpu_list", "12,27" },
  { "/sys/devices/system/cpu/cpu27/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu27/cache/index1/shared_cpu_list", "12,27" },
  { "/sys/devices/system/cpu/cpu27/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu27/cache/index2/shared_cpu_list", "12,27" },
  { "/sys/devices/system/cpu/cpu27/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu27/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu27/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu28/cache/index0/shared_cpu_list", "13,28" },
  { "/sys/devices/system/cpu/cpu28/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu28/cache/index1/shared_cpu_list", "13,28" },
  { "/sys/devices/system/cpu/cpu28/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu28/cache/index2/shared_cpu_list", "13,28" },
  { "/sys/devices/system/cpu/cpu28/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu28/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu28/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu29/cache/index0/shared_cpu_list", "14,29" },
  { "/sys/devices/system/cpu/cpu29/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu29/cache/index1/shared_cpu_list", "14,29" },
  { "/sys/devices/system/cpu/cpu29/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu29/cache/index2/shared_cpu_list", "14,29" },
  { "/sys/devices/system/cpu/cpu29/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu29/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu29/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu30/cache/index0/shared_cpu_list", "15,30" },
  { "/sys/devices/system/cpu/cpu30/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu30/cache/index1/shared_cpu_list", "15,30" },
  { "/sys/devices/system/cpu/cpu30/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu30/cache/index2/shared_cpu_list", "15,30" },
  { "/sys/devices/system/cpu/cpu30/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu30/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu30/cache/index3/type", "Unified" },
  { "/sys/devices/system/cpu/cpu31/cache/index0/shared_cpu_list", "16,31" },
  { "/sys/devices/system/cpu/cpu31/cache/index0/type", "Data" },
  { "/sys/devices/system/cpu/cpu31/cache/index1/shared_cpu_list", "16,31" },
  { "/sys/devices/system/cpu/cpu31/cache/index1/type", "Instruction" },
  { "/sys/devices/system/cpu/cpu31/cache/index2/shared_cpu_list", "16,31" },
  { "/sys/devices/system/cpu/cpu31/cache/index2/type", "Unified" },
  { "/sys/devices/system/cpu/cpu31/cache/index3/shared_cpu_list", "9-16,24-31"},
  { "/sys/devices/system/cpu/cpu31/cache/index3/type", "Unified" }
};

/// This is the expected CacheLocality structure for fakeSysfsTree
static const CacheLocality nonUniformExampleLocality = {
  32,
  { 16, 16, 2 },
  { 0, 2, 4, 6, 8, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28,
    30, 1, 3, 5, 7, 9, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31 }
};

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

TEST(Getcpu, VdsoGetcpu) {
  unsigned cpu;
  Getcpu::vdsoFunc()(&cpu, nullptr, nullptr);

  EXPECT_TRUE(cpu < CPU_SETSIZE);
}

TEST(SequentialThreadId, Simple) {
  unsigned cpu = 0;
  auto rv = SequentialThreadId<std::atomic>::getcpu(&cpu, nullptr, nullptr);
  EXPECT_EQ(rv, 0);
  EXPECT_TRUE(cpu > 0);
  unsigned again;
  SequentialThreadId<std::atomic>::getcpu(&again, nullptr, nullptr);
  EXPECT_EQ(cpu, again);
}

static FOLLY_TLS unsigned testingCpu = 0;

static int testingGetcpu(unsigned* cpu, unsigned* node, void* unused) {
  if (cpu != nullptr) {
    *cpu = testingCpu;
  }
  if (node != nullptr) {
    *node = testingCpu;
  }
  return 0;
}

TEST(AccessSpreader, Stubbed) {
  std::vector<std::unique_ptr<AccessSpreader<>>> spreaders(100);
  for (size_t s = 1; s < spreaders.size(); ++s) {
    spreaders[s].reset(new AccessSpreader<>(
        s, nonUniformExampleLocality, &testingGetcpu));
  }
  std::vector<size_t> cpusInLocalityOrder = {
      0, 17, 1, 18, 2, 19, 3, 20, 4, 21, 5, 6, 7, 22, 8, 23, 9, 24, 10, 25,
      11, 26, 12, 27, 13, 28, 14, 29, 15, 30, 16, 31 };
  for (size_t i = 0; i < 32; ++i) {
    // extra i * 32 is to check wrapping behavior of impl
    testingCpu = cpusInLocalityOrder[i] + i * 64;
    for (size_t s = 1; s < spreaders.size(); ++s) {
      EXPECT_EQ((i * s) / 32, spreaders[s]->current())
          << "i=" << i << ", cpu=" << testingCpu << ", s=" << s;
    }
  }
}

TEST(AccessSpreader, Default) {
  AccessSpreader<> spreader(16);
  EXPECT_LT(spreader.current(), 16);
}

TEST(AccessSpreader, Shared) {
  for (size_t s = 1; s < 200; ++s) {
    EXPECT_LT(AccessSpreader<>::shared(s).current(), s);
  }
}

TEST(AccessSpreader, Statics) {
  LOG(INFO) << "stripeByCore.numStripes() = "
            << AccessSpreader<>::stripeByCore.numStripes();
  LOG(INFO) << "stripeByChip.numStripes() = "
            << AccessSpreader<>::stripeByChip.numStripes();
  for (size_t s = 1; s < 200; ++s) {
    EXPECT_LT(AccessSpreader<>::current(s), s);
  }
}

TEST(AccessSpreader, Wrapping) {
  // this test won't pass unless locality.numCpus divides kMaxCpus
  auto numCpus = 16;
  auto locality = CacheLocality::uniform(numCpus);
  for (size_t s = 1; s < 200; ++s) {
    AccessSpreader<> spreader(s, locality, &testingGetcpu);
    for (size_t c = 0; c < 400; ++c) {
      testingCpu = c;
      auto observed = spreader.current();
      testingCpu = c % numCpus;
      auto expected = spreader.current();
      EXPECT_EQ(expected, observed)
          << "numCpus=" << numCpus << ", s=" << s << ", c=" << c;
    }
  }
}

// Benchmarked at ~21 nanos on fbk35 (2.6) and fbk18 (3.2) kernels with
// a 2.2Ghz Xeon
// ============================================================================
// folly/test/CacheLocalityTest.cpp                relative  time/iter  iters/s
// ============================================================================
// LocalAccessSpreaderUse                                      20.77ns   48.16M
// SharedAccessSpreaderUse                                     21.95ns   45.55M
// AccessSpreaderConstruction                                 466.56ns    2.14M
// ============================================================================

BENCHMARK(LocalAccessSpreaderUse, iters) {
  folly::BenchmarkSuspender braces;
  AccessSpreader<> spreader(16);
  braces.dismiss();

  for (unsigned long i = 0; i < iters; ++i) {
    auto x = spreader.current();
    folly::doNotOptimizeAway(x);
  }
}

BENCHMARK(SharedAccessSpreaderUse, iters) {
  for (unsigned long i = 0; i < iters; ++i) {
    auto x = AccessSpreader<>::current(16);
    folly::doNotOptimizeAway(x);
  }
}

BENCHMARK(AccessSpreaderConstruction, iters) {
  std::aligned_storage<sizeof(AccessSpreader<>),
                       std::alignment_of<AccessSpreader<>>::value>::type raw;
  for (unsigned long i = 0; i < iters; ++i) {
    auto x = new (&raw) AccessSpreader<>(16);
    folly::doNotOptimizeAway(x);
    x->~AccessSpreader();
  }
}

enum class SpreaderType { GETCPU, SHARED, TLS_RR };

// Benchmark scores here reflect the time for 32 threads to perform an
// atomic increment on a dual-socket E5-2660 @ 2.2Ghz.  Surprisingly,
// if we don't separate the counters onto unique 128 byte stripes the
// 1_stripe and 2_stripe results are identical, even though the L3 is
// claimed to have 64 byte cache lines.
//
// _stub means there was no call to getcpu or the tls round-robin
// implementation, because for a single stripe the cpu doesn't matter.
// _getcpu refers to the vdso getcpu implementation with a locally
// constructed AccessSpreader.  _tls_rr refers to execution using
// SequentialThreadId, the fallback if the vdso getcpu isn't available.
// _shared refers to calling AccessSpreader<>::current(numStripes)
// inside the hot loop.
//
// At 16_stripe_0_work and 32_stripe_0_work there is only L1 traffic,
// so since the stripe selection is 21 nanos the atomic increments in
// the L1 is ~15 nanos.  At width 8_stripe_0_work the line is expected
// to ping-pong almost every operation, since the loops have the same
// duration.  Widths 4 and 2 have the same behavior, but each tour of the
// cache line is 4 and 8 cores long, respectively.  These all suggest a
// lower bound of 60 nanos for intra-chip handoff and increment between
// the L1s.
//
// With 455 nanos (1K cycles) of busywork per contended increment, the
// system can hide all of the latency of a tour of length 4, but not
// quite one of length 8.  I was a bit surprised at how much worse the
// non-striped version got.  It seems that the inter-chip traffic also
// interferes with the L1-only localWork.load().  When the local work is
// doubled to about 1 microsecond we see that the inter-chip contention
// is still very important, but subdivisions on the same chip don't matter.
//
// sudo nice -n -20
//   _bin/folly/test/cache_locality_test --benchmark --bm_min_iters=1000000
// ============================================================================
// folly/test/CacheLocalityTest.cpp                relative  time/iter  iters/s
// ============================================================================
// contentionAtWidth(1_stripe_0_work_stub)                      1.14us  873.64K
// contentionAtWidth(2_stripe_0_work_getcpu)                  495.58ns    2.02M
// contentionAtWidth(4_stripe_0_work_getcpu)                  232.99ns    4.29M
// contentionAtWidth(8_stripe_0_work_getcpu)                  101.16ns    9.88M
// contentionAtWidth(16_stripe_0_work_getcpu)                  41.93ns   23.85M
// contentionAtWidth(32_stripe_0_work_getcpu)                  42.04ns   23.79M
// contentionAtWidth(64_stripe_0_work_getcpu)                  41.94ns   23.84M
// contentionAtWidth(2_stripe_0_work_tls_rr)                    1.00us  997.41K
// contentionAtWidth(4_stripe_0_work_tls_rr)                  694.41ns    1.44M
// contentionAtWidth(8_stripe_0_work_tls_rr)                  590.27ns    1.69M
// contentionAtWidth(16_stripe_0_work_tls_rr)                 222.13ns    4.50M
// contentionAtWidth(32_stripe_0_work_tls_rr)                 169.49ns    5.90M
// contentionAtWidth(64_stripe_0_work_tls_rr)                 162.20ns    6.17M
// contentionAtWidth(2_stripe_0_work_shared)                  495.54ns    2.02M
// contentionAtWidth(4_stripe_0_work_shared)                  236.27ns    4.23M
// contentionAtWidth(8_stripe_0_work_shared)                  114.81ns    8.71M
// contentionAtWidth(16_stripe_0_work_shared)                  44.65ns   22.40M
// contentionAtWidth(32_stripe_0_work_shared)                  41.76ns   23.94M
// contentionAtWidth(64_stripe_0_work_shared)                  43.47ns   23.00M
// atomicIncrBaseline(local_incr_0_work)                       20.39ns   49.06M
// ----------------------------------------------------------------------------
// contentionAtWidth(1_stripe_500_work_stub)                    2.04us  491.13K
// contentionAtWidth(2_stripe_500_work_getcpu)                610.98ns    1.64M
// contentionAtWidth(4_stripe_500_work_getcpu)                507.72ns    1.97M
// contentionAtWidth(8_stripe_500_work_getcpu)                542.53ns    1.84M
// contentionAtWidth(16_stripe_500_work_getcpu)               496.55ns    2.01M
// contentionAtWidth(32_stripe_500_work_getcpu)               500.67ns    2.00M
// atomicIncrBaseline(local_incr_500_work)                    484.69ns    2.06M
// ----------------------------------------------------------------------------
// contentionAtWidth(1_stripe_1000_work_stub)                   2.11us  473.78K
// contentionAtWidth(2_stripe_1000_work_getcpu)               970.64ns    1.03M
// contentionAtWidth(4_stripe_1000_work_getcpu)               987.31ns    1.01M
// contentionAtWidth(8_stripe_1000_work_getcpu)                 1.01us  985.52K
// contentionAtWidth(16_stripe_1000_work_getcpu)              986.09ns    1.01M
// contentionAtWidth(32_stripe_1000_work_getcpu)              960.23ns    1.04M
// atomicIncrBaseline(local_incr_1000_work)                   950.63ns    1.05M
// ============================================================================
static void contentionAtWidth(size_t iters, size_t stripes, size_t work,
                              SpreaderType spreaderType,
                              size_t counterAlignment = 128,
                              size_t numThreads = 32) {
  folly::BenchmarkSuspender braces;

  AccessSpreader<> spreader(
      stripes,
      CacheLocality::system<std::atomic>(),
      spreaderType == SpreaderType::TLS_RR
          ? SequentialThreadId<std::atomic>::getcpu : nullptr);

  std::atomic<size_t> ready(0);
  std::atomic<bool> go(false);

  // while in theory the cache line size is 64 bytes, experiments show
  // that we get contention on 128 byte boundaries for Ivy Bridge.  The
  // extra indirection adds 1 or 2 nanos
  assert(counterAlignment >= sizeof(std::atomic<size_t>));
  std::vector<char> raw(counterAlignment * stripes);

  // if we happen to be using the tlsRoundRobin, then sequentially
  // assigning the thread identifiers is the unlikely best-case scenario.
  // We don't want to unfairly benefit or penalize.  Computing the exact
  // maximum likelihood of the probability distributions is annoying, so
  // I approximate as 2/5 of the ids that have no threads, 2/5 that have
  // 1, 2/15 that have 2, and 1/15 that have 3.  We accomplish this by
  // wrapping back to slot 0 when we hit 1/15 and 1/5.

  std::vector<std::thread> threads;
  while (threads.size() < numThreads) {
    threads.push_back(std::thread([&,iters,stripes,work]() {
      std::atomic<size_t>* counters[stripes];
      for (size_t i = 0; i < stripes; ++i) {
        counters[i]
          = new (raw.data() + counterAlignment * i) std::atomic<size_t>();
      }

      spreader.current();
      ready++;
      while (!go.load()) {
        sched_yield();
      }
      std::atomic<int> localWork;
      if (spreaderType == SpreaderType::SHARED) {
        for (size_t i = iters; i > 0; --i) {
          ++*(counters[AccessSpreader<>::current(stripes)]);
          for (size_t j = work; j > 0; --j) {
            localWork.load();
          }
        }
      } else {
        for (size_t i = iters; i > 0; --i) {
          ++*(counters[spreader.current()]);
          for (size_t j = work; j > 0; --j) {
            localWork.load();
          }
        }
      }
    }));

    if (threads.size() == numThreads / 15 ||
        threads.size() == numThreads / 5) {
      // create a few dummy threads to wrap back around to 0 mod numCpus
      for (size_t i = threads.size(); i != numThreads; ++i) {
        std::thread([&]() {
          spreader.current();
        }).join();
      }
    }
  }

  while (ready < numThreads) {
    sched_yield();
  }
  braces.dismiss();
  go = true;

  for (auto& thr : threads) {
    thr.join();
  }
}

static void atomicIncrBaseline(size_t iters, size_t work,
                               size_t numThreads = 32) {
  folly::BenchmarkSuspender braces;

  std::atomic<bool> go(false);

  std::vector<std::thread> threads;
  while (threads.size() < numThreads) {
    threads.push_back(std::thread([&]() {
      while (!go.load()) {
        sched_yield();
      }
      std::atomic<size_t> localCounter;
      std::atomic<int> localWork;
      for (size_t i = iters; i > 0; --i) {
        localCounter++;
        for (size_t j = work; j > 0; --j) {
          localWork.load();
        }
      }
    }));
  }

  braces.dismiss();
  go = true;

  for (auto& thr : threads) {
    thr.join();
  }
}

BENCHMARK_DRAW_LINE()

BENCHMARK_NAMED_PARAM(contentionAtWidth, 1_stripe_0_work_stub,
                      1, 0, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 2_stripe_0_work_getcpu,
                      2, 0, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 4_stripe_0_work_getcpu,
                      4, 0, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 8_stripe_0_work_getcpu,
                      8, 0, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 16_stripe_0_work_getcpu,
                      16, 0, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 32_stripe_0_work_getcpu,
                      32, 0, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 64_stripe_0_work_getcpu,
                      64, 0, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 2_stripe_0_work_tls_rr,
                      2, 0, SpreaderType::TLS_RR)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 4_stripe_0_work_tls_rr,
                      4, 0, SpreaderType::TLS_RR)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 8_stripe_0_work_tls_rr,
                      8, 0, SpreaderType::TLS_RR)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 16_stripe_0_work_tls_rr,
                      16, 0, SpreaderType::TLS_RR)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 32_stripe_0_work_tls_rr,
                      32, 0, SpreaderType::TLS_RR)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 64_stripe_0_work_tls_rr,
                      64, 0, SpreaderType::TLS_RR)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 2_stripe_0_work_shared,
                      2, 0, SpreaderType::SHARED)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 4_stripe_0_work_shared,
                      4, 0, SpreaderType::SHARED)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 8_stripe_0_work_shared,
                      8, 0, SpreaderType::SHARED)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 16_stripe_0_work_shared,
                      16, 0, SpreaderType::SHARED)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 32_stripe_0_work_shared,
                      32, 0, SpreaderType::SHARED)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 64_stripe_0_work_shared,
                      64, 0, SpreaderType::SHARED)
BENCHMARK_NAMED_PARAM(atomicIncrBaseline, local_incr_0_work, 0)
BENCHMARK_DRAW_LINE()
BENCHMARK_NAMED_PARAM(contentionAtWidth, 1_stripe_500_work_stub,
                      1, 500, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 2_stripe_500_work_getcpu,
                      2, 500, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 4_stripe_500_work_getcpu,
                      4, 500, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 8_stripe_500_work_getcpu,
                      8, 500, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 16_stripe_500_work_getcpu,
                      16, 500, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 32_stripe_500_work_getcpu,
                      32, 500, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(atomicIncrBaseline, local_incr_500_work, 500)
BENCHMARK_DRAW_LINE()
BENCHMARK_NAMED_PARAM(contentionAtWidth, 1_stripe_1000_work_stub,
                      1, 1000, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 2_stripe_1000_work_getcpu,
                      2, 1000, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 4_stripe_1000_work_getcpu,
                      4, 1000, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 8_stripe_1000_work_getcpu,
                      8, 1000, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 16_stripe_1000_work_getcpu,
                      16, 1000, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(contentionAtWidth, 32_stripe_1000_work_getcpu,
                      32, 1000, SpreaderType::GETCPU)
BENCHMARK_NAMED_PARAM(atomicIncrBaseline, local_incr_1000_work, 1000)


int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  auto ret = RUN_ALL_TESTS();
  if (!ret && FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return ret;
}
