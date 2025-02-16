#ifndef BASE_EVENT_LOOP_H
#define BASE_EVENT_LOOP_H

#include <cstdint>
#include <memory>
#include <type_traits>

#if defined(_WIN32)
#include <windows.h>
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif  // defined(_WIN32)

#include "absl/base/attributes.h"
#include "absl/container/inlined_vector.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "function_view.h"
#include "task_queue_base.h"
#include "time_utils.h"

namespace base {

#if defined(_WIN32)
using SocketFd = SOCKET;
#else
using SocketFd = int;
#endif

// A bitmask indicating a set of I/O events.
using EventMask = uint8_t;

inline constexpr EventMask kEventReadable = 0x01;
inline constexpr EventMask kEventWritable = 0x02;
inline constexpr EventMask kEventError = 0x04;

class EventLoop;

// A listener associated with a file descriptor.
class EventListener {
 public:
  virtual ~EventListener() = default;

  virtual void OnSocketEvent(EventLoop* event_loop,
                             SocketFd fd,
                             EventMask events) {}
};

class EventLoop : public TaskQueueBase {
 public:
  EventLoop();
  virtual ~EventLoop() = default;
  // EventLoop binds to the specified thread
  void Begin();
  // Release all tasks
  void Quit();
  //
  bool IsQuitting();

 public:
  // wakeup the loop
  virtual void WakeUp() = 0;
  // Indicates whether the event loop implementation supports edge-triggered
  // notifications.  If true, all of the events are permanent and are notified
  // as long as they are registered.  If false, whenever an event is triggered,
  // the event registration is unset and has to be re-armed using RearmSocket().
  virtual bool SupportsEdgeTriggered() const { return false; }

  // Watches for all of the requested |events| that occur on the |fd| and
  // notifies the |listener| about them.  |fd| must not be already registered;
  // if it is, the function returns false.  The |listener| must be alive for as
  // long as it is registered.
  virtual ABSL_MUST_USE_RESULT bool RegisterSocket(SocketFd fd,
                                                   EventMask events,
                                                   EventListener* listener) = 0;
  // Removes the listener associated with |fd|.  Returns false if the listener
  // is not found.
  virtual ABSL_MUST_USE_RESULT bool UnregisterSocket(SocketFd fd) = 0;
  // Adds |events| to the list of the listened events for |fd|, given that |fd|
  // is already registered.  Must be only called if SupportsEdgeTriggered() is
  // false.
  virtual ABSL_MUST_USE_RESULT bool RearmSocket(SocketFd fd,
                                                EventMask events) = 0;
  // Causes the |fd| to be notified of |events| on the next event loop iteration
  // even if none of the specified events has happened.
  virtual ABSL_MUST_USE_RESULT bool ArtificiallyNotifyEvent(
      SocketFd fd,
      EventMask events) = 0;

  // Runs a single iteration of the event loop.  The iteration will run for at
  // most |default_timeout|.
  virtual void RunEventLoopOnce(absl::Duration default_timeout) = 0;

 public:
  // Convenience method to invoke a functor on another thread.
  // Blocks the current thread until execution is complete.
  // Ex: thread.BlockingCall([&] { result = MyFunctionReturningBool(); });
  // NOTE: This function can only be called when synchronous calls are allowed.
  // See ScopedDisallowBlockingCalls for details.
  // NOTE: Blocking calls are DISCOURAGED, consider if what you're doing can
  // be achieved with PostTask() and callbacks instead.
  void BlockingCall(FunctionView<void()> functor,
                    const Location& location = Location::Current()) {
    BlockingCallImpl(std::move(functor), location);
  }

  template <typename Functor,
            typename ReturnT = std::invoke_result_t<Functor>,
            typename = typename std::enable_if_t<!std::is_void_v<ReturnT>>>
  ReturnT BlockingCall(Functor&& functor,
                       const Location& location = Location::Current()) {
    ReturnT result;
    BlockingCall([&] { result = std::forward<Functor>(functor)(); }, location);
    return result;
  }

  virtual void BlockingCallImpl(FunctionView<void()> functor,
                                const Location& location);

 public:
  virtual void PostDelayedTaskOnTaskQueue(absl::AnyInvocable<void() &&> task,
                                          int64_t start_micros,
                                          absl::Duration delay) = 0;

 protected:
  void RunPendingTasks();
  // Subclasses should implement this method to support the behavior defined in
  // the PostTask and PostTaskTraits docs above.
  void PostTaskImpl(absl::AnyInvocable<void() &&> task,
                    const PostTaskTraits& traits,
                    const Location& location) override;

  void PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task,
                           absl::Duration delay,
                           const PostDelayedTaskTraits& traits,
                           const Location& location) override;

 protected:
  static int ToCmsWait(absl::Duration max_wait_duration);
  std::atomic<bool> running_;
  absl::Mutex pending_lock_;
  absl::InlinedVector<absl::AnyInvocable<void() &&>, 4> pending_;
};

}  // namespace base

#endif
