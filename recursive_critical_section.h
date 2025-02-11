/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef BASE_DEPRECATED_RECURSIVE_CRITICAL_SECTION_H_
#define BASE_DEPRECATED_RECURSIVE_CRITICAL_SECTION_H_

#include <atomic>

#include "build_macros.h"
#include "platform_thread_types.h"

#if defined(__OS_WIN__)
// clang-format off
// clang formating would change include order.

// Include winsock2.h before including <windows.h> to maintain consistency with
// win32.h. To include win32.h directly, it must be broken out into its own
// build target.
#include <winsock2.h>
#include <windows.h>
#include <sal.h>  // must come after windows headers.
// clang-format on
#endif  // defined(_WIN32)

#if defined(__OS_POSIX__)
#include <pthread.h>
#endif

// See notes in the 'Performance' unit test for the effects of this flag.
#define USE_NATIVE_MUTEX_ON_MAC 1

#if defined(__OS_MAC__) && !USE_NATIVE_MUTEX_ON_MAC
#include <dispatch/dispatch.h>
#endif

namespace base {

// NOTE: This class is deprecated. Please use webrtc::Mutex instead!
// Search using https://www.google.com/?q=recursive+lock+considered+harmful
// to find the reasons.
//
// Locking methods (Enter, TryEnter, Leave)are const to permit protecting
// members inside a const context without requiring mutable
// RecursiveCriticalSections everywhere. RecursiveCriticalSection is
// reentrant lock.
class RecursiveCriticalSection {
 public:
  RecursiveCriticalSection();
  ~RecursiveCriticalSection();

  void Enter() const;
  bool TryEnter() const;
  void Leave() const;

 private:
  // Use only for ABSL_DCHECKing.
  bool CurrentThreadIsOwner() const;

#if defined(__OS_WIN__)
  mutable CRITICAL_SECTION crit_;
#elif defined(__OS_POSIX__)
#if defined(__OS_MAC__) && !USE_NATIVE_MUTEX_ON_MAC
  // Number of times the lock has been locked + number of threads waiting.
  // TODO(tommi): We could use this number and subtract the recursion count
  // to find places where we have multiple threads contending on the same lock.
  mutable std::atomic<int> lock_queue_;
  // `recursion_` represents the recursion count + 1 for the thread that owns
  // the lock. Only modified by the thread that owns the lock.
  mutable int recursion_;
  // Used to signal a single waiting thread when the lock becomes available.
  mutable dispatch_semaphore_t semaphore_;
  // The thread that currently holds the lock. Required to handle recursion.
  mutable PlatformThreadRef owning_thread_;
#else
  mutable pthread_mutex_t mutex_;
#endif
  mutable PlatformThreadRef thread_;  // Only used by ABSL_DCHECKs.
  mutable int recursion_count_;       // Only used by ABSL_DCHECKs.
#else  // !defined(_WIN32) && !defined(__OS_POSIX__)
#error Unsupported platform.
#endif
};

// CritScope, for serializing execution through a scope.
class CritScope {
 public:
  explicit CritScope(const RecursiveCriticalSection* cs);
  ~CritScope();

  CritScope(const CritScope&) = delete;
  CritScope& operator=(const CritScope&) = delete;

 private:
  const RecursiveCriticalSection* const cs_;
};

}  // namespace base

#endif  // RTC_BASE_DEPRECATED_RECURSIVE_CRITICAL_SECTION_H_
