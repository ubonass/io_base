#include "event_loop.h"
#include <errno.h>

#if defined(WEBRTC_LINUX)
#include <linux/sockios.h>
#endif

#if defined(__OS_WIN__)
#define LAST_SYSTEM_ERROR (::GetLastError())
#elif defined(__native_client__) && __native_client__
#define LAST_SYSTEM_ERROR (0)
#elif defined(__OS_POSIX__)
#define LAST_SYSTEM_ERROR (errno)
#endif  // __OS_WIN__

namespace base {

EvenLoop::EvenLoop() {
  // TODO
}

EvenLoop::~EvenLoop() {
  // TODO
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
    ABSL_LOG(ERROR) << "pipe failed";
  }
#else
  afd_[0] = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (afd_[0] < 0) {
    ABSL_LOG(ERROR) << "create event fd failed, errno:" << errno << ","
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

}  // namespace base
