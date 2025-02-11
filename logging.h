#ifndef BASE_LOGGING_H_
#define BASE_LOGGING_H_

#include "absl/log/log.h"
#include "absl/log/log_sink.h"

namespace base {

enum LogSeverity {
  INFO = 0,
  WARNING = 1,
  ERROR = 2,
  FATAL = 3,
};

class BasicLogSink : public absl::LogSink {
 public:
  virtual ~BasicLogSink() = default;
  // Called when |message| is emitted at |level|.
  virtual void OnLogMessage(LogSeverity level, absl::string_view message) = 0;

 protected:
  void Send(const absl::LogEntry& entry);
};

void InitializeLog(LogSeverity severity, BasicLogSink* log_sink);

void DeInitializeLog();

}  // namespace base

#endif
