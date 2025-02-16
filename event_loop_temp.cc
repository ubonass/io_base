#include <errno.h>
#include "event_loop.h"

#if defined(__OS_LINUX__)
#include <linux/sockios.h>
#endif

#if defined(__OS_POSIX__)
#include <fcntl.h>
#if defined(__USE_EPOLL__)
// "poll" will be used to wait for the signal dispatcher.
#include <poll.h>
#elif defined(__USE_POLL__)
#include <poll.h>
#endif
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#include "absl/log/absl_check.h"
#include "logging.h"

#if defined(__OS_WIN__)
#define LAST_SYSTEM_ERROR (::GetLastError())
#elif defined(__native_client__) && __native_client__
#define LAST_SYSTEM_ERROR (0)
#elif defined(__OS_POSIX__)
#define LAST_SYSTEM_ERROR (errno)
#endif  // __OS_WIN__

namespace {
class ScopedSetTrue {
 public:
  ScopedSetTrue(bool* value) : value_(value) {
    ABSL_DCHECK(!*value_);
    *value_ = true;
  }
  ~ScopedSetTrue() { *value_ = false; }

 private:
  bool* value_;
};

}  // namespace

namespace base {
#if defined(__OS_WIN__)
Signaler::Signaler(EvenLoop* loop, bool& flag_to_clear)
    : loop_(loop), flag_to_clear_(flag_to_clear) {
  hev_ = WSACreateEvent();
  if (hev_) {
    loop_->Add(this);
  }
}

Signaler::~Signaler() {
  if (hev_ != nullptr) {
    loop_->Remove(this);
    WSACloseEvent(hev_);
    hev_ = nullptr;
  }
}

void Signaler::Signal() {
  if (hev_ != nullptr) {
    WSASetEvent(hev_);
  }
}

uint32_t Signaler::GetRequestedEvents() {
  return 0;
}

void Signaler::OnEvent(uint32_t ff, int err) {
  WSAResetEvent(hev_);
  flag_to_clear_ = false;
}

WSAEVENT Signaler::GetWSAEvent() {
  return hev_;
}

SOCKET Signaler::GetSocket() {
  return INVALID_SOCKET;
}

bool Signaler::CheckSignalClose() {
  return false;
}
#endif

#if defined(__OS_POSIX__)
Signaler::Signaler(EvenLoop* loop, bool& flag_to_clear)
    : loop_(loop), fSignaled_(false), flag_to_clear_(flag_to_clear) {
#if defined(__APPLE__)
  if (pipe(afd_.data()) < 0) {
    LOG(ERROR) << "pipe failed";
  }
#else
  afd_[0] = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (afd_[0] < 0) {
    LOG(ERROR) << "create event fd failed, errno:" << errno << ","
               << strerror(errno);
  }
#endif
  loop_->Add(this);
}

Signaler::~Signaler() {
  loop_->Remove(this);
#if defined(__APPLE__)
  close(afd_[0]);
  close(afd_[1]);
#else
  if (afd_[0] > 0) {
    close(afd_[0]);
  }
#endif
}

void Signaler::Signal() {
  absl::MutexLock lock(&mutex_);
  if (!fSignaled_) {
#if defined(__APPLE__)
    const uint8_t b[1] = {0};
    const ssize_t res = write(afd_[1], b, sizeof(b));
    ABSL_DCHECK_EQ(1, res);
#else
    const uint64_t one = 1;
    const ssize_t ret = write(afd_[0], &one, sizeof(one));
    ABSL_DCHECK_GT(ret, 0);
#endif
    fSignaled_ = true;
  }
}

uint32_t Signaler::GetRequestedEvents() {
  return DE_READ;
}

void Signaler::OnEvent(uint32_t ff, int err) {
  // It is not possible to perfectly emulate an auto-resetting event with
  // pipes.  This simulates it by resetting before the event is handled.

  absl::MutexLock lock(&mutex_);
  if (fSignaled_) {
#if defined(__APPLE__)
    uint8_t b[4];  // Allow for reading more than 1 byte, but expect 1.
    const ssize_t res = read(afd_[0], b, sizeof(b));
    ABSL_DCHECK_EQ(1, res);
#else
    uint64_t tmp = 0;
    const ssize_t ret = read(afd_[0], &tmp, sizeof(tmp));
    ABSL_DCHECK_GT(ret, 0);
#endif
    fSignaled_ = false;
  }
  flag_to_clear_ = false;
}

int Signaler::GetDescriptor() {
  return afd_[0];
}

bool Signaler::IsDescriptorClosed() {
  return false;
}
#endif

EvenLoop::EvenLoop()
    :
#if defined(__USE_EPOLL__)
      // Since Linux 2.6.8, the size argument is ignored, but must be greater
      // than zero. Before that the size served as hint to the kernel for the
      // amount of space to initially allocate in internal data structures.
      epoll_fd_(epoll_create(FD_SETSIZE)),
#endif
#if defined(__OS_WIN__)
      socket_ev_(WSACreateEvent()),
#endif
      fWait_(false) {
#if defined(__USE_EPOLL__)
  if (epoll_fd_ == -1) {
    // Not an error, will fall back to "select" below.
    ABSL_LOG_E(WARNING, EN, errno) << "epoll_create";
    // Note that -1 == INVALID_SOCKET, the alias used by later checks.
  }
#endif
  // The `fWait_` flag to be cleared by the Signaler.
  signal_wakeup_ = new Signaler(this, fWait_);
}

EvenLoop::~EvenLoop() {
#if defined(__OS_WIN__)
  WSACloseEvent(socket_ev_);
#endif
  delete signal_wakeup_;
#if defined(__USE_EPOLL__)
  if (epoll_fd_ != INVALID_SOCKET) {
    close(epoll_fd_);
  }
#endif
  ABSL_DCHECK(dispatcher_by_key_.empty());
  ABSL_DCHECK(key_by_dispatcher_.empty());
}

bool EvenLoop::Wait(absl::Duration max_wait_duration, bool process_io) {
  // TODO'
  return true;
}

void EvenLoop::WakeUp() {
  signal_wakeup_->Signal();
}

void EvenLoop::Add(Dispatcher* dispatcher) {
  // TODO
}

void EvenLoop::Remove(Dispatcher* dispatcher) {
  // TODO
}

void EvenLoop::Update(Dispatcher* dispatcher) {
  // TODO
}

int EvenLoop::ToCmsWait(absl::Duration max_wait_duration) {
  return max_wait_duration == absl::InfiniteDuration()
             ? kForeverMs
             : std::max(static_cast<int64_t>(1),
                        ToInt64Milliseconds(max_wait_duration));
}

#if defined(__OS_POSIX__)

bool EvenLoop::Wait(absl::Duration max_wait_duration, bool process_io) {
  // We don't support reentrant waiting.
  ABSL_DCHECK(!waiting_);
  ScopedSetTrue s(&waiting_);
  const int cmsWait = ToCmsWait(max_wait_duration);

#if defined(__USE_POLL__)
  return WaitPoll(cmsWait, process_io);
#else
#if defined(__USE_EPOLL__)
  // We don't keep a dedicated "epoll" descriptor containing only the non-IO
  // (i.e. signaling) dispatcher, so "poll" will be used instead of the default
  // "select" to support sockets larger than FD_SETSIZE.
  if (!process_io) {
    return WaitPollOneDispatcher(cmsWait, signal_wakeup_);
  } else if (epoll_fd_ != INVALID_SOCKET) {
    return WaitEpoll(cmsWait);
  }
#endif
  return WaitSelect(cmsWait, process_io);
#endif
}

// `error_event` is true if we are responding to an event where we know an
// error has occurred, which is possible with the poll/epoll implementations
// but not the select implementation.
//
// `check_error` is true if there is the possibility of an error.
static void ProcessEvents(Dispatcher* dispatcher,
                          bool readable,
                          bool writable,
                          bool error_event,
                          bool check_error) {
  ABSL_DCHECK(!(error_event && !check_error));
  int errcode = 0;
  if (check_error) {
    socklen_t len = sizeof(errcode);
    int res = ::getsockopt(dispatcher->GetDescriptor(), SOL_SOCKET, SO_ERROR,
                           &errcode, &len);
    if (res < 0) {
      // If we are sure an error has occurred, or if getsockopt failed for a
      // socket descriptor, make sure we set the error code to a nonzero value.
      if (error_event || errno != ENOTSOCK) {
        errcode = EBADF;
      }
    }
  }

  // Most often the socket is writable or readable or both, so make a single
  // virtual call to get requested events
  const uint32_t requested_events = dispatcher->GetRequestedEvents();
  uint32_t ff = 0;

  // Check readable descriptors. If we're waiting on an accept, signal
  // that. Otherwise we're waiting for data, check to see if we're
  // readable or really closed.
  // TODO(pthatcher): Only peek at TCP descriptors.
  if (readable) {
    if (errcode || dispatcher->IsDescriptorClosed()) {
      ff |= DE_CLOSE;
    } else if (requested_events & DE_ACCEPT) {
      ff |= DE_ACCEPT;
    } else {
      ff |= DE_READ;
    }
  }

  // Check writable descriptors. If we're waiting on a connect, detect
  // success versus failure by the reaped error code.
  if (writable) {
    if (requested_events & DE_CONNECT) {
      if (!errcode) {
        ff |= DE_CONNECT;
      }
    } else {
      ff |= DE_WRITE;
    }
  }

  // Make sure we report any errors regardless of whether readable or writable.
  if (errcode) {
    ff |= DE_CLOSE;
  }

  // Tell the descriptor about the event.
  if (ff != 0) {
    dispatcher->OnEvent(ff, errcode);
  }
}

#if defined(__USE_POLL__) || defined(__USE_EPOLL__)
static void ProcessPollEvents(Dispatcher* dispatcher, const pollfd& pfd) {
  bool readable = (pfd.revents & (POLLIN | POLLPRI));
  bool writable = (pfd.revents & POLLOUT);

  // Linux and Fuchsia define POLLRDHUP, which is set when the peer has
  // disconnected. On other platforms, we only check for POLLHUP.
#if defined(__OS_LINUX__) || defined(__OS_FUCHSIA__)
  constexpr short kEvents = POLLRDHUP | POLLERR | POLLHUP;
#else
  constexpr short kEvents = POLLERR | POLLHUP;
#endif
  bool error = (pfd.revents & kEvents);

  ProcessEvents(dispatcher, readable, writable, error, error);
}

static pollfd DispatcherToPollfd(Dispatcher* dispatcher) {
  pollfd fd{
      .fd = dispatcher->GetDescriptor(),
      .events = 0,
      .revents = 0,
  };

  uint32_t ff = dispatcher->GetRequestedEvents();
  if (ff & (DE_READ | DE_ACCEPT)) {
    fd.events |= POLLIN;
  }
  if (ff & (DE_WRITE | DE_CONNECT)) {
    fd.events |= POLLOUT;
  }

  return fd;
}
#endif  // __USE_POLL__ || __USE_EPOLL__

bool EvenLoop::WaitSelect(int cmsWait, bool process_io) {
  // Calculate timing information

  struct timeval* ptvWait = nullptr;
  struct timeval tvWait;
  int64_t stop_us;
  if (cmsWait != kForeverMs) {
    // Calculate wait timeval
    tvWait.tv_sec = cmsWait / 1000;
    tvWait.tv_usec = (cmsWait % 1000) * 1000;
    ptvWait = &tvWait;

    // Calculate when to return
    stop_us = rtc::TimeMicros() + cmsWait * 1000;
  }

  fd_set fdsRead;
  fd_set fdsWrite;
// Explicitly unpoison these FDs on MemorySanitizer which doesn't handle the
// inline assembly in FD_ZERO.
// http://crbug.com/344505
#ifdef MEMORY_SANITIZER
  __msan_unpoison(&fdsRead, sizeof(fdsRead));
  __msan_unpoison(&fdsWrite, sizeof(fdsWrite));
#endif

  fWait_ = true;

  while (fWait_) {
    // Zero all fd_sets. Although select() zeros the descriptors not signaled,
    // we may need to do this for dispatchers that were deleted while
    // iterating.
    FD_ZERO(&fdsRead);
    FD_ZERO(&fdsWrite);
    int fdmax = -1;
    {
      CritScope cr(&crit_);
      current_dispatcher_keys_.clear();
      for (auto const& kv : dispatcher_by_key_) {
        uint64_t key = kv.first;
        Dispatcher* pdispatcher = kv.second;
        if (!process_io && (pdispatcher != signal_wakeup_)) {
          continue;
        }
        current_dispatcher_keys_.push_back(key);
        int fd = pdispatcher->GetDescriptor();
        // "select"ing a file descriptor that is equal to or larger than
        // FD_SETSIZE will result in undefined behavior.
        ABSL_DCHECK_LT(fd, FD_SETSIZE);
        if (fd > fdmax) {
          fdmax = fd;
        }

        uint32_t ff = pdispatcher->GetRequestedEvents();
        if (ff & (DE_READ | DE_ACCEPT)) {
          FD_SET(fd, &fdsRead);
        }
        if (ff & (DE_WRITE | DE_CONNECT)) {
          FD_SET(fd, &fdsWrite);
        }
      }
    }

    // Wait then call handlers as appropriate
    // < 0 means error
    // 0 means timeout
    // > 0 means count of descriptors ready
    int n = select(fdmax + 1, &fdsRead, &fdsWrite, nullptr, ptvWait);

    // If error, return error.
    if (n < 0) {
      if (errno != EINTR) {
        ABSL_LOG_E(LS_ERROR, EN, errno) << "select";
        return false;
      }
      // Else ignore the error and keep going. If this EINTR was for one of the
      // signals managed by this EvenLoop, the
      // PosixSignalDeliveryDispatcher will be in the signaled state in the next
      // iteration.
    } else if (n == 0) {
      // If timeout, return success
      return true;
    } else {
      // We have signaled descriptors
      CritScope cr(&crit_);
      // Iterate only on the dispatchers whose file descriptors were passed into
      // select; this avoids the ABA problem (a socket being destroyed and a new
      // one created with the same file descriptor).
      for (uint64_t key : current_dispatcher_keys_) {
        if (!dispatcher_by_key_.count(key)) {
          continue;
        }
        Dispatcher* pdispatcher = dispatcher_by_key_.at(key);

        int fd = pdispatcher->GetDescriptor();

        bool readable = FD_ISSET(fd, &fdsRead);
        if (readable) {
          FD_CLR(fd, &fdsRead);
        }

        bool writable = FD_ISSET(fd, &fdsWrite);
        if (writable) {
          FD_CLR(fd, &fdsWrite);
        }

        // The error code can be signaled through reads or writes.
        ProcessEvents(pdispatcher, readable, writable, /*error_event=*/false,
                      readable || writable);
      }
    }

    // Recalc the time remaining to wait. Doing it here means it doesn't get
    // calced twice the first time through the loop
    if (ptvWait) {
      ptvWait->tv_sec = 0;
      ptvWait->tv_usec = 0;
      int64_t time_left_us = stop_us - rtc::TimeMicros();
      if (time_left_us > 0) {
        ptvWait->tv_sec = time_left_us / rtc::kNumMicrosecsPerSec;
        ptvWait->tv_usec = time_left_us % rtc::kNumMicrosecsPerSec;
      }
    }
  }

  return true;
}

#if defined(__USE_EPOLL__)

void EvenLoop::AddEpoll(Dispatcher* pdispatcher, uint64_t key) {
  ABSL_DCHECK(epoll_fd_ != INVALID_SOCKET);
  int fd = pdispatcher->GetDescriptor();
  ABSL_DCHECK(fd != INVALID_SOCKET);
  if (fd == INVALID_SOCKET) {
    return;
  }

  struct epoll_event event = {0};
  event.events = GetEpollEvents(pdispatcher->GetRequestedEvents());
  if (event.events == 0u) {
    // Don't add at all if we don't have any requested events. Could indicate a
    // closed socket.
    return;
  }
  event.data.u64 = key;
  int err = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
  ABSL_DCHECK_EQ(err, 0);
  if (err == -1) {
    ABSL_LOG_E(LS_ERROR, EN, errno) << "epoll_ctl EPOLL_CTL_ADD";
  }
}

void EvenLoop::RemoveEpoll(Dispatcher* pdispatcher) {
  ABSL_DCHECK(epoll_fd_ != INVALID_SOCKET);
  int fd = pdispatcher->GetDescriptor();
  ABSL_DCHECK(fd != INVALID_SOCKET);
  if (fd == INVALID_SOCKET) {
    return;
  }

  struct epoll_event event = {0};
  int err = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &event);
  ABSL_DCHECK(err == 0 || errno == ENOENT);
  // Ignore ENOENT, which could occur if this descriptor wasn't added due to
  // having no requested events.
  if (err == -1 && errno != ENOENT) {
    ABSL_LOG_E(LS_ERROR, EN, errno) << "epoll_ctl EPOLL_CTL_DEL";
  }
}

