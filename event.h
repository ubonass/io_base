/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef IO_BASE_EVENT_H_
#define IO_BASE_EVENT_H_

#include "absl/time/time.h"
#include "build_macros.h"

#if defined(__OS_WIN__)
#include <windows.h>
#elif defined(__OS_POSIX__)
#include <pthread.h>
#else
#error "Must define either __OS_WIN__ or __OS_POSIX__"
#endif

#include "yield_policy.h"

namespace base {

#if defined(__OS_ANDROID__)
void WarnThatTheCurrentThreadIsProbablyDeadlocked();
#else
inline void WarnThatTheCurrentThreadIsProbablyDeadlocked() {}
#endif

class Event {
 public:
  // TODO(bugs.webrtc.org/14366): Consider removing this redundant alias.
  static constexpr absl::Duration kForever = absl::InfiniteDuration();
  static constexpr absl::Duration kDefaultWarnDuration = absl::Seconds(3);

  Event();
  Event(bool manual_reset, bool initially_signaled);
  Event(const Event&) = delete;
  Event& operator=(const Event&) = delete;
  ~Event();

  void Set();
  void Reset();

  // Waits for the event to become signaled, but logs a warning if it takes more
  // than `warn_after`, and gives up completely if it takes more than
  // `give_up_after`. (If `warn_after >= give_up_after`, no warning will be
  // logged.) Either or both may be `kForever`, which means wait indefinitely.
  //
  // Care is taken so that the underlying OS wait call isn't requested to sleep
  // shorter than `give_up_after`.
  //
  // Returns true if the event was signaled, false if there was a timeout or
  // some other error.
  bool Wait(absl::Duration give_up_after, absl::Duration warn_after);

  // Waits with the given timeout and a reasonable default warning timeout.
  bool Wait(absl::Duration give_up_after) {
    return Wait(give_up_after,
                absl::time_internal::IsInfiniteDuration(give_up_after)
                    ? kDefaultWarnDuration
                    : kForever);
  }

 private:
#if defined(__OS_WIN__)
  HANDLE event_handle_;
#elif defined(__OS_POSIX__)
  pthread_mutex_t event_mutex_;
  pthread_cond_t event_cond_;
  const bool is_manual_reset_;
  bool event_status_;
#endif
};

// These classes are provided for compatibility with Chromium.
// The rtc::Event implementation is overriden inside of Chromium for the
// purposes of detecting when threads are blocked that shouldn't be as well as
// to use the more accurate event implementation that's there than is provided
// by default on some platforms (e.g. Windows).
// When building with standalone WebRTC, this class is a noop.
// For further information, please see the
// ScopedAllowBaseSyncPrimitives(ForTesting) classes in Chromium.
class ScopedAllowBaseSyncPrimitives {
 public:
  ScopedAllowBaseSyncPrimitives() {}
  ~ScopedAllowBaseSyncPrimitives() {}
};

class ScopedAllowBaseSyncPrimitivesForTesting {
 public:
  ScopedAllowBaseSyncPrimitivesForTesting() {}
  ~ScopedAllowBaseSyncPrimitivesForTesting() {}
};

}  // namespace base

#endif  // RTC_BASE_EVENT_H_
