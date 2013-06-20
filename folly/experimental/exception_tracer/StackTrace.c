/*
 * Copyright 2013 Facebook, Inc.
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
#include <stdio.h>
#include <stdlib.h>

#define UNW_LOCAL_ONLY 1

#include <libunwind.h>

struct Context {
  StackTrace* trace;
  size_t skip;
  size_t capacity;
};

static int checkError(const char* name, int err) {
  if (err < 0) {
    fprintf(stderr, "libunwind error: %s %d\n", name, err);
    return -EINVAL;
  }
  return 0;
}

static int addIP(struct Context* ctx, unw_cursor_t* cursor) {
  if (ctx->skip) {
    --ctx->skip;
    return 0;
  }

  unw_word_t ip;
  int r = unw_get_reg(cursor, UNW_REG_IP, &ip);
  int err = checkError("unw_get_reg", r);
  if (err) return err;

  if (ctx->trace->frameCount == ctx->capacity) {
    size_t newCapacity = (ctx->capacity < 8 ? 8 : ctx->capacity * 1.5);
    uintptr_t* newBlock =
      realloc(ctx->trace->frameIPs, newCapacity * sizeof(uintptr_t));
    if (!newBlock) {
      return -ENOMEM;
    }
    ctx->trace->frameIPs = newBlock;
    ctx->capacity = newCapacity;
  }

  ctx->trace->frameIPs[ctx->trace->frameCount++] = ip;
  return 0;  /* success */
}

int getCurrentStackTrace(size_t skip, StackTrace* trace) {
  trace->frameIPs = NULL;
  trace->frameCount = 0;

  struct Context ctx;
  ctx.trace = trace;
  ctx.skip = skip;
  ctx.capacity = 0;

  unw_context_t uctx;
  int r = unw_getcontext(&uctx);
  int err = checkError("unw_get_context", r);
  if (err) return err;

  unw_cursor_t cursor;
  r = unw_init_local(&cursor, &uctx);
  err = checkError("unw_init_local", r);
  if (err) return err;

  while ((r = unw_step(&cursor)) > 0) {
    if ((err = addIP(&ctx, &cursor)) != 0) {
      destroyStackTrace(trace);
      return err;
    }
  }
  err = checkError("unw_step", r);
  if (err) return err;

  return 0;
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