void EvenLoop::UpdateEpoll(Dispatcher* pdispatcher, uint64_t key) {
  ABSL_DCHECK(epoll_fd_ != INVALID_SOCKET);
  int fd = pdispatcher->GetDescriptor();
  ABSL_DCHECK(fd != INVALID_SOCKET);
  if (fd == INVALID_SOCKET) {
    return;
  }

  struct epoll_event event = {0};
  event.events = GetEpollEvents(pdispatcher->GetRequestedEvents());
  event.data.u64 = key;
  // Remove if we don't have any requested events. Could indicate a closed
  // socket.
  if (event.events == 0u) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &event);
  } else {
    int err = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event);
    ABSL_DCHECK(err == 0 || errno == ENOENT);
    if (err == -1) {
      // Could have been removed earlier due to no requested events.
      if (errno == ENOENT) {
        err = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
        if (err == -1) {
          ABSL_LOG_E(LS_ERROR, EN, errno) << "epoll_ctl EPOLL_CTL_ADD";
        }
      } else {
        ABSL_LOG_E(LS_ERROR, EN, errno) << "epoll_ctl EPOLL_CTL_MOD";
      }
    }
  }
}

bool EvenLoop::WaitEpoll(int cmsWait) {
  ABSL_DCHECK(epoll_fd_ != INVALID_SOCKET);
  int64_t msWait = -1;
  int64_t msStop = -1;
  if (cmsWait != kForeverMs) {
    msWait = cmsWait;
    msStop = TimeAfter(cmsWait);
  }

  fWait_ = true;
  while (fWait_) {
    // Wait then call handlers as appropriate
    // < 0 means error
    // 0 means timeout
    // > 0 means count of descriptors ready
    int n = epoll_wait(epoll_fd_, epoll_events_.data(), epoll_events_.size(),
                       static_cast<int>(msWait));
    if (n < 0) {
      if (errno != EINTR) {
        ABSL_LOG_E(LS_ERROR, EN, errno) << "epoll";
        return false;
      }
      // Else ignore the error and keep going. If this EINTR was for one of the
      // signals managed by this EvenLoop, the
      // PosixSignalDeliveryDispatcher will be in the signaled state in the next
      // iteration.
    } else if (n == 0) {
      // If timeout, return success
      return true;
    } else {
      // We have signaled descriptors
      CritScope cr(&crit_);
      for (int i = 0; i < n; ++i) {
        const epoll_event& event = epoll_events_[i];
        uint64_t key = event.data.u64;
        if (!dispatcher_by_key_.count(key)) {
          // The dispatcher for this socket no longer exists.
          continue;
        }
        Dispatcher* pdispatcher = dispatcher_by_key_.at(key);

        bool readable = (event.events & (EPOLLIN | EPOLLPRI));
        bool writable = (event.events & EPOLLOUT);
        bool error = (event.events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP));

        ProcessEvents(pdispatcher, readable, writable, error, error);
      }
    }

    if (cmsWait != kForeverMs) {
      msWait = TimeDiff(msStop, TimeMillis());
      if (msWait <= 0) {
        // Return success on timeout.
        return true;
      }
    }
  }

  return true;
}

