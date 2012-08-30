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


#ifndef FOLLY_EXPERIMENTAL_EXCEPTION_TRACER_STACKTRACE_H_
#define FOLLY_EXPERIMENTAL_EXCEPTION_TRACER_STACKTRACE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StackTrace {
  uintptr_t* frameIPs;  /* allocated with malloc() */
  size_t frameCount;
} StackTrace;

/**
 * Get the current stack trace, allocating trace->frameIPs using malloc().
 * Skip the topmost "skip" frames.
 * Return 0 on success, a negative value on error.
 * On error, trace->frameIPs is NULL.
 */
int getCurrentStackTrace(size_t skip, StackTrace* trace);

/**
 * Free data allocated in a StackTrace object.
 */
void destroyStackTrace(StackTrace* trace);

/**
 * A stack of stack traces.
 */
typedef struct StackTraceStack {
  StackTrace trace;
  struct StackTraceStack* next;
} StackTraceStack;

/**
 * Push the current stack trace onto the stack.
 * Return 0 on success, a negative value on error.
 * On error, the stack is unchanged.
 */
int pushCurrentStackTrace(size_t skip, StackTraceStack** head);

/**
 * Pop (and destroy) the top stack trace from the stack.
 */
void popStackTrace(StackTraceStack** head);

/**
 * Completely empty the stack, destroying everything.
 */
void clearStack(StackTraceStack** head);

/**
 * Move the top stack trace from one stack to another.
 * Return 0 on success, a negative value on error (if the source stack is
 * empty)
 */
int moveTop(StackTraceStack** from, StackTraceStack** to);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* FOLLY_EXPERIMENTAL_EXCEPTION_TRACER_STACKTRACE_H_ */

