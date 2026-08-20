#include "tcp_statistics_server.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

bool parseUnsigned(std::string_view text, std::uint64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }

    std::uint64_t parsed = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

void printUsage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <bind-address> <port> <N> <T-seconds>\n"
              << "Example: " << executable << " 0.0.0.0 9000 10 5\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 5) {
        printUsage(argv[0]);
        return 1;
    }

    std::uint64_t portValue = 0;
    std::uint64_t everyNValue = 0;
    std::uint64_t timeoutValue = 0;
    if (!parseUnsigned(argv[2], portValue) || portValue == 0U ||
        portValue > std::numeric_limits<std::uint16_t>::max()) {
        std::cerr << "Port must be between 1 and 65535\n";
        return 2;
    }
    if (!parseUnsigned(argv[3], everyNValue) || everyNValue == 0U ||
        everyNValue > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "N must be a positive integer\n";
        return 3;
    }
    if (!parseUnsigned(argv[4], timeoutValue) || timeoutValue == 0U ||
        timeoutValue >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::chrono::seconds::rep>::max())) {
        std::cerr << "T must be a positive number of seconds\n";
        return 4;
    }

    journal_stats::installTerminationHandlers();
    journal_stats::TcpStatisticsServer server(
        argv[1], static_cast<std::uint16_t>(portValue),
        static_cast<std::size_t>(everyNValue),
        std::chrono::seconds(static_cast<std::chrono::seconds::rep>(
            timeoutValue)),
        std::cout, std::cerr);

    const journal_stats::ServerRunResult result = server.run();
    switch (result) {
        case journal_stats::ServerRunResult::Stopped:
            return 0;
        case journal_stats::ServerRunResult::ListenError:
            return 5;
        case journal_stats::ServerRunResult::PollError:
            return 6;
    }
    return 7;
}
