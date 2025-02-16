#ifndef BASE_EVENT_LOOP_H
#define BASE_EVENT_LOOP_H

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "build_macros.h"

#if defined(_MSC_VER) && _MSC_VER < 1300
#pragma warning(disable : 4786)
#endif

#ifdef MEMORY_SANITIZER
#include <sanitizer/msan_interface.h>
#endif

#if defined(__OS_POSIX__)
#if defined(__OS_LINUX__)
// On Linux, use epoll.
#include <sys/epoll.h>

#define __USE_EPOLL__ 1
#elif defined(__OS_FUCHSIA__) || defined(__OS_MAC__)
// Fuchsia implements select and poll but not epoll, and testing shows that poll
// is faster than select.
#include <poll.h>
#define __USE_POLL__ 1
#else
// On other POSIX systems, use select by default.
#endif  // __OS_LINUX__, __OS_FUCHSIA__, __OS_MAC__
#endif  // __OS_POSIX__

#if defined(__OS_WIN__)
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "recursive_critical_section.h"

namespace base {

enum DispatcherEvent {
  DE_READ = 0x0001,
  DE_WRITE = 0x0002,
  DE_CONNECT = 0x0004,
  DE_CLOSE = 0x0008,
  DE_ACCEPT = 0x0010,
};

class Dispatcher {
 public:
  virtual ~Dispatcher() {}
  virtual uint32_t GetRequestedEvents() = 0;
  virtual void OnEvent(uint32_t ff, int err) = 0;
#if defined(__OS_WIN__)
  virtual WSAEVENT GetWSAEvent() = 0;
  virtual SOCKET GetSocket() = 0;
  virtual bool CheckSignalClose() = 0;
#else
  virtual int GetDescriptor() = 0;
  virtual bool IsDescriptorClosed() = 0;
#endif
};

#if defined(__OS_WIN__)
inline uint32_t FlagsToEvents(uint32_t events) {
  uint32_t ffFD = FD_CLOSE;
  if (events & DE_READ) {
    ffFD |= FD_READ;
  }
  if (events & DE_WRITE) {
    ffFD |= FD_WRITE;
  }
  if (events & DE_CONNECT) {
    ffFD |= FD_CONNECT;
  }
  if (events & DE_ACCEPT) {
    ffFD |= FD_ACCEPT;
  }
  return ffFD;
}

class EvenLoop;

// Sets the value of a boolean value to false when signaled.
class Signaler : public Dispatcher {
 public:
  Signaler(EvenLoop* loop, bool& flag_to_clear);

  ~Signaler() override;

  virtual void Signal();

  uint32_t GetRequestedEvents() override;

  void OnEvent(uint32_t ff, int err) override;

  WSAEVENT GetWSAEvent() override;
  SOCKET GetSocket() override;

  bool CheckSignalClose() override;

 private:
  EvenLoop* loop_;
  WSAEVENT hev_;
  bool& flag_to_clear_;
};
#endif

#if defined(__OS_POSIX__)
class Signaler : public Dispatcher {
 public:
  Signaler(EvenLoop* loop, bool& flag_to_clear);

  ~Signaler() override;

  virtual void Signal();

  uint32_t GetRequestedEvents() override;

  void OnEvent(uint32_t /* ff */, int /* err */) override;

  int GetDescriptor() override;

  bool IsDescriptorClosed() override;

 private:
  EvenLoop* const loop_;
  bool fSignaled_;
  absl::Mutex mutex_;
  bool& flag_to_clear_;
  std::array<int, 2> afd_ = {-1, -1};
};
#endif

class EvenLoop {
 public:
  EvenLoop();
  virtual ~EvenLoop();

  virtual bool Wait(absl::Duration max_wait_duration, bool process_io);

  virtual void WakeUp();

  virtual void Add(Dispatcher* dispatcher);
  virtual void Remove(Dispatcher* dispatcher);
  virtual void Update(Dispatcher* dispatcher);

 protected:
  // The number of events to process with one call to "epoll_wait".
  static constexpr size_t kNumEpollEvents = 128;
  // A local historical definition of "foreverness", in milliseconds.
  static constexpr int kForeverMs = -1;

  static int ToCmsWait(absl::Duration max_wait_duration);

#if defined(__OS_POSIX__)
  bool WaitSelect(int cmsWait, bool process_io);

#if defined(__USE_EPOLL__)
  void AddEpoll(Dispatcher* dispatcher, uint64_t key);
  void RemoveEpoll(Dispatcher* dispatcher);
  void UpdateEpoll(Dispatcher* dispatcher, uint64_t key);
  bool WaitEpoll(int cmsWait);
  bool WaitPollOneDispatcher(int cmsWait, Dispatcher* dispatcher);

  // This array is accessed in isolation by a thread calling into Wait().
  // It's useless to use a SequenceChecker to guard it because a socket
  // server can outlive the thread it's bound to, forcing the Wait call
  // to have to reset the sequence checker on Wait calls.
  std::array<epoll_event, kNumEpollEvents> epoll_events_;
  const int epoll_fd_ = INVALID_SOCKET;

#elif defined(__USE_POLL__)
  bool WaitPoll(int cmsWait, bool process_io);

#endif  // __USE_EPOLL__, __USE_POLL__
#endif  // __OS_POSIX__

  // uint64_t keys are used to uniquely identify a dispatcher in order to avoid
  // the ABA problem during the epoll loop (a dispatcher being destroyed and
  // replaced by one with the same address).
  uint64_t next_dispatcher_key_ = 0;
  std::unordered_map<uint64_t, Dispatcher*> dispatcher_by_key_;
  // Reverse lookup necessary for removals/updates.
  std::unordered_map<Dispatcher*, uint64_t> key_by_dispatcher_;
  // A list of dispatcher keys that we're interested in for the current
  // select(), poll(), or WSAWaitForMultipleEvents() loop. Again, used to avoid
  // the ABA problem (a socket being destroyed and a new one created with the
  // same handle, erroneously receiving the events from the destroyed socket).
  //
  // Kept as a member variable just for efficiency.
  std::vector<uint64_t> current_dispatcher_keys_;
  Signaler* signal_wakeup_;  // Assigned in constructor only
  RecursiveCriticalSection crit_;
#if defined(__OS_WIN__)
  const WSAEVENT socket_ev_;
#endif
  bool fWait_;
  // Are we currently in a select()/epoll()/WSAWaitForMultipleEvents loop?
  // Used for a DCHECK, because we don't support reentrant waiting.
  bool waiting_ = false;
};

}  // namespace base

#endif
