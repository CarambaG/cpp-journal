#include "tcp_statistics_server.hpp"

#include "log_record.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <limits>
#include <netdb.h>
#include <netinet/in.h>
#include <ostream>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace journal_stats {
namespace {

constexpr std::size_t maximumRecordSize = 1024U * 1024U;
constexpr int maximumPollIntervalMilliseconds = 250;
volatile std::sig_atomic_t terminationFlag = 0;

struct Client {
    int socket{-1};
    std::string buffer;
};

void terminationHandler(int) noexcept {
    terminationFlag = 1;
}

void closeSocket(int socket) noexcept {
    if (socket >= 0) {
        close(socket);
    }
}

int createListeningSocket(const std::string& bindAddress,
                          std::uint16_t requestedPort,
                          std::uint16_t& actualPort,
                          std::ostream& errorOutput) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const bool anyAddress = bindAddress.empty() || bindAddress == "*";
    if (anyAddress) {
        hints.ai_flags = AI_PASSIVE;
    }

    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(requestedPort);
    const int resolveResult =
        getaddrinfo(anyAddress ? nullptr : bindAddress.c_str(),
                    service.c_str(), &hints, &addresses);
    if (resolveResult != 0) {
        errorOutput << "Cannot resolve bind address: "
                    << gai_strerror(resolveResult) << '\n';
        return -1;
    }

    int listener = -1;
    for (addrinfo* current = addresses; current != nullptr;
         current = current->ai_next) {
        const int candidate = socket(current->ai_family,
                                     current->ai_socktype,
                                     current->ai_protocol);
        if (candidate < 0) {
            continue;
        }

        const int enabled = 1;
        if (setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, &enabled,
                       sizeof(enabled)) != 0) {
            closeSocket(candidate);
            continue;
        }

        if (bind(candidate, current->ai_addr,
                 static_cast<socklen_t>(current->ai_addrlen)) == 0 &&
            listen(candidate, SOMAXCONN) == 0) {
            listener = candidate;
            break;
        }
        closeSocket(candidate);
    }

    freeaddrinfo(addresses);
    if (listener < 0) {
        errorOutput << "Cannot bind or listen on " << bindAddress << ':'
                    << requestedPort << ": " << std::strerror(errno) << '\n';
        return -1;
    }

    sockaddr_storage localAddress{};
    socklen_t addressLength = sizeof(localAddress);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&localAddress),
                    &addressLength) != 0) {
        errorOutput << "Cannot determine listening port: "
                    << std::strerror(errno) << '\n';
        closeSocket(listener);
        return -1;
    }

    if (localAddress.ss_family == AF_INET) {
        const auto* address =
            reinterpret_cast<const sockaddr_in*>(&localAddress);
        actualPort = ntohs(address->sin_port);
    } else if (localAddress.ss_family == AF_INET6) {
        const auto* address =
            reinterpret_cast<const sockaddr_in6*>(&localAddress);
        actualPort = ntohs(address->sin6_port);
    } else {
        errorOutput << "Unsupported listening address family\n";
        closeSocket(listener);
        return -1;
    }

    return listener;
}

std::optional<StatisticsClock::time_point> earliestDeadline(
    std::optional<StatisticsClock::time_point> left,
    std::optional<StatisticsClock::time_point> right) {
    if (!left.has_value()) {
        return right;
    }
    if (!right.has_value()) {
        return left;
    }
    return std::min(*left, *right);
}

int pollTimeout(std::optional<StatisticsClock::time_point> deadline,
                StatisticsClock::time_point now) noexcept {
    if (!deadline.has_value()) {
        return maximumPollIntervalMilliseconds;
    }
    if (*deadline <= now) {
        return 0;
    }

    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now)
            .count();
    if (remaining <= 0) {
        return 1;
    }
    return static_cast<int>(std::min<std::int64_t>(
        remaining, maximumPollIntervalMilliseconds));
}

}  // namespace

TcpStatisticsServer::TcpStatisticsServer(
    std::string bindAddress, std::uint16_t port, std::size_t everyN,
    std::chrono::seconds interval, std::ostream& output,
    std::ostream& errorOutput)
    : bindAddress_(std::move(bindAddress)),
      requestedPort_(port),
      output_(output),
      errorOutput_(errorOutput),
      reporter_(everyN, interval, StatisticsClock::now()) {}

