#include "event_loop.h"

#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/absl_check.h"
#include "event.h"
#include "logging.h"

namespace base {

EventLoop::EventLoop() : running_(false) {}

int EventLoop::ToCmsWait(absl::Duration max_wait_duration) {
  return max_wait_duration == absl::InfiniteDuration()
             ? INFINITE
             : std::max(static_cast<int64_t>(1),
                        absl::ToInt64Milliseconds(max_wait_duration));
}

void EventLoop::Begin() {
  CurrentTaskQueueSetter set_current(this);
  running_.store(true);
}

void EventLoop::Quit() {
  {
    // Ensure remaining deleted tasks are destroyed with Current() set up
    // to this task queue.
    absl::InlinedVector<absl::AnyInvocable<void() &&>, 4> pending;
    absl::MutexLock lock(&pending_lock_);
    pending_.swap(pending);
  }
  running_.store(false, std::memory_order_release);
}

bool EventLoop::IsQuitting() {
  return running_.load(std::memory_order_acquire) != true;
}

void EventLoop::RunPendingTasks() {
  absl::InlinedVector<absl::AnyInvocable<void() &&>, 4> tasks;
  {
    absl::MutexLock lock(&pending_lock_);
    if (pending_.empty()) {
      return;
    }
    tasks.swap(pending_);
  }

  ABSL_DCHECK(!tasks.empty());
  for (auto& task : tasks) {
    std::move(task)();
    // Prefer to delete the `task` before running the next one.
    task = nullptr;
  }
}

// Subclasses should implement this method to support the behavior defined in
// the PostTask and PostTaskTraits docs above.
void EventLoop::PostTaskImpl(absl::AnyInvocable<void() &&> task,
                             const PostTaskTraits& traits,
                             const Location& location) {
  if (IsQuitting()) {
    return;
  }

  if (IsCurrent()) {
    pending_.push_back(std::move(task));
  } else {
    absl::MutexLock lock(&pending_lock_);
    pending_.push_back(std::move(task));
  }

  WakeUp();
}

void EventLoop::PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task,
                                    absl::Duration delay,
                                    const PostDelayedTaskTraits& traits,
                                    const Location& location) {
  if (IsQuitting()) {
    return;
  }

  int64_t start_micros = CurrentTimeMicros();

  if (delay <= absl::ZeroDuration()) {
    PostTaskImpl(std::move(task), {}, location);
    return;
  }

  if (IsCurrent()) {
    PostDelayedTaskOnTaskQueue(std::move(task), start_micros, delay);
  } else {
    PostTask([start_micros, delay, task = std::move(task), this]() mutable {
      // Compensate for the time that has passed since the posting.
      PostDelayedTaskOnTaskQueue(std::move(task), start_micros, delay);
    });
  }
}

void EventLoop::BlockingCallImpl(FunctionView<void()> functor,
                                 const Location& location) {
  // todo
  if (IsQuitting()) {
    return;
  }

  if (IsCurrent()) {
    functor();
    return;
  }

  Event done;
  absl::Cleanup cleanup = [&done] { done.Set(); };
  PostTask([functor, cleanup = std::move(cleanup)] { functor(); });
  done.Wait(Event::kForever);
}

}  // namespace base
