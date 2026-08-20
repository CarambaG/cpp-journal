#pragma once

#include "journal/logger.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

namespace journal_stats {

using StatisticsClock = std::chrono::steady_clock;

struct StatisticsSnapshot {
    std::uint64_t totalMessages{0};
    std::array<std::uint64_t, 3> messagesByLevel{};
    std::uint64_t messagesLastHour{0};
    std::size_t minimumLength{0};
    std::size_t maximumLength{0};
    double averageLength{0.0};
    std::uint64_t revision{0};
};

class MessageStatistics final {
public:
    void add(journal::Level level, std::size_t messageLength,
             StatisticsClock::time_point receivedAt);

    [[nodiscard]] StatisticsSnapshot snapshot(
        StatisticsClock::time_point now);

    [[nodiscard]] std::optional<StatisticsClock::time_point> nextExpiration()
        const;

private:
    void removeExpired(StatisticsClock::time_point now);

    mutable std::mutex mutex_;
    std::uint64_t totalMessages_{0};
    std::array<std::uint64_t, 3> messagesByLevel_{};
    std::deque<StatisticsClock::time_point> recentMessages_;
    std::size_t minimumLength_{0};
    std::size_t maximumLength_{0};
    long double totalLength_{0.0L};
    std::uint64_t revision_{0};
};

class StatisticsReporter final {
public:
    StatisticsReporter(std::size_t everyN,
                       std::chrono::seconds interval,
                       StatisticsClock::time_point startedAt) noexcept;

    [[nodiscard]] bool shouldPrintAfterMessage(
        const StatisticsSnapshot& snapshot,
        StatisticsClock::time_point now) const noexcept;

    [[nodiscard]] bool shouldPrintOnTimer(
        const StatisticsSnapshot& snapshot,
        StatisticsClock::time_point now) const noexcept;

    [[nodiscard]] std::optional<StatisticsClock::time_point> nextDeadline(
        const StatisticsSnapshot& snapshot) const noexcept;

    void markPrinted(const StatisticsSnapshot& snapshot,
                     StatisticsClock::time_point now) noexcept;

    [[nodiscard]] bool hasUnprintedChanges(
        const StatisticsSnapshot& snapshot) const noexcept;

private:
    std::size_t everyN_;
    std::chrono::seconds interval_;
    StatisticsClock::time_point lastPrintedAt_;
    std::uint64_t lastPrintedRevision_{0};
};

[[nodiscard]] std::string formatStatistics(
    const StatisticsSnapshot& snapshot);

}  // namespace journal_stats
