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


#include "folly/experimental/exception_tracer/StackTrace.h"

#include <errno.h>
#include <stdlib.h>
#include "unwind.h"

struct Context {
  StackTrace* trace;
  size_t skip;
  size_t capacity;
};

static _Unwind_Reason_Code addIP(struct _Unwind_Context* ctx, void* varg) {
  struct Context* arg = (struct Context*)varg;

  if (arg->skip) {
    --arg->skip;
    return _URC_NO_REASON;
  }

  if (arg->trace->frameCount == arg->capacity) {
    size_t newCapacity = (arg->capacity < 8 ? 8 : arg->capacity * 1.5);
    uintptr_t* newBlock =
      realloc(arg->trace->frameIPs, newCapacity * sizeof(uintptr_t));
    if (!newBlock) {
      return _URC_FATAL_PHASE1_ERROR;
    }
    arg->trace->frameIPs = newBlock;
    arg->capacity = newCapacity;
  }

  arg->trace->frameIPs[arg->trace->frameCount++] = _Unwind_GetIP(ctx);
  return _URC_NO_REASON;  /* success */
}

int getCurrentStackTrace(size_t skip, StackTrace* trace) {
  trace->frameIPs = NULL;
  trace->frameCount = 0;
  struct Context ctx;
  ctx.trace = trace;
  ctx.skip = skip;
  ctx.capacity = 0;

  if (_Unwind_Backtrace(addIP, &ctx) == _URC_END_OF_STACK) {
    return 0;
  }

  destroyStackTrace(trace);
  return -ENOMEM;
}

void destroyStackTrace(StackTrace* trace) {
  free(trace->frameIPs);
  trace->frameIPs = NULL;
  trace->frameCount = 0;
}

int pushCurrentStackTrace(size_t skip, StackTraceStack** head) {
  StackTraceStack* newHead = malloc(sizeof(StackTraceStack));
  if (!newHead) {
    return -ENOMEM;
  }

  int err;
  if ((err = getCurrentStackTrace(skip, &newHead->trace)) != 0) {
    free(newHead);
    return -ENOMEM;
  }

  newHead->next = *head;
  *head = newHead;
  return 0;
}

void popStackTrace(StackTraceStack** head) {
  StackTraceStack* oldHead = *head;
  *head = oldHead->next;
  destroyStackTrace(&oldHead->trace);
  free(oldHead);
}

void clearStack(StackTraceStack** head) {
  while (*head) {
    popStackTrace(head);
  }
}

int moveTop(StackTraceStack** from, StackTraceStack** to) {
  StackTraceStack* top = *from;
  if (!top) {
    return -EINVAL;
  }

  *from = top->next;
  top->next = *to;
  *to = top;
  return 0;
}