bool EvenLoop::WaitPollOneDispatcher(int cmsWait, Dispatcher* dispatcher) {
  ABSL_DCHECK(dispatcher);
  int64_t msWait = -1;
  int64_t msStop = -1;
  if (cmsWait != kForeverMs) {
    msWait = cmsWait;
    msStop = TimeAfter(cmsWait);
  }

  fWait_ = true;
  const int fd = dispatcher->GetDescriptor();

  while (fWait_) {
    auto fds = DispatcherToPollfd(dispatcher);

    // Wait then call handlers as appropriate
    // < 0 means error
    // 0 means timeout
    // > 0 means count of descriptors ready
    int n = poll(&fds, 1, static_cast<int>(msWait));
    if (n < 0) {
      if (errno != EINTR) {
        ABSL_LOG_E(LS_ERROR, EN, errno) << "poll";
        return false;
      }
      // Else ignore the error and keep going. If this EINTR was for one of the
      // signals managed by this EvenLoop, the
      // PosixSignalDeliveryDispatcher will be in the signaled state in the next
      // iteration.
    } else if (n == 0) {
      // If timeout, return success
      return true;
    } else {
      // We have signaled descriptors (should only be the passed dispatcher).
      ABSL_DCHECK_EQ(n, 1);
      ABSL_DCHECK_EQ(fds.fd, fd);
      ProcessPollEvents(dispatcher, fds);
    }

    if (cmsWait != kForeverMs) {
      msWait = TimeDiff(msStop, TimeMillis());
      if (msWait < 0) {
        // Return success on timeout.
        return true;
      }
    }
  }

  return true;
}

