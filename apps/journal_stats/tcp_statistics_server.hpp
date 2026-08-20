#pragma once

#include "statistics.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace journal_stats {

enum class ServerRunResult {
    Stopped,
    ListenError,
    PollError,
};

class TcpStatisticsServer final {
public:
    TcpStatisticsServer(std::string bindAddress, std::uint16_t port,
                        std::size_t everyN, std::chrono::seconds interval,
                        std::ostream& output, std::ostream& errorOutput);

    TcpStatisticsServer(const TcpStatisticsServer&) = delete;
    TcpStatisticsServer& operator=(const TcpStatisticsServer&) = delete;

    [[nodiscard]] ServerRunResult run();
    void requestStop() noexcept;

    [[nodiscard]] std::uint16_t boundPort() const noexcept;
    [[nodiscard]] std::uint64_t processedMessages() const noexcept;

private:
    void processLine(std::string_view line);
    void printStatistics(const StatisticsSnapshot& snapshot,
                         StatisticsClock::time_point now);

    std::string bindAddress_;
    std::uint16_t requestedPort_;
    std::ostream& output_;
    std::ostream& errorOutput_;
    MessageStatistics statistics_;
    StatisticsReporter reporter_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<std::uint16_t> boundPort_{0};
    std::atomic<std::uint64_t> processedMessages_{0};
};

void installTerminationHandlers() noexcept;
[[nodiscard]] bool terminationRequested() noexcept;

}  // namespace journal_stats
