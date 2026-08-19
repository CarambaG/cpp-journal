#include "input_parser.hpp"
#include "journal_application.hpp"

#include <atomic>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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

bool isValidLevel(journal::Level level) noexcept {
    switch (level) {
        case journal::Level::Debug:
        case journal::Level::Info:
        case journal::Level::Error:
            return true;
    }
    return false;
}

struct RecordedMessage {
    std::string text;
    journal::Level level{journal::Level::Info};
    bool usedDefaultLevel{false};
    std::thread::id writerThread;
};

class RecordingLogger final : public journal::ILogger {
public:
    [[nodiscard]] journal::WriteResult log(
        std::string_view message) noexcept override {
        return record(message, defaultLevel(), true);
    }

    [[nodiscard]] journal::WriteResult log(
        std::string_view message, journal::Level level) noexcept override {
        if (!isValidLevel(level)) {
            return journal::WriteResult::InvalidLevel;
        }
        return record(message, level, false);
    }

    [[nodiscard]] bool setDefaultLevel(
        journal::Level level) noexcept override {
        if (!isValidLevel(level)) {
            return false;
        }
        defaultLevel_.store(level, std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] journal::Level defaultLevel() const noexcept override {
        return defaultLevel_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool isReady() const noexcept override {
        return true;
    }

    void setResult(journal::WriteResult result) noexcept {
        result_.store(result, std::memory_order_relaxed);
    }

    [[nodiscard]] std::vector<RecordedMessage> records() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_;
    }

private:
    journal::WriteResult record(std::string_view message,
                                journal::Level level,
                                bool usedDefaultLevel) noexcept {
        const journal::WriteResult result =
            result_.load(std::memory_order_relaxed);
        if (result != journal::WriteResult::Written) {
            return result;
        }

        try {
            std::lock_guard<std::mutex> lock(mutex_);
            records_.push_back({std::string(message), level, usedDefaultLevel,
                                std::this_thread::get_id()});
            return journal::WriteResult::Written;
        } catch (...) {
            return journal::WriteResult::WriteError;
        }
    }

    mutable std::mutex mutex_;
    std::vector<RecordedMessage> records_;
    std::atomic<journal::Level> defaultLevel_{journal::Level::Info};
    std::atomic<journal::WriteResult> result_{journal::WriteResult::Written};
};

void testLevelParsing() {
    CHECK(journal_app::parseLevelName("debug") == journal::Level::Debug);
    CHECK(journal_app::parseLevelName(" INFO ") == journal::Level::Info);
    CHECK(journal_app::parseLevelName("ErRoR") == journal::Level::Error);
    CHECK(!journal_app::parseLevelName("warning").has_value());
}

void testInputParsing() {
    const journal_app::ParsedInput implicit =
        journal_app::parseInput(" service started ");
    CHECK(implicit.kind == journal_app::InputKind::Message);
    CHECK(implicit.message.text == "service started");
    CHECK(!implicit.message.level.has_value());

    const journal_app::ParsedInput explicitLevel =
        journal_app::parseInput(" [ERROR] connection failed ");
    CHECK(explicitLevel.kind == journal_app::InputKind::Message);
    CHECK(explicitLevel.message.text == "connection failed");
    CHECK(explicitLevel.message.level == journal::Level::Error);

    CHECK(journal_app::parseInput("   ").kind ==
          journal_app::InputKind::Empty);
    CHECK(journal_app::parseInput("/help").kind ==
          journal_app::InputKind::Help);
    CHECK(journal_app::parseInput("/quit").kind ==
          journal_app::InputKind::Quit);
    CHECK(journal_app::parseInput("/exit").kind ==
          journal_app::InputKind::Quit);
    CHECK(journal_app::parseInput("[warning] message").kind ==
          journal_app::InputKind::Invalid);
    CHECK(journal_app::parseInput("[info]").kind ==
          journal_app::InputKind::Invalid);
    CHECK(journal_app::parseInput("[error message").kind ==
          journal_app::InputKind::Invalid);
}

void testBackgroundWriterPreservesOrderAndDrainsQueue() {
    RecordingLogger logger;
    std::ostringstream errors;
    const std::thread::id inputThread = std::this_thread::get_id();

    journal_app::JournalApplication application(logger, errors);
    CHECK(application.submit({"first", std::nullopt}));
    CHECK(application.submit({"second", journal::Level::Error}));
    CHECK(application.submit({"third", journal::Level::Debug}));
    application.stop();

    const std::vector<RecordedMessage> records = logger.records();
    CHECK(records.size() == 3U);
    if (records.size() == 3U) {
        CHECK(records[0].text == "first");
        CHECK(records[0].usedDefaultLevel);
        CHECK(records[0].level == journal::Level::Info);
        CHECK(records[1].text == "second");
        CHECK(!records[1].usedDefaultLevel);
        CHECK(records[1].level == journal::Level::Error);
        CHECK(records[2].text == "third");
        CHECK(records[2].level == journal::Level::Debug);
        CHECK(records[0].writerThread != inputThread);
        CHECK(records[0].writerThread == records[1].writerThread);
        CHECK(records[1].writerThread == records[2].writerThread);
    }
    CHECK(errors.str().empty());
    CHECK(!application.submit({"after stop", std::nullopt}));
}

void testConcurrentSubmissionIsSafe() {
    RecordingLogger logger;
    std::ostringstream errors;
    journal_app::JournalApplication application(logger, errors);

    constexpr int producerCount = 4;
    constexpr int messagesPerProducer = 50;
    std::vector<std::thread> producers;
    producers.reserve(producerCount);

    for (int producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back(
            [&application, producer, messagesPerProducer]() {
                for (int index = 0; index < messagesPerProducer; ++index) {
                    const std::string text =
                        "producer=" + std::to_string(producer) +
                        " message=" + std::to_string(index);
                    CHECK(application.submit({text, journal::Level::Info}));
                }
            });
    }

    for (std::thread& producer : producers) {
        producer.join();
    }
    application.stop();

    CHECK(logger.records().size() ==
          static_cast<std::size_t>(producerCount * messagesPerProducer));
    CHECK(errors.str().empty());
}

void testWriteErrorsAreReported() {
    RecordingLogger logger;
    logger.setResult(journal::WriteResult::WriteError);
    std::ostringstream errors;

    journal_app::JournalApplication application(logger, errors);
    CHECK(application.submit({"cannot write", journal::Level::Info}));
    application.stop();

    CHECK(application.hasWriteErrors());
    CHECK(errors.str().find("write error") != std::string::npos);
}

}  // namespace

int main() {
    testLevelParsing();
    testInputParsing();
    testBackgroundWriterPreservesOrderAndDrainsQueue();
    testConcurrentSubmissionIsSafe();
    testWriteErrorsAreReported();

    const int failureCount = failures.load(std::memory_order_relaxed);
    if (failureCount != 0) {
        std::cerr << failureCount << " check(s) failed\n";
        return 1;
    }

    std::cout << "All journal CLI tests passed\n";
    return 0;
}
