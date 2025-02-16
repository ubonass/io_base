

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include "logging.h"

#if defined(_WIN32)
#include "event_loop_win.h"
#else
#include "event_loop_posix.h"
#endif

using namespace base;

class Test : public BasicLogSink {
 public:
  void Run() {
    event_loop_.reset(new EventLoopWin());
    loop_thread_.reset(new std::thread(&Test::RunLoop, this));
    while (true) {
      Sleep(1000);
      if (init_.load() && !start_.load()) {
        start_.store(true);
        event_loop_->PostTask([this]() {
          TestPostDelay();
          LOG(INFO) << " process task.... ";
        });
      }
    }
  }

  void RunLoop() {
    event_loop_->Begin();
    init_.store(true);
    while (true) {
      event_loop_->RunEventLoopOnce(absl::InfiniteDuration());
    }
    event_loop_->Quit();
  }

  void TestPostDelay() {
    int64_t nowMs = TimeMillis();
    if (last_time_ != -1) {
      LOG(INFO) << " time duration:" << (nowMs - last_time_);
    }
    last_time_ = nowMs;
    event_loop_->PostDelayedTask([this]() { TestPostDelay(); },
                                 absl::Milliseconds(16));
  }

 protected:
  void OnLogMessage(LogSeverity level, absl::string_view message) override {
    std::cout << message;
  }

 private:
  int64_t last_time_ = -1;
  std::atomic<bool> init_ = {false};
  std::atomic<bool> start_ = {false};
  std::unique_ptr<EventLoop> event_loop_;
  std::unique_ptr<std::thread> loop_thread_;
};

int main() {
  Test test;
  InitializeLog(LogSeverity::INFO, &test);
#if defined(WIN32)
  // Initialize Winsock
  WSADATA wsaData;
  auto nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (nResult != 0) {
    LOG(ERROR) << "WSAStartup failed: " << nResult;
    exit(1);
  }
#endif
  test.Run();
  DeInitializeLog();
#if defined(WIN32)
  WSACleanup();
#endif
}
