﻿/*
 *  Copyright 2005 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef BASE_TIME_UTILS_H_
#define BASE_TIME_UTILS_H_

#include <stdint.h>
#include <time.h>

namespace base {

static const int64_t kNumMillisecsPerSec = INT64_C(1000);
static const int64_t kNumMicrosecsPerSec = INT64_C(1000000);
static const int64_t kNumNanosecsPerSec = INT64_C(1000000000);

static const int64_t kNumMicrosecsPerMillisec =
    kNumMicrosecsPerSec / kNumMillisecsPerSec;
static const int64_t kNumNanosecsPerMillisec =
    kNumNanosecsPerSec / kNumMillisecsPerSec;
static const int64_t kNumNanosecsPerMicrosec =
    kNumNanosecsPerSec / kNumMicrosecsPerSec;

// Elapsed milliseconds between NTP base, 1900 January 1 00:00 GMT
// (see https://tools.ietf.org/html/rfc868), and January 1 00:00 GMT 1970
// epoch. This is useful when converting between the NTP time base and the
// time base used in RTCP reports.
constexpr int64_t kNtpJan1970Millisecs = 2'208'988'800 * kNumMillisecsPerSec;

int64_t SystemTimeNanos();
// highe pricion..
int64_t CurrentTimeMicros();
int64_t CurrentTimeNanos();
// Returns the actual system time, even if a clock is set for testing.
// Useful for timeouts while using a test clock, or for logging.
int64_t SystemTimeMillis();

// Returns the current time in milliseconds in 32 bits.
uint32_t Time32();

// Returns the current time in milliseconds in 64 bits.
int64_t TimeMillis();
// Deprecated. Do not use this in any new code.
inline int64_t Time() {
  return TimeMillis();
}

// Returns the current time in microseconds.
int64_t TimeMicros();

// Returns the current time in nanoseconds.
int64_t TimeNanos();

// Returns a future timestamp, 'elapsed' milliseconds from now.
int64_t TimeAfter(int64_t elapsed);

// Number of milliseconds that would elapse between 'earlier' and 'later'
// timestamps.  The value is negative if 'later' occurs before 'earlier'.
int64_t TimeDiff(int64_t later, int64_t earlier);
int32_t TimeDiff32(uint32_t later, uint32_t earlier);

// The number of milliseconds that have elapsed since 'earlier'.
inline int64_t TimeSince(int64_t earlier) {
  return TimeMillis() - earlier;
}

// The number of milliseconds that will elapse between now and 'later'.
inline int64_t TimeUntil(int64_t later) {
  return later - TimeMillis();
}

// Return the number of microseconds since January 1, 1970, UTC.
// Useful mainly when producing logs to be correlated with other
// devices, and when the devices in question all have properly
// synchronized clocks.
//
// Note that this function obeys the system's idea about what the time
// is. It is not guaranteed to be monotonic; it will jump in case the
// system time is changed, e.g., by some other process calling
// settimeofday. Always use rtc::TimeMicros(), not this function, for
// measuring time intervals and timeouts.
int64_t TimeUTCMicros();

// Return the number of milliseconds since January 1, 1970, UTC.
// See above.
int64_t TimeUTCMillis();

}  // namespace base

#endif  // RTC_BASE_TIME_UTILS_H_
