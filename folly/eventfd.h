/*
 * Copyright 2012 Facebook, Inc.
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

/**
 * Wrapper around the eventfd system call, as defined in <sys/eventfd.h>
 * in glibc 2.9+.
 *
 * @author Tudor Bosman (tudorb@fb.com)
 */

#ifndef FOLLY_BASE_EVENTFD_H_
#define FOLLY_BASE_EVENTFD_H_

#ifndef __linux__
#error This file may be compiled on Linux only.
#endif

#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>

// Use existing __NR_eventfd2 if already defined
// Values from the Linux kernel source:
// arch/x86/include/asm/unistd_{32,64}.h
#ifndef __NR_eventfd2
#if defined(__x86_64__)
#define __NR_eventfd2  290
#elif defined(__i386__)
#define __NR_eventfd2  328
#else
#error "Can't define __NR_eventfd2 for your architecture."
#endif
#endif

#ifndef EFD_SEMAPHORE
#define EFD_SEMAPHORE 1
#endif

/* from linux/fcntl.h - this conflicts with fcntl.h so include just the #define
 * we need
 */
#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000 /* set close_on_exec */
#endif

#ifndef EFD_CLOEXEC
#define EFD_CLOEXEC O_CLOEXEC
#endif

#ifndef EFD_NONBLOCK
#define EFD_NONBLOCK O_NONBLOCK
#endif

namespace folly {

// http://www.kernel.org/doc/man-pages/online/pages/man2/eventfd.2.html
inline int eventfd(unsigned int initval, int flags) {
  // Use the eventfd2 system call, as in glibc 2.9+
  // (requires kernel 2.6.30+)
  return syscall(__NR_eventfd2, initval, flags);
}

}  // namespace folly

#endif /* FOLLY_BASE_EVENTFD_H_ */

