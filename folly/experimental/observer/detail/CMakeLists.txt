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

folly_add_library(
  NAME graph_cycle_detector
  HEADERS
    GraphCycleDetector.h
  EXPORTED_DEPS
    folly_observer_detail_graph_cycle_detector
)

folly_add_library(
  NAME observer_manager
  HEADERS
    Core.h
    ObserverManager.h
  EXPORTED_DEPS
    folly_observer_detail_observer_manager
)
