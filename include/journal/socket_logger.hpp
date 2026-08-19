#pragma once

#include "journal/logger.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace journal {

// Sends newline-delimited journal records to a TCP server.
class SocketLogger final : public ILogger {
public:
    // Resolves host and establishes the TCP connection immediately.
    // Check isReady() before the first write.
    SocketLogger(std::string host, std::uint16_t port,
                 Level defaultLevel) noexcept;
    ~SocketLogger() override;

    SocketLogger(const SocketLogger&) = delete;
    SocketLogger& operator=(const SocketLogger&) = delete;
    SocketLogger(SocketLogger&&) = delete;
    SocketLogger& operator=(SocketLogger&&) = delete;

    [[nodiscard]] WriteResult log(std::string_view message) noexcept override;
    [[nodiscard]] WriteResult log(std::string_view message,
                                  Level level) noexcept override;

    [[nodiscard]] bool setDefaultLevel(Level level) noexcept override;
    [[nodiscard]] Level defaultLevel() const noexcept override;
    [[nodiscard]] bool isReady() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace journal
