/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef BASE_PLATFORM_THREAD_TYPES_H_
#define BASE_PLATFORM_THREAD_TYPES_H_
#include "build_macros.h"
// clang-format off
// clang formating would change include order.
#if defined(__OS_WIN__)
// Include winsock2.h before including <windows.h> to maintain consistency with
// win32.h. To include win32.h directly, it must be broken out into its own
// build target.
#include <winsock2.h>
#include <windows.h>
#elif defined(__Fuchsia__)
#include <zircon/types.h>
#include <zircon/process.h>
#elif defined(__OS_POSIX__)
#include <pthread.h>
#include <unistd.h>
#if defined(__OS_MAC__)
#include <pthread_spis.h>
#endif
#endif
// clang-format on

namespace base {
#if defined(_WIN32)
typedef DWORD PlatformThreadId;
typedef DWORD PlatformThreadRef;
#elif defined(__Fuchsia__)
typedef zx_handle_t PlatformThreadId;
typedef zx_handle_t PlatformThreadRef;
#elif defined(__OS_POSIX__)
typedef pid_t PlatformThreadId;
typedef pthread_t PlatformThreadRef;
#endif

// Retrieve the ID of the current thread.
PlatformThreadId CurrentThreadId();

// Retrieves a reference to the current thread. On Windows, this is the same
// as CurrentThreadId. On other platforms it's the pthread_t returned by
// pthread_self().
PlatformThreadRef CurrentThreadRef();

// Compares two thread identifiers for equality.
bool IsThreadRefEqual(const PlatformThreadRef& a, const PlatformThreadRef& b);

// Sets the current thread name.
void SetCurrentThreadName(const char* name);

}  // namespace base

#endif  // RTC_BASE_PLATFORM_THREAD_TYPES_H_
