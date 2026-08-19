#include "journal/file_logger.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

std::atomic<int> failures{0};

namespace {

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            std::cerr << "FAILED: " << __func__ << ':' << __LINE__ << ": "    \
                      << #condition << '\n';                                    \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

std::string readFile(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::size_t lineCount(const std::string& text) {
    std::size_t count = 0;
    for (const char character : text) {
        if (character == '\n') {
            ++count;
        }
    }
    return count;
}

void testWritesRequiredFields(const fs::path& directory) {
    const fs::path path = directory / "required-fields.log";
    journal::FileLogger logger(path.string(), journal::Level::Debug);

    CHECK(logger.isReady());
    CHECK(logger.log("service started", journal::Level::Info) ==
          journal::WriteResult::Written);

    const std::string content = readFile(path);
    CHECK(content.find("[INFO] service started") != std::string::npos);
    CHECK(content.size() >= 26U);
    CHECK(!content.empty() && content.front() == '[');
    CHECK(content.find('T') != std::string::npos);
    CHECK(content.find('Z') != std::string::npos);
}

void testFiltersLowerLevels(const fs::path& directory) {
    const fs::path path = directory / "filter.log";
    journal::FileLogger logger(path.string(), journal::Level::Info);

    CHECK(logger.log("must be filtered", journal::Level::Debug) ==
          journal::WriteResult::Filtered);
    CHECK(logger.log("must be written", journal::Level::Info) ==
          journal::WriteResult::Written);

    const std::string content = readFile(path);
    CHECK(content.find("must be filtered") == std::string::npos);
    CHECK(content.find("must be written") != std::string::npos);
}

void testChangesDefaultLevel(const fs::path& directory) {
    const fs::path path = directory / "level-change.log";
    journal::FileLogger logger(path.string(), journal::Level::Error);

    CHECK(logger.defaultLevel() == journal::Level::Error);
    CHECK(logger.log("implicit error") == journal::WriteResult::Written);
    CHECK(logger.log("filtered debug", journal::Level::Debug) ==
          journal::WriteResult::Filtered);
    CHECK(logger.setDefaultLevel(journal::Level::Debug));
    CHECK(logger.defaultLevel() == journal::Level::Debug);
    CHECK(logger.log("implicit debug") == journal::WriteResult::Written);

    const std::string content = readFile(path);
    CHECK(content.find("[ERROR] implicit error") != std::string::npos);
    CHECK(content.find("filtered debug") == std::string::npos);
    CHECK(content.find("[DEBUG] implicit debug") != std::string::npos);
}

void testAppendsAndEscapesMessages(const fs::path& directory) {
    const fs::path path = directory / "append.log";
    {
        journal::FileLogger logger(path.string(), journal::Level::Debug);
        CHECK(logger.log("first") == journal::WriteResult::Written);
    }
    {
        journal::FileLogger logger(path.string(), journal::Level::Debug);
        CHECK(logger.log("second\nline\\tail") ==
              journal::WriteResult::Written);
    }

    const std::string content = readFile(path);
    CHECK(content.find("first") != std::string::npos);
    CHECK(content.find("second\\nline\\\\tail") != std::string::npos);
    CHECK(lineCount(content) == 2U);
}

void testReportsInvalidStateAndLevel(const fs::path& directory) {
    const fs::path missingDirectory = directory / "missing" / "journal.log";
    journal::FileLogger logger(missingDirectory.string(), journal::Level::Info);

    CHECK(!logger.isReady());
    CHECK(logger.log("message", journal::Level::Info) ==
          journal::WriteResult::NotReady);

    const auto invalidLevel = static_cast<journal::Level>(255);
    CHECK(!logger.setDefaultLevel(invalidLevel));
    CHECK(logger.log("message", invalidLevel) ==
          journal::WriteResult::InvalidLevel);

    const fs::path validPath = directory / "invalid-initial-level.log";
    journal::FileLogger invalidLogger(validPath.string(), invalidLevel);
    CHECK(!invalidLogger.isReady());
    CHECK(invalidLogger.log("message", journal::Level::Info) ==
          journal::WriteResult::NotReady);
    CHECK(invalidLogger.setDefaultLevel(journal::Level::Info));
    CHECK(invalidLogger.isReady());
    CHECK(invalidLogger.log("recovered") == journal::WriteResult::Written);
}

void testConcurrentWrites(const fs::path& directory) {
    const fs::path path = directory / "concurrent.log";
    journal::FileLogger logger(path.string(), journal::Level::Debug);

    constexpr int threadCount = 4;
    constexpr int messagesPerThread = 100;
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
        threads.emplace_back([&logger, threadIndex]() {
            for (int messageIndex = 0; messageIndex < messagesPerThread;
                 ++messageIndex) {
                const std::string message =
                    "thread=" + std::to_string(threadIndex) +
                    " message=" + std::to_string(messageIndex);
                CHECK(logger.log(message, journal::Level::Info) ==
                      journal::WriteResult::Written);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    CHECK(lineCount(readFile(path)) ==
          static_cast<std::size_t>(threadCount * messagesPerThread));
}

}  // namespace

void runSocketLoggerTests();

int main() {
    const fs::path testDirectory =
        fs::current_path() / "journal-test-output";
    std::error_code error;
    fs::remove_all(testDirectory, error);
    error.clear();
    fs::create_directories(testDirectory, error);
    if (error) {
        std::cerr << "Cannot create test directory: " << error.message()
                  << '\n';
        return 2;
    }

    testWritesRequiredFields(testDirectory);
    testFiltersLowerLevels(testDirectory);
    testChangesDefaultLevel(testDirectory);
    testAppendsAndEscapesMessages(testDirectory);
    testReportsInvalidStateAndLevel(testDirectory);
    testConcurrentWrites(testDirectory);
    runSocketLoggerTests();

    fs::remove_all(testDirectory, error);

    const int failureCount = failures.load();
    if (failureCount != 0) {
        std::cerr << failureCount << " check(s) failed\n";
        return 1;
    }

    std::cout << "All journal tests passed\n";
    return 0;
}
