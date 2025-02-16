// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_EVENT_LOOP_WIN_H_
#define BASE_EVENT_LOOP_WIN_H_

#include "event_loop.h"

#include <mmsystem.h>  // Must come after windows headers.
#include <sal.h>       // Must come after windows headers.
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <utility>

#include "absl/container/btree_map.h"
#include "absl/types/span.h"
#include "linked_hash_map.h"

namespace base {

class DelayedTaskInfo {
 public:
  // Default ctor needed to support priority_queue::pop().
  DelayedTaskInfo();
  DelayedTaskInfo(absl::Duration delay, absl::AnyInvocable<void() &&> task);
  DelayedTaskInfo(DelayedTaskInfo&&) = default;

  // Implement for priority_queue.
  bool operator>(const DelayedTaskInfo& other) const;

  // Required by priority_queue::pop().
  DelayedTaskInfo& operator=(DelayedTaskInfo&& other) = default;

  // See below for why this method is const.
  void Run() const;

  int64_t due_time_micros() const;

 private:
  int64_t due_time_micros_ = 0;

  // `task` needs to be mutable because std::priority_queue::top() returns
  // a const reference and a key in an ordered queue must not be changed.
  // There are two basic workarounds, one using const_cast, which would also
  // make the key (`due_time`), non-const and the other is to make the non-key
  // (`task`), mutable.
  // Because of this, the `task` variable is made private and can only be
  // mutated by calling the `Run()` method.
  mutable absl::AnyInvocable<void() &&> task_;
};

class MultimediaTimer {
 public:
  // Note: We create an event that requires manual reset.
  MultimediaTimer();

  ~MultimediaTimer();

  MultimediaTimer(const MultimediaTimer&) = delete;
  MultimediaTimer& operator=(const MultimediaTimer&) = delete;

  bool StartOneShotTimer(UINT delay_ms);

  void Cancel();

  WSAEVENT* event_for_wait();

 private:
  WSAEVENT event_ = WSA_INVALID_EVENT;
  MMRESULT timer_id_ = 0;
};

class EventLoopWin : public EventLoop {
 public:
  EventLoopWin();
  ~EventLoopWin() override;

  void WakeUp() override;

  // EventLoop implementation.
  ABSL_MUST_USE_RESULT bool RegisterSocket(SocketFd fd,
                                           EventMask events,
                                           EventListener* listener) override;

  ABSL_MUST_USE_RESULT bool UnregisterSocket(SocketFd fd) override;

  ABSL_MUST_USE_RESULT bool RearmSocket(SocketFd fd, EventMask events) override;

  ABSL_MUST_USE_RESULT bool ArtificiallyNotifyEvent(SocketFd fd,
                                                    EventMask events) override;

  void RunEventLoopOnce(absl::Duration default_timeout) override;

  // EventLoop implementation.
 public:
  void PostDelayedTaskOnTaskQueue(absl::AnyInvocable<void() &&> task,
                                  int64_t start_micros,
                                  absl::Duration delay) override;

  void RunDueTasks();
  void ScheduleNextTimer();
  void CancelTimers();
  void ProcessPendingTasks();
  void ProcessTimerTasksUpTo(int64_t current_time_micros);

 private:
  struct Registration {
    EventMask events = 0;
    EventListener* listener;
    EventMask artificially_notify_at_next_iteration = 0;
  };

  // Used for deferred execution of I/O callbacks.
  struct ReadyListEntry {
    SocketFd fd;
    std::weak_ptr<Registration> registration;
    EventMask events;
  };

  // We're using a linked hash map here to ensure the events are called in the
  // registration order.  This isn't strictly speaking necessary, but makes
  // testing things easier.
  using RegistrationMap =
      LinkedHashMap<SocketFd, std::shared_ptr<Registration>>;

  // Calls poll(2) with the provided timeout and dispatches the callbacks
  // accordingly.
  void ProcessSocketEvents();

  // Adds the I/O callbacks for |fd| to the |ready_lits| as appopriate.
  void DispatchSocketEvent(std::vector<ReadyListEntry>& ready_list,
                           SocketFd fd,
                           short mask);  // NOLINT(runtime/int)

  // Runs all of the callbacks on the ready list.
  void RunReadyCallbacks(std::vector<ReadyListEntry>& ready_list);

  RegistrationMap registrations_;

  bool has_artificial_events_pending_ = false;

  WSAEVENT wakeup_ev_ = WSA_INVALID_EVENT;
  WSAEVENT socket_ev_ = WSA_INVALID_EVENT;

  MultimediaTimer timer_;
  // Since priority_queue<> by defult orders items in terms of
  // largest->smallest, using std::less<>, and we want smallest->largest,
  // we would like to use std::greater<> here.
  std::priority_queue<DelayedTaskInfo,
                      std::vector<DelayedTaskInfo>,
                      std::greater<DelayedTaskInfo>>
      timer_tasks_;
  UINT_PTR timer_id_ = 0;
};

}  // namespace base

#endif  // BASE_EVENT_LOOP_WIN_H_