ServerRunResult TcpStatisticsServer::run() {
    std::uint16_t actualPort = 0;
    const int listener = createListeningSocket(
        bindAddress_, requestedPort_, actualPort, errorOutput_);
    if (listener < 0) {
        return ServerRunResult::ListenError;
    }

    boundPort_.store(actualPort, std::memory_order_release);
    output_ << "Listening on " << bindAddress_ << ':' << actualPort << '\n';

    std::vector<Client> clients;
    ServerRunResult result = ServerRunResult::Stopped;

    while (!stopRequested_.load(std::memory_order_relaxed) &&
           !terminationRequested()) {
        const auto now = StatisticsClock::now();
        StatisticsSnapshot currentSnapshot = statistics_.snapshot(now);
        if (reporter_.shouldPrintOnTimer(currentSnapshot, now)) {
            printStatistics(currentSnapshot, now);
            currentSnapshot = statistics_.snapshot(now);
        }

        const auto deadline = earliestDeadline(
            reporter_.nextDeadline(currentSnapshot),
            statistics_.nextExpiration());

        const std::size_t polledClientCount = clients.size();
        std::vector<pollfd> descriptors;
        descriptors.reserve(polledClientCount + 1U);
        descriptors.push_back({listener, POLLIN, 0});
        for (const Client& client : clients) {
            descriptors.push_back({client.socket, POLLIN, 0});
        }

        const int pollResult =
            poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()),
                 pollTimeout(deadline, now));
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            errorOutput_ << "poll failed: " << std::strerror(errno) << '\n';
            result = ServerRunResult::PollError;
            break;
        }

        if ((descriptors[0].revents & POLLIN) != 0) {
            const int clientSocket = accept(listener, nullptr, nullptr);
            if (clientSocket >= 0) {
                clients.push_back({clientSocket, {}});
            } else if (errno != EINTR && errno != EAGAIN &&
                       errno != EWOULDBLOCK) {
                errorOutput_ << "accept failed: " << std::strerror(errno)
                             << '\n';
            }
        }

        if ((descriptors[0].revents & (POLLERR | POLLNVAL)) != 0) {
            errorOutput_ << "Listening socket failed\n";
            result = ServerRunResult::PollError;
            break;
        }

        for (std::size_t index = polledClientCount; index > 0U; --index) {
            const std::size_t clientIndex = index - 1U;
            const short events = descriptors[clientIndex + 1U].revents;
            bool removeClient = false;

            if ((events & POLLIN) != 0) {
                std::array<char, 4096> buffer{};
                const ssize_t received =
                    recv(clients[clientIndex].socket, buffer.data(),
                         buffer.size(), 0);
                if (received > 0) {
                    Client& client = clients[clientIndex];
                    client.buffer.append(
                        buffer.data(), static_cast<std::size_t>(received));

                    for (;;) {
                        const std::size_t newline = client.buffer.find('\n');
                        if (newline == std::string::npos) {
                            break;
                        }
                        if (newline > maximumRecordSize) {
                            errorOutput_ << "Record exceeds the 1 MiB limit\n";
                            removeClient = true;
                            break;
                        }

                        std::string line = client.buffer.substr(0U, newline);
                        client.buffer.erase(0U, newline + 1U);
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }
                        processLine(line);
                    }

                    if (client.buffer.size() > maximumRecordSize) {
                        errorOutput_ << "Record exceeds the 1 MiB limit\n";
                        removeClient = true;
                    }
                } else if (received == 0) {
                    removeClient = true;
                } else if (errno != EINTR && errno != EAGAIN &&
                           errno != EWOULDBLOCK) {
                    errorOutput_ << "recv failed: " << std::strerror(errno)
                                 << '\n';
                    removeClient = true;
                }
            }

            if ((events & (POLLERR | POLLNVAL)) != 0 ||
                ((events & POLLHUP) != 0 && (events & POLLIN) == 0)) {
                removeClient = true;
            }

            if (removeClient) {
                if (!clients[clientIndex].buffer.empty()) {
                    errorOutput_ << "Discarded incomplete log record\n";
                }
                closeSocket(clients[clientIndex].socket);
                clients.erase(clients.begin() +
                              static_cast<std::ptrdiff_t>(clientIndex));
            }
        }
    }

    for (const Client& client : clients) {
        shutdown(client.socket, SHUT_RDWR);
        closeSocket(client.socket);
    }
    closeSocket(listener);
    boundPort_.store(0, std::memory_order_release);

    const auto stoppedAt = StatisticsClock::now();
    const StatisticsSnapshot finalSnapshot = statistics_.snapshot(stoppedAt);
    if (reporter_.hasUnprintedChanges(finalSnapshot)) {
        printStatistics(finalSnapshot, stoppedAt);
    }
    return result;
}

void TcpStatisticsServer::requestStop() noexcept {
    stopRequested_.store(true, std::memory_order_relaxed);
}

std::uint16_t TcpStatisticsServer::boundPort() const noexcept {
    return boundPort_.load(std::memory_order_acquire);
}

std::uint64_t TcpStatisticsServer::processedMessages() const noexcept {
    return processedMessages_.load(std::memory_order_acquire);
}

void TcpStatisticsServer::processLine(std::string_view line) {
    LogRecord record;
    const ParseRecordResult parseResult = parseLogRecord(line, record);
    if (parseResult != ParseRecordResult::Parsed) {
        errorOutput_ << "Ignored malformed log record: "
                     << toString(parseResult) << '\n';
        return;
    }

    output_ << "Received: " << line << std::endl;

    const auto receivedAt = StatisticsClock::now();
    statistics_.add(record.level, record.message.size(), receivedAt);
    processedMessages_.fetch_add(1U, std::memory_order_release);

    const StatisticsSnapshot currentSnapshot =
        statistics_.snapshot(receivedAt);
    if (reporter_.shouldPrintAfterMessage(currentSnapshot, receivedAt)) {
        printStatistics(currentSnapshot, receivedAt);
    }
}

void TcpStatisticsServer::printStatistics(
    const StatisticsSnapshot& snapshot,
    StatisticsClock::time_point now) {
    output_ << formatStatistics(snapshot) << std::flush;
    reporter_.markPrinted(snapshot, now);
}

void installTerminationHandlers() noexcept {
    terminationFlag = 0;
    std::signal(SIGINT, terminationHandler);
    std::signal(SIGTERM, terminationHandler);
}

bool terminationRequested() noexcept {
    return terminationFlag != 0;
}

}  // namespace journal_stats
