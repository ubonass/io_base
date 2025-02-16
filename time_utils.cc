/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "time_utils.h"

#include <stdint.h>
#include <chrono>

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>  // Must come after windows headers.
#endif

namespace base {

int64_t SystemTimeNanos() {
  auto now = std::chrono::steady_clock::now();
  auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
      now.time_since_epoch());
  return nanoseconds.count();
}

int64_t CurrentTimeNanos() {
#if defined(_WIN32)
  static const UINT kPeriod = 1;
  bool high_res = (::timeBeginPeriod(kPeriod) == TIMERR_NOERROR);
  int64_t ret = SystemTimeNanos();
  if (high_res) {
    ::timeEndPeriod(kPeriod);
  }
  return ret;
#else
  return SystemTimeNanos();
#endif
}

int64_t CurrentTimeMicros() {
  return CurrentTimeNanos() / kNumNanosecsPerMicrosec;
}

int64_t SystemTimeMillis() {
  return static_cast<int64_t>(SystemTimeNanos() / kNumNanosecsPerMillisec);
}

int64_t TimeNanos() {
  return SystemTimeNanos();
}

uint32_t Time32() {
  return static_cast<uint32_t>(TimeNanos() / kNumNanosecsPerMillisec);
}

int64_t TimeMillis() {
  return TimeNanos() / kNumNanosecsPerMillisec;
}

int64_t TimeMicros() {
  return TimeNanos() / kNumNanosecsPerMicrosec;
}

int64_t TimeAfter(int64_t elapsed) {
  // ABSL_DCHECK_GE(elapsed, 0);
  return TimeMillis() + elapsed;
}

int32_t TimeDiff32(uint32_t later, uint32_t earlier) {
  return later - earlier;
}

int64_t TimeDiff(int64_t later, int64_t earlier) {
  return later - earlier;
}

int64_t TimeUTCMicros() {
  auto now = std::chrono::system_clock::now();
  // 2.  Unix Epoch（1970-01-01 00:00:00 UTC）
  auto duration_since_epoch = now.time_since_epoch();
  auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
      duration_since_epoch);
  return nanoseconds.count() / kNumNanosecsPerMicrosec;
}

int64_t TimeUTCMillis() {
  return TimeUTCMicros() / kNumMicrosecsPerMillisec;
}

}  // namespace base
