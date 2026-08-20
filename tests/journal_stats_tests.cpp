#include "log_record.hpp"
#include "statistics.hpp"
#include "tcp_statistics_server.hpp"

#include "journal/socket_logger.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::atomic<int> failures{0};

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            std::cerr << "FAILED: " << __func__ << ':' << __LINE__ << ": "    \
                      << #condition << '\n';                                    \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

void testParsesSocketLoggerRecords() {
    journal_stats::LogRecord record;
    const auto result = journal_stats::parseLogRecord(
        "[2026-08-20T10:15:30.042Z] [ERROR] first\\nsecond\\\\tail",
        record);

    CHECK(result == journal_stats::ParseRecordResult::Parsed);
    CHECK(record.timestamp == "2026-08-20T10:15:30.042Z");
    CHECK(record.level == journal::Level::Error);
    CHECK(record.message == "first\nsecond\\tail");

    CHECK(journal_stats::parseLogRecord("bad record", record) ==
          journal_stats::ParseRecordResult::InvalidFormat);
    CHECK(journal_stats::parseLogRecord(
              "[time] [WARNING] message", record) ==
          journal_stats::ParseRecordResult::InvalidLevel);
    CHECK(journal_stats::parseLogRecord(
              "[time] [INFO] invalid\\q", record) ==
          journal_stats::ParseRecordResult::InvalidEscapeSequence);
}

void testCalculatesMessageStatistics() {
    journal_stats::MessageStatistics statistics;
    const auto startedAt = journal_stats::StatisticsClock::time_point{};

    statistics.add(journal::Level::Debug, 3U, startedAt);
    statistics.add(journal::Level::Info, 5U,
                   startedAt + std::chrono::minutes(10));
    statistics.add(journal::Level::Error, 7U,
                   startedAt + std::chrono::minutes(20));

    auto snapshot = statistics.snapshot(
        startedAt + std::chrono::minutes(30));
    CHECK(snapshot.totalMessages == 3U);
    CHECK(snapshot.messagesByLevel[0] == 1U);
    CHECK(snapshot.messagesByLevel[1] == 1U);
    CHECK(snapshot.messagesByLevel[2] == 1U);
    CHECK(snapshot.messagesLastHour == 3U);
    CHECK(snapshot.minimumLength == 3U);
    CHECK(snapshot.maximumLength == 7U);
    CHECK(std::abs(snapshot.averageLength - 5.0) < 0.0001);

    snapshot = statistics.snapshot(startedAt + std::chrono::minutes(71));
    CHECK(snapshot.totalMessages == 3U);
    CHECK(snapshot.messagesLastHour == 1U);
    CHECK(snapshot.minimumLength == 3U);
    CHECK(snapshot.maximumLength == 7U);
}

void testStatisticsOutputRules() {
    const auto startedAt = journal_stats::StatisticsClock::time_point{};
    journal_stats::StatisticsReporter reporter(
        3U, std::chrono::seconds(10), startedAt);

    journal_stats::StatisticsSnapshot snapshot;
    snapshot.totalMessages = 1U;
    snapshot.revision = 1U;
    CHECK(!reporter.shouldPrintAfterMessage(
        snapshot, startedAt + std::chrono::seconds(1)));
    CHECK(reporter.shouldPrintOnTimer(
        snapshot, startedAt + std::chrono::seconds(10)));

    reporter.markPrinted(snapshot, startedAt + std::chrono::seconds(10));
    CHECK(!reporter.hasUnprintedChanges(snapshot));

    snapshot.totalMessages = 3U;
    snapshot.revision = 3U;
    CHECK(reporter.shouldPrintAfterMessage(
        snapshot, startedAt + std::chrono::seconds(11)));
}

void testReceivesRecordsFromSocketLogger() {
    std::ostringstream output;
    std::ostringstream errors;
    journal_stats::TcpStatisticsServer server(
        "127.0.0.1", 0U, 2U, std::chrono::seconds(60), output, errors);

    journal_stats::ServerRunResult runResult =
        journal_stats::ServerRunResult::ListenError;
    std::thread serverThread([&server, &runResult] {
        runResult = server.run();
    });

    std::uint16_t port = 0;
    for (int attempt = 0; attempt < 200; ++attempt) {
        port = server.boundPort();
        if (port != 0U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(port != 0U);

    if (port != 0U) {
        {
            journal::SocketLogger logger("127.0.0.1", port,
                                         journal::Level::Debug);
            CHECK(logger.isReady());
            CHECK(logger.log("first", journal::Level::Info) ==
                  journal::WriteResult::Written);
        }
        {
            journal::SocketLogger logger("127.0.0.1", port,
                                         journal::Level::Debug);
            CHECK(logger.isReady());
            CHECK(logger.log("network\nmessage", journal::Level::Error) ==
                  journal::WriteResult::Written);
        }

        for (int attempt = 0; attempt < 200; ++attempt) {
            if (server.processedMessages() == 2U) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(server.processedMessages() == 2U);
    }

    server.requestStop();
    serverThread.join();

    CHECK(runResult == journal_stats::ServerRunResult::Stopped);
    CHECK(output.str().find("Received: ") != std::string::npos);
    CHECK(output.str().find("[ERROR] network\\nmessage") !=
          std::string::npos);
    CHECK(output.str().find("Total messages: 2") != std::string::npos);
    CHECK(output.str().find("INFO=1") != std::string::npos);
    CHECK(output.str().find("ERROR=1") != std::string::npos);
    CHECK(output.str().find("Last hour: 2") != std::string::npos);
    CHECK(errors.str().empty());
}

}  // namespace

int main() {
    testParsesSocketLoggerRecords();
    testCalculatesMessageStatistics();
    testStatisticsOutputRules();
    testReceivesRecordsFromSocketLogger();

    const int failureCount = failures.load();
    if (failureCount != 0) {
        std::cerr << failureCount << " check(s) failed\n";
        return 1;
    }

    std::cout << "All journal statistics tests passed\n";
    return 0;
}