#elif defined(__USE_POLL__)

bool EvenLoop::WaitPoll(int cmsWait, bool process_io) {
  int64_t msWait = -1;
  int64_t msStop = -1;
  if (cmsWait != kForeverMs) {
    msWait = cmsWait;
    msStop = TimeAfter(cmsWait);
  }

  std::vector<pollfd> pollfds;
  fWait_ = true;

  while (fWait_) {
    {
      CritScope cr(&crit_);
      current_dispatcher_keys_.clear();
      pollfds.clear();
      pollfds.reserve(dispatcher_by_key_.size());

      for (auto const& kv : dispatcher_by_key_) {
        uint64_t key = kv.first;
        Dispatcher* pdispatcher = kv.second;
        if (!process_io && (pdispatcher != signal_wakeup_)) {
          continue;
        }
        current_dispatcher_keys_.push_back(key);
        pollfds.push_back(DispatcherToPollfd(pdispatcher));
      }
    }

    // Wait then call handlers as appropriate
    // < 0 means error
    // 0 means timeout
    // > 0 means count of descriptors ready
    int n = poll(pollfds.data(), pollfds.size(), static_cast<int>(msWait));
    if (n < 0) {
      if (errno != EINTR) {
        ABSL_LOG_E(LS_ERROR, EN, errno) << "poll";
        return false;
      }
      // Else ignore the error and keep going. If this EINTR was for one of the
      // signals managed by this EvenLoop, the
      // PosixSignalDeliveryDispatcher will be in the signaled state in the next
      // iteration.
    } else if (n == 0) {
      // If timeout, return success
      return true;
    } else {
      // We have signaled descriptors
      CritScope cr(&crit_);
      // Iterate only on the dispatchers whose file descriptors were passed into
      // poll; this avoids the ABA problem (a socket being destroyed and a new
      // one created with the same file descriptor).
      for (size_t i = 0; i < current_dispatcher_keys_.size(); ++i) {
        uint64_t key = current_dispatcher_keys_[i];
        if (!dispatcher_by_key_.count(key)) {
          continue;
        }
        ProcessPollEvents(dispatcher_by_key_.at(key), pollfds[i]);
      }
    }

    if (cmsWait != kForeverMs) {
      msWait = TimeDiff(msStop, TimeMillis());
      if (msWait < 0) {
        // Return success on timeout.
        return true;
      }
    }
  }

  return true;
}

