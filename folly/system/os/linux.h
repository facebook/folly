/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#pragma once

struct open_how;

namespace folly {

/// linux_syscall_openat2
///
/// Invokes the openat2 syscall. Returns the file descriptor on success, or
/// returns -1 and sets errno on failure.
long linux_syscall_openat2(
    int dirfd, char const* pathname, struct open_how const* how);

} // namespace folly
