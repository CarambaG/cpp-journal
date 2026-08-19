#include "journal/file_logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace journal {
namespace {

int severity(Level level) noexcept {
    return static_cast<int>(level);
}

}  // namespace

const char* toString(Level level) noexcept {
    switch (level) {
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

const char* toString(WriteResult result) noexcept {
    switch (result) {
        case WriteResult::Written:
            return "written";
        case WriteResult::Filtered:
            return "filtered";
        case WriteResult::InvalidLevel:
            return "invalid level";
        case WriteResult::NotReady:
            return "logger is not ready";
        case WriteResult::WriteError:
            return "write error";
    }
    return "unknown result";
}

FileLogger::FileLogger(std::string filename, Level defaultLevel) noexcept
    : defaultLevel_(isValidLevel(defaultLevel) ? defaultLevel : Level::Info),
      configurationValid_(isValidLevel(defaultLevel)) {
    stream_.open(std::move(filename), std::ios::out | std::ios::app);
}

FileLogger::~FileLogger() {
    std::lock_guard<std::mutex> lock(streamMutex_);
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }
}

WriteResult FileLogger::log(std::string_view message) noexcept {
    return log(message, defaultLevel());
}

WriteResult FileLogger::log(std::string_view message, Level level) noexcept {
    if (!isValidLevel(level)) {
        return WriteResult::InvalidLevel;
    }

    if (!configurationValid_.load(std::memory_order_relaxed)) {
        return WriteResult::NotReady;
    }

    if (severity(level) < severity(defaultLevel())) {
        return WriteResult::Filtered;
    }

    const std::string timestamp = makeTimestamp();
    const std::string escapedMessage = escapeMessage(message);

    std::lock_guard<std::mutex> lock(streamMutex_);
    if (!stream_.is_open()) {
        return WriteResult::NotReady;
    }

    stream_ << '[' << timestamp << "] [" << toString(level) << "] "
            << escapedMessage << '\n';
    stream_.flush();

    return stream_ ? WriteResult::Written : WriteResult::WriteError;
}

bool FileLogger::setDefaultLevel(Level level) noexcept {
    if (!isValidLevel(level)) {
        return false;
    }
    defaultLevel_.store(level, std::memory_order_relaxed);
    configurationValid_.store(true, std::memory_order_relaxed);
    return true;
}

Level FileLogger::defaultLevel() const noexcept {
    return defaultLevel_.load(std::memory_order_relaxed);
}

bool FileLogger::isReady() const noexcept {
    std::lock_guard<std::mutex> lock(streamMutex_);
    return configurationValid_.load(std::memory_order_relaxed) &&
           stream_.is_open() && stream_.good();
}

bool FileLogger::isValidLevel(Level level) noexcept {
    switch (level) {
        case Level::Debug:
        case Level::Info:
        case Level::Error:
            return true;
    }
    return false;
}

std::string FileLogger::makeTimestamp() noexcept {
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto millisecondsPart =
        duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t time = system_clock::to_time_t(now);

    std::tm utcTime{};
#if defined(_WIN32)
    // MSVC provides the bounds-checked variant with the destination first.
    if (gmtime_s(&utcTime, &time) != 0) {
#else
    // POSIX systems provide the reentrant variant.
    if (gmtime_r(&time, &utcTime) == nullptr) {
#endif
        return "0000-00-00T00:00:00.000Z";
    }

    std::ostringstream output;
    output << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setfill('0') << std::setw(3) << millisecondsPart.count()
           << 'Z';
    return output.str();
}

std::string FileLogger::escapeMessage(std::string_view message) {
    std::string result;
    result.reserve(message.size());

    for (const char character : message) {
        switch (character) {
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\\':
                result += "\\\\";
                break;
            default:
                result.push_back(character);
                break;
        }
    }
    return result;
}

}  // namespace journal
