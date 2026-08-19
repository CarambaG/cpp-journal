#pragma once

#include "journal/logger.hpp"

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace journal {

class FileLogger final : public ILogger {
public:
    // Opens the file in append mode. Check isReady() before the first write.
    FileLogger(std::string filename, Level defaultLevel) noexcept;
    ~FileLogger() override;

    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;
    FileLogger(FileLogger&&) = delete;
    FileLogger& operator=(FileLogger&&) = delete;

    [[nodiscard]] WriteResult log(std::string_view message) noexcept override;
    [[nodiscard]] WriteResult log(std::string_view message,
                                  Level level) noexcept override;

    [[nodiscard]] bool setDefaultLevel(Level level) noexcept override;
    [[nodiscard]] Level defaultLevel() const noexcept override;
    [[nodiscard]] bool isReady() const noexcept override;

private:
    [[nodiscard]] static bool isValidLevel(Level level) noexcept;
    [[nodiscard]] static std::string makeTimestamp() noexcept;
    [[nodiscard]] static std::string escapeMessage(std::string_view message);

    mutable std::mutex streamMutex_;
    std::ofstream stream_;
    std::atomic<Level> defaultLevel_;
    std::atomic<bool> configurationValid_;
};

}  // namespace journal
