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

#include <folly/wangle/channel/Pipeline.h>

namespace folly { namespace wangle {

void PipelineBase::setWriteFlags(WriteFlags flags) {
  writeFlags_ = flags;
}

WriteFlags PipelineBase::getWriteFlags() {
  return writeFlags_;
}

void PipelineBase::setReadBufferSettings(
    uint64_t minAvailable,
    uint64_t allocationSize) {
  readBufferSettings_ = std::make_pair(minAvailable, allocationSize);
}

std::pair<uint64_t, uint64_t> PipelineBase::getReadBufferSettings() {
  return readBufferSettings_;
}

typename PipelineBase::ContextIterator PipelineBase::removeAt(
    const typename PipelineBase::ContextIterator& it) {
  (*it)->detachPipeline();

  const auto dir = (*it)->getDirection();
  if (dir == HandlerDir::BOTH || dir == HandlerDir::IN) {
    auto it2 = std::find(inCtxs_.begin(), inCtxs_.end(), it->get());
    CHECK(it2 != inCtxs_.end());
    inCtxs_.erase(it2);
  }

  if (dir == HandlerDir::BOTH || dir == HandlerDir::OUT) {
    auto it2 = std::find(outCtxs_.begin(), outCtxs_.end(), it->get());
    CHECK(it2 != outCtxs_.end());
    outCtxs_.erase(it2);
  }

  return ctxs_.erase(it);
}

PipelineBase& PipelineBase::removeFront() {
  if (ctxs_.empty()) {
    throw std::invalid_argument("No handlers in pipeline");
  }
  removeAt(ctxs_.begin());
  return *this;
}

PipelineBase& PipelineBase::removeBack() {
  if (ctxs_.empty()) {
    throw std::invalid_argument("No handlers in pipeline");
  }
  removeAt(--ctxs_.end());
  return *this;
}

void PipelineBase::detachHandlers() {
  for (auto& ctx : ctxs_) {
    if (ctx != owner_) {
      ctx->detachPipeline();
    }
  }
}

}} // folly::wangle
