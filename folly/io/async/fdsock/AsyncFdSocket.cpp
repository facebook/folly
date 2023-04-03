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

#include "AsyncFdSocket.h"

namespace folly {

AsyncFdSocket::AsyncFdSocket(EventBase* evb)
    : AsyncSocket(evb)
#if !defined(_WIN32)
      ,
      sendMsgCob_(evb) {
  setUpCallbacks();
}
#else
{
}
#endif

AsyncFdSocket::AsyncFdSocket(
    EventBase* evb,
    const folly::SocketAddress& address,
    uint32_t connectTimeout)
    : AsyncFdSocket(evb) {
  connect(nullptr, address, connectTimeout);
}

AsyncFdSocket::AsyncFdSocket(
    EventBase* evb, NetworkSocket fd, const folly::SocketAddress* peerAddress)
    : AsyncSocket(evb, fd, /* zeroCopyBufId */ 0, peerAddress)
#if !defined(_WIN32)
      ,
      sendMsgCob_(evb) {
  setUpCallbacks();
}
#else
{
}
#endif

void AsyncFdSocket::writeChainWithFds(
    WriteCallback* callback,
    std::unique_ptr<folly::IOBuf> buf,
    SocketFds::ToSend fds,
    WriteFlags flags) {
#if defined(_WIN32)
  DCHECK(fds.empty()) << "AsyncFdSocket cannot send FDs on Windows";
#else
  DCHECK_EQ(&sendMsgCob_, getSendMsgParamsCB());

  if (buf->empty() && !fds.empty()) {
    DestructorGuard dg(this);
    AsyncSocketException ex(
        AsyncSocketException::BAD_ARGS,
        withAddr("Cannot send FDs without at least 1 data byte"));
    return failWrite(__func__, callback, 0, ex);
  }

  if (!sendMsgCob_.registerFdsForWriteTag(
          WriteRequestTag{buf.get()},
          std::move(fds) // discarded on error
          )) {
    // Careful: this has no unittest coverage because I don't have a good
    // idea for how to cause this in a meaningful way.  Should this be
    // a DCHECK? Plans that don't work:
    //  - Creating two `unique_ptr` from the same raw pointer would be
    //    bad news bears for the test.
    //  - IOBuf recycling via getReleaseIOBufCallback() shouldn't be
    //    possible either, since we unregister the tag in our `releaseIOBuf`
    //    override below before releasing the IOBuf.
    DestructorGuard dg(this);
    AsyncSocketException ex(
        AsyncSocketException::BAD_ARGS,
        withAddr("Buffer was already owned by this socket"));
    return failWrite(__func__, callback, 0, ex);
  }
#endif // !Windows

  writeChain(callback, std::move(buf), flags);
}

// The callbacks aren't defined on Windows -- the CMSG macros don't compile
#if !defined(_WIN32)

void AsyncFdSocket::setUpCallbacks() noexcept {
  AsyncSocket::setSendMsgParamCB(&sendMsgCob_);
}

void AsyncFdSocket::releaseIOBuf(
    std::unique_ptr<folly::IOBuf> buf, ReleaseIOBufCallback* callback) {
  sendMsgCob_.destroyFdsForWriteTag(WriteRequestTag{buf.get()});
  AsyncSocket::releaseIOBuf(std::move(buf), callback);
}

std::pair<
    size_t,
    AsyncFdSocket::FdSendMsgParamsCallback::WriteTagToFds::iterator>
AsyncFdSocket::FdSendMsgParamsCallback::getCmsgSizeAndFds(
    const AsyncSocket::WriteRequestTag& writeTag) noexcept {
  auto it = writeTagToFds_.find(writeTag);
  if (it == writeTagToFds_.end()) {
    return std::make_pair(0, it);
  }
  return std::make_pair(CMSG_SPACE(sizeof(int) * it->second.size()), it);
}

void AsyncFdSocket::FdSendMsgParamsCallback::getAncillaryData(
    folly::WriteFlags,
    void* data,
    const WriteRequestTag& writeTag,
    const bool /*byteEventsEnabled*/) noexcept {
  eventBase_->dcheckIsInEventBaseThread();

  auto [cmsgSpace, fdsIt] = getCmsgSizeAndFds(writeTag);
  CHECK_NE(0, cmsgSpace);
  const auto& fds = fdsIt->second;

  // NOT checking `fds.size() < SCM_MAX_FD` here because there's no way to
  // propagate the error to the caller, and ultimately the write will fail
  // out with EINVAL anyway.  The front-end that accepts FDs should check
  // this instead.  If there is a debuggability issue, do add a LOG(ERROR).

  ::msghdr msg{}; // Discarded, we just need to populate `data`

  msg.msg_control = data;
  msg.msg_controllen = cmsgSpace;
  struct ::cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  CHECK_NOTNULL(cmsg);

  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;

  auto* outFds = reinterpret_cast<int*>(CMSG_DATA(cmsg));
  for (size_t n = 0; n < fds.size(); ++n) {
    outFds[n] = fds[n]->fd();
  }

  VLOG(4) << "this=" << this << ", getAncillaryData() sending " << fds.size()
          << " FDs";
}

uint32_t AsyncFdSocket::FdSendMsgParamsCallback::getAncillaryDataSize(
    folly::WriteFlags,
    const WriteRequestTag& writeTag,
    const bool /*byteEventsEnabled*/) noexcept {
  // Not checking thread because `getAncillaryData` will follow.

  // Due to the unfortunate design of the callback API, this will run
  // again in `getAncillaryData`, let's pray for CPU caches here.
  auto [size, _] = getCmsgSizeAndFds(writeTag);
  return size;
}

void AsyncFdSocket::FdSendMsgParamsCallback::wroteBytes(
    const WriteRequestTag& writeTag) noexcept {
  auto nh = writeTagToFds_.extract(writeTag);
  if (nh.empty()) {
    // `AsyncSocket` will only call `wroteBytes` if `getAncillaryDataSize`
    // returned a nonzero value, so we'll never get here.
    LOG(DFATAL) << "wroteBytes without a matching `getAncillaryData`?";
  } else {
    VLOG(5) << "this=" << this << ", FdSendMsgParamsCallback::wroteBytes() on "
            << nh.mapped().size() << " FDs for tag " << writeTag;
  }
}

bool AsyncFdSocket::FdSendMsgParamsCallback::registerFdsForWriteTag(
    WriteRequestTag writeTag, SocketFds::ToSend&& fds) {
  VLOG(5) << "this=" << this << ", registerFdsForWriteTag() on " << fds.size()
          << " FDs for tag " << writeTag;
  if (writeTag.empty()) {
    return false; // no insertion, error: we require a nonempty tag
  }
  auto [it, inserted] = writeTagToFds_.try_emplace(writeTag, std::move(fds));
  return inserted;
}

void AsyncFdSocket::FdSendMsgParamsCallback::destroyFdsForWriteTag(
    WriteRequestTag writeTag) noexcept {
  auto nh = writeTagToFds_.extract(writeTag);
  if (nh.empty()) {
    return; // Not every write has FDs; also, `wroteBytes` clears them.
  }
  VLOG(5) << "this=" << this << ", destroyFdsForWriteTag() on "
          << nh.mapped().size() << " FDs for tag " << writeTag;
}

#endif // !Windows

} // namespace folly
