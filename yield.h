/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef BASE_SYNCHRONIZATION_YIELD_H_
#define BASE_SYNCHRONIZATION_YIELD_H_

namespace base {

// Request rescheduling of threads.
void YieldCurrentThread();

}  // namespace base

#endif  // BASE_SYNCHRONIZATION_YIELD_H_