#endif  // __USE_EPOLL__, __USE_POLL__
#endif  // __OS_POSIX__

#if defined(__OS_WIN__)
bool EvenLoop::Wait(absl::Duration max_wait_duration, bool process_io) {
  // We don't support reentrant waiting.
  ABSL_DCHECK(!waiting_);
  ScopedSetTrue s(&waiting_);

  int cmsWait = ToCmsWait(max_wait_duration);
  int64_t cmsTotal = cmsWait;
  int64_t cmsElapsed = 0;
  int64_t msStart = Time();

  fWait_ = true;
  while (fWait_) {
    std::vector<WSAEVENT> events;
    std::vector<uint64_t> event_owners;

    events.push_back(socket_ev_);

    {
      CritScope cr(&crit_);
      // Get a snapshot of all current dispatchers; this is used to avoid the
      // ABA problem (see later comment) and avoids the dispatcher_by_key_
      // iterator being invalidated by calling CheckSignalClose, which may
      // remove the dispatcher from the list.
      current_dispatcher_keys_.clear();
      for (auto const& kv : dispatcher_by_key_) {
        current_dispatcher_keys_.push_back(kv.first);
      }
      for (uint64_t key : current_dispatcher_keys_) {
        if (!dispatcher_by_key_.count(key)) {
          continue;
        }
        Dispatcher* disp = dispatcher_by_key_.at(key);
        if (!disp) {
          continue;
        }
        if (!process_io && (disp != signal_wakeup_)) {
          continue;
        }
        SOCKET s = disp->GetSocket();
        if (disp->CheckSignalClose()) {
          // We just signalled close, don't poll this socket.
        } else if (s != INVALID_SOCKET) {
          WSAEventSelect(s, events[0],
                         FlagsToEvents(disp->GetRequestedEvents()));
        } else {
          events.push_back(disp->GetWSAEvent());
          event_owners.push_back(key);
        }
      }
    }

    // Which is shorter, the delay wait or the asked wait?

    int64_t cmsNext;
    if (cmsWait == kForeverMs) {
      cmsNext = cmsWait;
    } else {
      cmsNext = std::max<int64_t>(0, cmsTotal - cmsElapsed);
    }

    // Wait for one of the events to signal
    DWORD dw =
        WSAWaitForMultipleEvents(static_cast<DWORD>(events.size()), &events[0],
                                 false, static_cast<DWORD>(cmsNext), false);

    if (dw == WSA_WAIT_FAILED) {
      // Failed?
      // TODO(pthatcher): need a better strategy than this!
      WSAGetLastError();
      // ABSL_DCHECK_NOTREACHED();
      return false;
    } else if (dw == WSA_WAIT_TIMEOUT) {
      // Timeout?
      return true;
    } else {
      // Figure out which one it is and call it
      CritScope cr(&crit_);
      int index = dw - WSA_WAIT_EVENT_0;
      if (index > 0) {
        --index;  // The first event is the socket event
        uint64_t key = event_owners[index];
        if (!dispatcher_by_key_.count(key)) {
          // The dispatcher could have been removed while waiting for events.
          continue;
        }
        Dispatcher* disp = dispatcher_by_key_.at(key);
        disp->OnEvent(0, 0);
      } else if (process_io) {
        // Iterate only on the dispatchers whose sockets were passed into
        // WSAEventSelect; this avoids the ABA problem (a socket being
        // destroyed and a new one created with the same SOCKET handle).
        for (uint64_t key : current_dispatcher_keys_) {
          if (!dispatcher_by_key_.count(key)) {
            continue;
          }
          Dispatcher* disp = dispatcher_by_key_.at(key);
          SOCKET s = disp->GetSocket();
          if (s == INVALID_SOCKET) {
            continue;
          }

          WSANETWORKEVENTS wsaEvents;
          int err = WSAEnumNetworkEvents(s, events[0], &wsaEvents);
          if (err == 0) {
            {
              if ((wsaEvents.lNetworkEvents & FD_READ) &&
                  wsaEvents.iErrorCode[FD_READ_BIT] != 0) {
                LOG(WARNING) << "PhysicalSocketServer got FD_READ_BIT error "
                             << wsaEvents.iErrorCode[FD_READ_BIT];
              }
              if ((wsaEvents.lNetworkEvents & FD_WRITE) &&
                  wsaEvents.iErrorCode[FD_WRITE_BIT] != 0) {
                LOG(WARNING) << "PhysicalSocketServer got FD_WRITE_BIT error "
                             << wsaEvents.iErrorCode[FD_WRITE_BIT];
              }
              if ((wsaEvents.lNetworkEvents & FD_CONNECT) &&
                  wsaEvents.iErrorCode[FD_CONNECT_BIT] != 0) {
                LOG(WARNING) << "PhysicalSocketServer got FD_CONNECT_BIT error "
                             << wsaEvents.iErrorCode[FD_CONNECT_BIT];
              }
              if ((wsaEvents.lNetworkEvents & FD_ACCEPT) &&
                  wsaEvents.iErrorCode[FD_ACCEPT_BIT] != 0) {
                LOG(WARNING) << "PhysicalSocketServer got FD_ACCEPT_BIT error "
                             << wsaEvents.iErrorCode[FD_ACCEPT_BIT];
              }
              if ((wsaEvents.lNetworkEvents & FD_CLOSE) &&
                  wsaEvents.iErrorCode[FD_CLOSE_BIT] != 0) {
                LOG(WARNING) << "PhysicalSocketServer got FD_CLOSE_BIT error "
                             << wsaEvents.iErrorCode[FD_CLOSE_BIT];
              }
            }
            uint32_t ff = 0;
            int errcode = 0;
            if (wsaEvents.lNetworkEvents & FD_READ) {
              ff |= DE_READ;
            }
            if (wsaEvents.lNetworkEvents & FD_WRITE) {
              ff |= DE_WRITE;
            }
            if (wsaEvents.lNetworkEvents & FD_CONNECT) {
              if (wsaEvents.iErrorCode[FD_CONNECT_BIT] == 0) {
                ff |= DE_CONNECT;
              } else {
                ff |= DE_CLOSE;
                errcode = wsaEvents.iErrorCode[FD_CONNECT_BIT];
              }
            }
            if (wsaEvents.lNetworkEvents & FD_ACCEPT) {
              ff |= DE_ACCEPT;
            }
            if (wsaEvents.lNetworkEvents & FD_CLOSE) {
              ff |= DE_CLOSE;
              errcode = wsaEvents.iErrorCode[FD_CLOSE_BIT];
            }
            if (ff != 0) {
              disp->OnEvent(ff, errcode);
            }
          }
        }
      }

      // Reset the network event until new activity occurs
      WSAResetEvent(socket_ev_);
    }

    // Break?
    if (!fWait_) {
      break;
    }
    cmsElapsed = TimeSince(msStart);
    if ((cmsWait != kForeverMs) && (cmsElapsed >= cmsWait)) {
      break;
    }
  }

  // Done
  return true;
}
#endif  // __OS_WIN__

}  // namespace base
