/*
 * Copyright 2015 Facebook, Inc.
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

#pragma once

#include <folly/io/async/AsyncTransport.h>
#include <folly/futures/Future.h>
#include <folly/ExceptionWrapper.h>

namespace folly { namespace wangle {

class PipelineBase;

template <class In, class Out>
class HandlerContext {
 public:
  virtual ~HandlerContext() {}

  virtual void fireRead(In msg) = 0;
  virtual void fireReadEOF() = 0;
  virtual void fireReadException(exception_wrapper e) = 0;

  virtual Future<void> fireWrite(Out msg) = 0;
  virtual Future<void> fireClose() = 0;

  virtual PipelineBase* getPipeline() = 0;

  virtual std::shared_ptr<AsyncTransport> getTransport() = 0;

  virtual void setWriteFlags(WriteFlags flags) = 0;
  virtual WriteFlags getWriteFlags() = 0;

  virtual void setReadBufferSettings(
      uint64_t minAvailable,
      uint64_t allocationSize) = 0;
  virtual std::pair<uint64_t, uint64_t> getReadBufferSettings() = 0;

  /* TODO
  template <class H>
  virtual void addHandlerBefore(H&&) {}
  template <class H>
  virtual void addHandlerAfter(H&&) {}
  template <class H>
  virtual void replaceHandler(H&&) {}
  virtual void removeHandler() {}
  */
};

template <class In>
class InboundHandlerContext {
 public:
  virtual ~InboundHandlerContext() {}

  virtual void fireRead(In msg) = 0;
  virtual void fireReadEOF() = 0;
  virtual void fireReadException(exception_wrapper e) = 0;

  virtual PipelineBase* getPipeline() = 0;

  virtual std::shared_ptr<AsyncTransport> getTransport() = 0;

  // TODO Need get/set writeFlags, readBufferSettings? Probably not.
  // Do we even really need them stored in the pipeline at all?
  // Could just always delegate to the socket impl
};

template <class Out>
class OutboundHandlerContext {
 public:
  virtual ~OutboundHandlerContext() {}

  virtual Future<void> fireWrite(Out msg) = 0;
  virtual Future<void> fireClose() = 0;

  virtual PipelineBase* getPipeline() = 0;

  virtual std::shared_ptr<AsyncTransport> getTransport() = 0;
};

enum class HandlerDir {
  IN,
  OUT,
  BOTH
};

}} // folly::wangle

#include <folly/wangle/channel/HandlerContext-inl.h>
