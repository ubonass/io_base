// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>
#include "absl/types/span.h"
#include "event_loop_win.h"
#include "logging.h"

namespace base {

#define WSA_WAKEUP_EVENT 0
#define WSA_TIMER_EVENT 1
#define WSA_SOCKET_EVENT 2

namespace {

uint32_t GetPollMask(EventMask event_mask) {
  return ((event_mask & kEventReadable) ? POLLIN : 0) |
         ((event_mask & kEventWritable) ? POLLOUT : 0) |
         ((event_mask & kEventError) ? POLLERR : 0);
}

EventMask GetEventMask(uint32_t poll_mask) {
  return ((poll_mask & POLLIN) ? kEventReadable : 0) |
         ((poll_mask & POLLOUT) ? kEventWritable : 0) |
         ((poll_mask & POLLERR) ? kEventError : 0);
}

}  // namespace

// Default ctor needed to support priority_queue::pop().
DelayedTaskInfo::DelayedTaskInfo() {}

DelayedTaskInfo::DelayedTaskInfo(absl::Duration delay,
                                 absl::AnyInvocable<void() &&> task)
    : due_time_micros_(CurrentTimeMicros() + absl::ToInt64Microseconds(delay)),
      task_(std::move(task)) {}

// Implement for priority_queue.
bool DelayedTaskInfo::operator>(const DelayedTaskInfo& other) const {
  return due_time_micros_ > other.due_time_micros_;
}

// See below for why this method is const.
void DelayedTaskInfo::Run() const {
  ABSL_DCHECK(task_);
  std::move(task_)();
}

int64_t DelayedTaskInfo::due_time_micros() const {
  return due_time_micros_;
}

// Note: We create an event that requires manual reset.
MultimediaTimer::MultimediaTimer() : event_(WSACreateEvent()) {
  ABSL_CHECK(event_ != WSA_INVALID_EVENT)
      << "Failed to create wakeup_ev_ event: " << WSAGetLastError();
}

MultimediaTimer::~MultimediaTimer() {
  Cancel();
  WSACloseEvent(event_);
}

bool MultimediaTimer::StartOneShotTimer(UINT delay_ms) {
  ABSL_DCHECK_EQ(0, timer_id_);
  ABSL_DCHECK(event_ != nullptr);
  timer_id_ =
      ::timeSetEvent(delay_ms, 0, reinterpret_cast<LPTIMECALLBACK>(event_), 0,
                     TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
  return timer_id_ != 0;
}

void MultimediaTimer::Cancel() {
  if (timer_id_) {
    ::timeKillEvent(timer_id_);
    timer_id_ = 0;
  }
  // Now that timer is killed and not able to set the event, reset the event.
  // Doing it in opposite order is racy because event may be set between
  // event was reset and timer is killed leaving MultimediaTimer in surprising
  // state where both event is set and timer is canceled.
  WSAResetEvent(event_);
}

WSAEVENT* MultimediaTimer::event_for_wait() {
  return &event_;
}

EventLoopWin::EventLoopWin()
    : wakeup_ev_(WSACreateEvent()), socket_ev_(WSACreateEvent()) {
  ABSL_CHECK(wakeup_ev_ != WSA_INVALID_EVENT)
      << "Failed to create wakeup_ev_ event: " << WSAGetLastError();

  ABSL_CHECK(socket_ev_ != WSA_INVALID_EVENT)
      << "Failed to create socket_ev_ event: " << WSAGetLastError();
}

EventLoopWin::~EventLoopWin() {
  WSACloseEvent(wakeup_ev_);
  wakeup_ev_ = WSA_INVALID_EVENT;

  WSACloseEvent(socket_ev_);
  socket_ev_ = WSA_INVALID_EVENT;
}

void EventLoopWin::WakeUp() {
  WSASetEvent(wakeup_ev_);
}

bool EventLoopWin::RegisterSocket(SocketFd fd,
                                  EventMask events,
                                  EventListener* listener) {
  if (IsQuitting()) {
    return false;
  }

  auto [it, success] =
      registrations_.insert({fd, std::make_shared<Registration>()});
  if (!success) {
    return false;
  }

  Registration& registration = *it->second;
  registration.events = events;
  registration.listener = listener;

  // Bind Socket events to global event (socket_ev_)  objects
  if (WSAEventSelect(fd, socket_ev_, GetPollMask(events)) != 0) {
    registrations_.erase(fd);
    LOG(ERROR) << "WSAEventSelect failed: " << WSAGetLastError();
    return false;
  }

  return true;
}

bool EventLoopWin::UnregisterSocket(SocketFd fd) {
  if (IsQuitting()) {
    return false;
  }

  auto it = registrations_.find(fd);
  if (it == registrations_.end()) {
    return false;
  }
  // The Socket was disassociated from the global event (socket_ev_)
  WSAEventSelect(fd, socket_ev_, 0);
  return registrations_.erase(fd);
}

bool EventLoopWin::RearmSocket(SocketFd fd, EventMask events) {
  if (IsQuitting()) {
    return false;
  }

  auto it = registrations_.find(fd);
  if (it == registrations_.end()) {
    return false;
  }
  it->second->events |= events;
  // Bind the updated event mask again (socket_ev_)
  int result = WSAEventSelect(fd, socket_ev_, GetPollMask(it->second->events));
  if (result != 0) {
    LOG(ERROR) << "WSAEventSelect failed: " << WSAGetLastError();
    return false;
  }
  return true;
}

bool EventLoopWin::ArtificiallyNotifyEvent(SocketFd fd, EventMask events) {
  if (IsQuitting()) {
    return false;
  }

  auto it = registrations_.find(fd);
  if (it == registrations_.end()) {
    return false;
  }
  has_artificial_events_pending_ = true;
  it->second->artificially_notify_at_next_iteration |= events;
  // Triggers the global event (socket_ev_) object
  WSASetEvent(socket_ev_);
  return true;
}

void EventLoopWin::RunEventLoopOnce(absl::Duration default_timeout) {
  auto timeout_ms = ToCmsWait(default_timeout);
  // event[0] wakeup_ev_
  // event[1] socket_ev_
  // event[2] timer_ev_;
  std::vector<WSAEVENT> events = {
      wakeup_ev_,                // WSA_WAKEUP_EVENT
      *timer_.event_for_wait(),  // WSA_TIMER_EVENT
      socket_ev_                 // WSA_SOCKET_EVENT
  };

  // Wait for the global event to trigger
  auto wait_result = WSAWaitForMultipleEvents(
      static_cast<DWORD>(events.size()), &events[0], FALSE, timeout_ms, FALSE);

  if (wait_result == WSA_WAIT_FAILED) {
    // Failed?
    // TODO(pthatcher): need a better strategy than this!
    LOG(WARNING) << "WSAEnumNetworkEvents failed: " << WSAGetLastError();
    return;
  }

  if (wait_result == WSA_WAIT_TIMEOUT && !has_artificial_events_pending_) {
    LOG(WARNING)
        << "WSA_WAIT_TIMEOUT but has_artificial_events_pending false..";
    return;
  }

  // Figure out which one it is and call it
  int index = wait_result - WSA_WAIT_EVENT_0;
  if (index == WSA_WAKEUP_EVENT) {
    ProcessPendingTasks();
  } else if (index == WSA_TIMER_EVENT) {
    ProcessTimerTasksUpTo(CurrentTimeMicros());
  } else {
    ProcessSocketEvents();
  }

  if (index == WSA_SOCKET_EVENT) {
    // Reset the network event until new activity occurs
    WSAResetEvent(socket_ev_);
  }
}

void EventLoopWin::ProcessSocketEvents() {
  if (registrations_.empty()) {
    return;
  }

  std::vector<ReadyListEntry> ready_list;
  // Iterate over all sockets to check event status
  for (auto& [fd, registration] : registrations_) {
    WSANETWORKEVENTS network_events;

    if (WSAEnumNetworkEvents(fd, socket_ev_, &network_events) != 0) {
      LOG(ERROR) << "WSAEnumNetworkEvents failed: " << WSAGetLastError();
      continue;
    }

    // Convert network events to POLL event mask
    short revents = 0;
    if (network_events.lNetworkEvents & FD_READ) {
      revents |= POLLIN;
    }

    if (network_events.lNetworkEvents & FD_WRITE) {
      revents |= POLLOUT;
    }

    if (network_events.lNetworkEvents & FD_CLOSE) {
      revents |= POLLHUP;
    }

    if (network_events.lNetworkEvents & FD_ACCEPT) {
      revents |= POLLIN;
    }

    if (network_events.lNetworkEvents & FD_CONNECT) {
      revents |= POLLOUT;
      if (network_events.iErrorCode[FD_CONNECT_BIT] != 0) {
        revents |= POLLERR;
      }
    }

    // merge artificially_notify_at_next_iteration
    revents |= GetPollMask(registration->artificially_notify_at_next_iteration);
    registration->artificially_notify_at_next_iteration = 0;

    if (revents != 0) {
      DispatchSocketEvent(ready_list, fd, revents);
    }
  }

  has_artificial_events_pending_ = false;

  if (!ready_list.empty()) {
    RunReadyCallbacks(ready_list);
  }
}

void EventLoopWin::DispatchSocketEvent(std::vector<ReadyListEntry>& ready_list,
                                       SocketFd fd,
                                       short mask) {
  auto it = registrations_.find(fd);
  if (it == registrations_.end()) {
    // QUIC_BUG(poll returned an unregistered fd) << fd;
    return;
  }
  Registration& registration = *it->second;

  // Make sure the event is within the registered scope
  mask &= GetPollMask(registration.events);
  if (!mask) {
    return;
  }

  ready_list.push_back(ReadyListEntry{fd, it->second, GetEventMask(mask)});
  registration.events &= ~GetEventMask(mask);
}

void EventLoopWin::RunReadyCallbacks(std::vector<ReadyListEntry>& ready_list) {
  for (ReadyListEntry& entry : ready_list) {
    std::shared_ptr<Registration> registration = entry.registration.lock();
    if (!registration) {
      // The socket has been unregistered from within one of the callbacks.
      continue;
    }
    registration->listener->OnSocketEvent(this, entry.fd, entry.events);
  }
  ready_list.clear();
}

void EventLoopWin::PostDelayedTaskOnTaskQueue(
    absl::AnyInvocable<void() &&> task,
    int64_t start_micros,
    absl::Duration delay) {
  int64_t now_micros = CurrentTimeMicros();
  delay -= absl::Microseconds(now_micros - start_micros);
  auto info = std::make_unique<DelayedTaskInfo>(delay, std::move(task));
  bool need_to_schedule_timers =
      timer_tasks_.empty() ||
      timer_tasks_.top().due_time_micros() > info->due_time_micros();
  timer_tasks_.push(std::move(*info));
  if (need_to_schedule_timers) {
    CancelTimers();
    ScheduleNextTimer();
  }
}

void EventLoopWin::RunDueTasks() {
  ABSL_DCHECK(!timer_tasks_.empty());
  int64_t now_micros = CurrentTimeMicros();
  do {
    const auto& top = timer_tasks_.top();
    if (top.due_time_micros() > now_micros) {
      break;
    }
    top.Run();
    timer_tasks_.pop();
  } while (!timer_tasks_.empty());
}

void EventLoopWin::ScheduleNextTimer() {
  ABSL_DCHECK_EQ(timer_id_, 0);
  if (timer_tasks_.empty()) {
    return;
  }

  const auto& next_task = timer_tasks_.top();
  absl::Duration delay = std::max(
      absl::ZeroDuration(),
      absl::Microseconds(next_task.due_time_micros() - CurrentTimeMicros()));
  uint32_t milliseconds = absl::ToInt64Milliseconds(delay);
  if (!timer_.StartOneShotTimer(milliseconds)) {
    timer_id_ = ::SetTimer(nullptr, 0, milliseconds, nullptr);
  }
}

void EventLoopWin::CancelTimers() {
  timer_.Cancel();
  if (timer_id_) {
    ::KillTimer(nullptr, timer_id_);
    timer_id_ = 0;
  }
}

void EventLoopWin::ProcessTimerTasksUpTo(int64_t current_time_micros) {
  if (timer_tasks_.empty()) {
    return;
  }
  // The multimedia timer was signaled.
  timer_.Cancel();
  RunDueTasks();
  ScheduleNextTimer();
}

void EventLoopWin::ProcessPendingTasks() {
  WSAResetEvent(wakeup_ev_);
  RunPendingTasks();
}

}  // namespace base
