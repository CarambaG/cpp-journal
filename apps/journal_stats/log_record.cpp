#include "log_record.hpp"

#include <utility>

namespace journal_stats {
namespace {

bool parseLevel(std::string_view text, journal::Level& level) noexcept {
    if (text == "DEBUG") {
        level = journal::Level::Debug;
        return true;
    }
    if (text == "INFO") {
        level = journal::Level::Info;
        return true;
    }
    if (text == "ERROR") {
        level = journal::Level::Error;
        return true;
    }
    return false;
}

ParseRecordResult unescapeMessage(std::string_view escaped,
                                  std::string& message) {
    std::string result;
    result.reserve(escaped.size());

    for (std::size_t index = 0; index < escaped.size(); ++index) {
        const char character = escaped[index];
        if (character != '\\') {
            result.push_back(character);
            continue;
        }

        if (index + 1U >= escaped.size()) {
            return ParseRecordResult::InvalidEscapeSequence;
        }

        const char escapedCharacter = escaped[++index];
        switch (escapedCharacter) {
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case '\\':
                result.push_back('\\');
                break;
            default:
                return ParseRecordResult::InvalidEscapeSequence;
        }
    }

    message = std::move(result);
    return ParseRecordResult::Parsed;
}

}  // namespace

ParseRecordResult parseLogRecord(std::string_view line, LogRecord& record) {
    if (line.empty() || line.front() != '[') {
        return ParseRecordResult::InvalidFormat;
    }

    const std::size_t timestampEnd = line.find("] [", 1U);
    if (timestampEnd == std::string_view::npos || timestampEnd == 1U) {
        return ParseRecordResult::InvalidFormat;
    }

    const std::size_t levelStart = timestampEnd + 3U;
    const std::size_t levelEnd = line.find("] ", levelStart);
    if (levelEnd == std::string_view::npos) {
        return ParseRecordResult::InvalidFormat;
    }

    journal::Level level{};
    if (!parseLevel(line.substr(levelStart, levelEnd - levelStart), level)) {
        return ParseRecordResult::InvalidLevel;
    }

    std::string message;
    const ParseRecordResult unescapeResult =
        unescapeMessage(line.substr(levelEnd + 2U), message);
    if (unescapeResult != ParseRecordResult::Parsed) {
        return unescapeResult;
    }

    record.timestamp = std::string(line.substr(1U, timestampEnd - 1U));
    record.level = level;
    record.message = std::move(message);
    return ParseRecordResult::Parsed;
}

const char* toString(ParseRecordResult result) noexcept {
    switch (result) {
        case ParseRecordResult::Parsed:
            return "parsed";
        case ParseRecordResult::InvalidFormat:
            return "invalid record format";
        case ParseRecordResult::InvalidLevel:
            return "invalid record level";
        case ParseRecordResult::InvalidEscapeSequence:
            return "invalid escape sequence";
    }
    return "unknown parse result";
}

}  // namespace journal_stats
