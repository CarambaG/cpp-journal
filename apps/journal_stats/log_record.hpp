#pragma once

#include "journal/logger.hpp"

#include <string>
#include <string_view>

namespace journal_stats {

struct LogRecord {
    std::string timestamp;
    journal::Level level{journal::Level::Info};
    std::string message;
};

enum class ParseRecordResult {
    Parsed,
    InvalidFormat,
    InvalidLevel,
    InvalidEscapeSequence,
};

// Parses the newline-delimited wire format produced by SocketLogger:
// [timestamp] [LEVEL] escaped message
[[nodiscard]] ParseRecordResult parseLogRecord(std::string_view line,
                                               LogRecord& record);

[[nodiscard]] const char* toString(ParseRecordResult result) noexcept;

}  // namespace journal_stats
