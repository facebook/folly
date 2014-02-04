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

#ifndef FOLLY_LOGGING_H_
#define FOLLY_LOGGING_H_

#include <chrono>
#include <glog/logging.h>

#ifndef FB_LOG_EVERY_MS
/**
 * Issues a LOG(severity) no more often than every
 * milliseconds. Example:
 *
 * FB_LOG_EVERY_MS(INFO, 10000) << "At least ten seconds passed"
 *   " since you last saw this.";
 *
 * The implementation uses for statements to introduce variables in
 * a nice way that doesn't mess surrounding statements.
 */
#define FB_LOG_EVERY_MS(severity, milli_interval)                       \
  for (bool FB_LEM_once = true; FB_LEM_once; )                          \
    for (const auto FB_LEM_now = ::std::chrono::system_clock::now();    \
         FB_LEM_once; )                                                 \
      for (static ::std::chrono::system_clock::time_point FB_LEM_last;  \
            FB_LEM_once; FB_LEM_once = false)                           \
        if (FB_LEM_now - FB_LEM_last <                                  \
            ::std::chrono::milliseconds(milli_interval)) {              \
        } else                                                          \
          (FB_LEM_last = FB_LEM_now, LOG(severity))

#endif

#endif  // FOLLY_LOGGING_H_
