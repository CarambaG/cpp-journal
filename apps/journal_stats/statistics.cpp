#include "statistics.hpp"

#include <iomanip>
#include <sstream>

namespace journal_stats {
namespace {

constexpr auto statisticsWindow = std::chrono::hours(1);

std::size_t levelIndex(journal::Level level) noexcept {
    switch (level) {
        case journal::Level::Debug:
            return 0U;
        case journal::Level::Info:
            return 1U;
        case journal::Level::Error:
            return 2U;
    }
    return 0U;
}

}  // namespace

void MessageStatistics::add(journal::Level level,
                            std::size_t messageLength,
                            StatisticsClock::time_point receivedAt) {
    std::lock_guard<std::mutex> lock(mutex_);
    removeExpired(receivedAt);

    ++totalMessages_;
    ++messagesByLevel_[levelIndex(level)];
    recentMessages_.push_back(receivedAt);

    if (totalMessages_ == 1U) {
        minimumLength_ = messageLength;
        maximumLength_ = messageLength;
    } else {
        if (messageLength < minimumLength_) {
            minimumLength_ = messageLength;
        }
        if (messageLength > maximumLength_) {
            maximumLength_ = messageLength;
        }
    }

    totalLength_ += static_cast<long double>(messageLength);
    ++revision_;
}

StatisticsSnapshot MessageStatistics::snapshot(
    StatisticsClock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    removeExpired(now);

    StatisticsSnapshot result;
    result.totalMessages = totalMessages_;
    result.messagesByLevel = messagesByLevel_;
    result.messagesLastHour =
        static_cast<std::uint64_t>(recentMessages_.size());
    result.minimumLength = minimumLength_;
    result.maximumLength = maximumLength_;
    result.averageLength = totalMessages_ == 0U
                               ? 0.0
                               : static_cast<double>(
                                     totalLength_ /
                                     static_cast<long double>(totalMessages_));
    result.revision = revision_;
    return result;
}

std::optional<StatisticsClock::time_point>
MessageStatistics::nextExpiration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (recentMessages_.empty()) {
        return std::nullopt;
    }
    return recentMessages_.front() + statisticsWindow;
}

void MessageStatistics::removeExpired(StatisticsClock::time_point now) {
    const auto threshold = now - statisticsWindow;
    bool changed = false;
    while (!recentMessages_.empty() &&
           recentMessages_.front() <= threshold) {
        recentMessages_.pop_front();
        changed = true;
    }
    if (changed) {
        ++revision_;
    }
}

StatisticsReporter::StatisticsReporter(
    std::size_t everyN, std::chrono::seconds interval,
    StatisticsClock::time_point startedAt) noexcept
    : everyN_(everyN == 0U ? 1U : everyN),
      interval_(interval <= std::chrono::seconds::zero()
                    ? std::chrono::seconds(1)
                    : interval),
      lastPrintedAt_(startedAt) {}

bool StatisticsReporter::shouldPrintAfterMessage(
    const StatisticsSnapshot& snapshot,
    StatisticsClock::time_point now) const noexcept {
    const bool nthMessage = snapshot.totalMessages != 0U &&
                            snapshot.totalMessages % everyN_ == 0U;
    return nthMessage || shouldPrintOnTimer(snapshot, now);
}

bool StatisticsReporter::shouldPrintOnTimer(
    const StatisticsSnapshot& snapshot,
    StatisticsClock::time_point now) const noexcept {
    return hasUnprintedChanges(snapshot) &&
           now >= lastPrintedAt_ + interval_;
}

std::optional<StatisticsClock::time_point>
StatisticsReporter::nextDeadline(
    const StatisticsSnapshot& snapshot) const noexcept {
    if (!hasUnprintedChanges(snapshot)) {
        return std::nullopt;
    }
    return lastPrintedAt_ + interval_;
}

void StatisticsReporter::markPrinted(
    const StatisticsSnapshot& snapshot,
    StatisticsClock::time_point now) noexcept {
    lastPrintedRevision_ = snapshot.revision;
    lastPrintedAt_ = now;
}

bool StatisticsReporter::hasUnprintedChanges(
    const StatisticsSnapshot& snapshot) const noexcept {
    return snapshot.revision != lastPrintedRevision_;
}

std::string formatStatistics(const StatisticsSnapshot& snapshot) {
    std::ostringstream output;
    output << "--- Statistics ---\n"
           << "Total messages: " << snapshot.totalMessages << '\n'
           << "By level: DEBUG=" << snapshot.messagesByLevel[0]
           << " INFO=" << snapshot.messagesByLevel[1]
           << " ERROR=" << snapshot.messagesByLevel[2] << '\n'
           << "Last hour: " << snapshot.messagesLastHour << '\n'
           << "Message length (bytes): min=" << snapshot.minimumLength
           << " max=" << snapshot.maximumLength << " avg=" << std::fixed
           << std::setprecision(2) << snapshot.averageLength << '\n';
    return output.str();
}

}  // namespace journal_stats
