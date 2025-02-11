#include "logging.h"

#include "absl/flags/flag.h"
#include "absl/log/initialize.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/string_view.h"

#ifndef ABSL_VLOG
ABSL_FLAG(int, v, 0, "Show all QUICHE_VLOG(m) messages for m <= this.");
#endif

namespace base {

static absl::LogSink* g_log_sink_ = nullptr;

void BasicLogSink::Send(const absl::LogEntry& entry) {
  switch (entry.log_severity()) {
    case absl::LogSeverity::kInfo:
      OnLogMessage(INFO, entry.text_message());
      break;
    case absl::LogSeverity::kWarning:
      OnLogMessage(WARNING, entry.text_message());
      break;

    case absl::LogSeverity::kError:
      OnLogMessage(ERROR, entry.text_message());
      break;

    case absl::LogSeverity::kFatal:
      OnLogMessage(FATAL, entry.text_message());
      break;
    default:
      OnLogMessage(INFO, entry.text_message());
      break;
  }
}

void DeInitializeLog() {
  if (g_log_sink_ != nullptr) {
    absl::RemoveLogSink(g_log_sink_);
  }
}

void InitializeLog(LogSeverity severity, BasicLogSink* log_sink) {
  absl::InitializeLog();
  // absl::SetStderrThreshold(absl::LogSeverity(severity));
  if (log_sink && !g_log_sink_) {
    g_log_sink_ = log_sink;
    absl::AddLogSink(log_sink);
  }
}

}  // namespace base
