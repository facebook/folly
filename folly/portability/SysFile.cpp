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

#include <folly/portability/SysFile.h>

#ifdef _WIN32
#include <cerrno>
#include <cstdint>
#include <limits>

#include <folly/CPortability.h>
#include <folly/portability/Windows.h>

namespace {

// Maps a GetLastError() from LockFileEx()/UnlockFile() to the errno flock()
// should report.
constexpr int flockErrnoFromLockFileError(
    unsigned long error, bool nonBlocking) {
  switch (error) {
    case ERROR_ACCESS_DENIED:
    case ERROR_NETWORK_ACCESS_DENIED:
      return EACCES;
    case ERROR_INVALID_HANDLE:
    case ERROR_INVALID_TARGET_HANDLE:
    case ERROR_DIRECT_ACCESS_HANDLE:
      return EBADF;
    case ERROR_INVALID_PARAMETER:
      return EINVAL;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_NOT_ENOUGH_QUOTA:
      return ENOMEM;
    case ERROR_NO_SYSTEM_RESOURCES:
      return ENOLCK;
    case ERROR_LOCK_VIOLATION:
      // The code LockFileEx uses for lock contention. A non-blocking
      // request maps it to EWOULDBLOCK, mirroring POSIX flock(2).
      return nonBlocking ? EWOULDBLOCK : EIO;
    default:
      // Not lock contention: ERROR_SHARING_VIOLATION is an open-time
      // conflict, and ERROR_IO_PENDING is handled at the call site.
      return EIO;
  }
}

// Maps a GetLastError() from CreateEventW() to an errno. A resource
// failure, never a lock outcome, so it must not share
// flockErrnoFromLockFileError()'s EWOULDBLOCK.
constexpr int errnoFromCreateEventError(unsigned long error) {
  switch (error) {
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_NOT_ENOUGH_QUOTA:
      return ENOMEM;
    default:
      return EIO;
  }
}

int flockDoUnlock(HANDLE h) {
  constexpr DWORD kMaxDWORD = std::numeric_limits<DWORD>::max();
  if (UnlockFile(h, 0, 0, kMaxDWORD, kMaxDWORD)) {
    return 0;
  }
  const auto error = GetLastError();
  // POSIX flock(LOCK_UN) succeeds when already unlocked; this shim only
  // ever locks/unlocks the whole range, so there is no partial-range
  // failure this could hide.
  if (error == ERROR_NOT_LOCKED) {
    return 0;
  }
  errno = flockErrnoFromLockFileError(error, false);
  return -1;
}

int flockDoLock(HANDLE h, int operation) {
  const bool nonBlocking = (operation & LOCK_NB) != 0;
  const DWORD flags = DWORD(
      (nonBlocking ? LOCKFILE_FAIL_IMMEDIATELY : 0) |
      (operation & LOCK_EX ? LOCKFILE_EXCLUSIVE_LOCK : 0));
  constexpr DWORD kMaxDWORD = std::numeric_limits<DWORD>::max();

  // GetOverlappedResult(..., TRUE) below waits on ov.hEvent if set, else on
  // `h` itself, which could then wake this call for an unrelated
  // outstanding request on the same handle. Every lock request pays for an
  // event of its own to keep that wait precise -- including the added
  // possibility of CreateEventW failing from resource exhaustion, a
  // failure mode flock() did not have before.
  const HANDLE event = CreateEventW(
      /* lpEventAttributes */ nullptr,
      /* bManualReset */ TRUE,
      /* bInitialState */ FALSE,
      /* lpName */ nullptr);
  if (event == nullptr) {
    errno = errnoFromCreateEventError(GetLastError());
    return -1;
  }

  OVERLAPPED ov = {};
  ov.hEvent = event;
  if (LockFileEx(h, flags, 0, kMaxDWORD, kMaxDWORD, &ov)) {
    CloseHandle(event);
    return 0;
  }

  const auto error = GetLastError();
  if (error != ERROR_IO_PENDING) {
    CloseHandle(event);
    // A contended LOCK_NB request reports EWOULDBLOCK, so callers such as
    // folly::File::try_lock() return false rather than throw.
    errno = flockErrnoFromLockFileError(error, nonBlocking);
    return -1;
  }

  // LockFileEx queued the request rather than completing it synchronously:
  // expected for a blocking request on an overlapped handle, and also
  // possible for LOCK_NB on some filesystems (e.g. a network redirector)
  // where determining availability itself needs an I/O round trip. LOCK_NB
  // does not wait for a holder to release, but try_lock() is therefore not
  // wait-free here: it waits only for that round trip, which is bounded
  // but not instantaneous. Abandoning the request instead would let the
  // kernel grant the lock later and write into an OVERLAPPED whose stack
  // frame is by then gone.
  DWORD bytes = 0;
  const bool granted = GetOverlappedResult(h, &ov, &bytes, TRUE);
  const auto pendingError = granted ? 0UL : GetLastError();
  CloseHandle(event);
  if (granted) {
    return 0;
  }
  errno = flockErrnoFromLockFileError(pendingError, nonBlocking);
  return -1;
}

} // namespace

extern "C" int FOLLY_ATTR_WEAK_SYMBOLS_COMPILE_TIME
flock(int fd, int operation) {
  const auto rawHandle = _get_osfhandle(fd);
  constexpr intptr_t kInvalidOsfHandle = -1;
  // The UCRT returns -2 for a standard stream without an attached console.
  constexpr intptr_t kNoConsoleHandle = -2;
  if (rawHandle == kInvalidOsfHandle || rawHandle == kNoConsoleHandle) {
    errno = EBADF;
    return -1;
  }
  const auto h = reinterpret_cast<HANDLE>(rawHandle);

  // POSIX flock(2) requires exactly one of LOCK_SH, LOCK_EX, LOCK_UN, with
  // no other bit set besides LOCK_NB; naming none, combining them, or
  // setting an unrecognized bit is invalid. Masking off only LOCK_NB
  // (rather than masking in just the three recognized bits) is what makes
  // an unrecognized bit fall through to EINVAL, matching Linux's flock(),
  // which switches on cmd & ~LOCK_NB the same way. BSD/XNU instead reports
  // EBADF when neither LOCK_SH nor LOCK_EX is named, and tests bits
  // independently rather than for an exact match, so it silently accepts
  // both LOCK_SH | LOCK_EX and an unrecognized extra bit. This shim
  // deliberately follows Linux.
  switch (operation & ~LOCK_NB) {
    case LOCK_UN:
      return flockDoUnlock(h);
    case LOCK_SH:
    case LOCK_EX:
      return flockDoLock(h, operation);
    default:
      errno = EINVAL;
      return -1;
  }
}
#endif
