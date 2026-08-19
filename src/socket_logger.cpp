#include "journal/socket_logger.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace journal {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle invalidSocket = INVALID_SOCKET;

class WinsockRuntime {
public:
    WinsockRuntime() noexcept {
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockRuntime() {
        if (ready_) {
            WSACleanup();
        }
    }

    [[nodiscard]] bool ready() const noexcept {
        return ready_;
    }

private:
    bool ready_{false};
};

bool networkRuntimeReady() noexcept {
    static WinsockRuntime runtime;
    return runtime.ready();
}

void closeSocket(SocketHandle socket) noexcept {
    if (socket != invalidSocket) {
        closesocket(socket);
    }
}

void shutdownSocket(SocketHandle socket) noexcept {
    if (socket != invalidSocket) {
        shutdown(socket, SD_BOTH);
    }
}
#else
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle invalidSocket = -1;

bool networkRuntimeReady() noexcept {
    return true;
}

void closeSocket(SocketHandle socket) noexcept {
    if (socket != invalidSocket) {
        close(socket);
    }
}

void shutdownSocket(SocketHandle socket) noexcept {
    if (socket != invalidSocket) {
        shutdown(socket, SHUT_RDWR);
    }
}
#endif

bool isValidLevel(Level level) noexcept {
    switch (level) {
        case Level::Debug:
        case Level::Info:
        case Level::Error:
            return true;
    }
    return false;
}

int severity(Level level) noexcept {
    return static_cast<int>(level);
}

std::string makeTimestamp() {
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto millisecondsPart =
        duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t time = system_clock::to_time_t(now);

    std::tm utcTime{};
#if defined(_WIN32)
    if (gmtime_s(&utcTime, &time) != 0) {
#else
    if (gmtime_r(&time, &utcTime) == nullptr) {
#endif
        return "0000-00-00T00:00:00.000Z";
    }

    std::ostringstream output;
    output << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setfill('0') << std::setw(3) << millisecondsPart.count()
           << 'Z';
    return output.str();
}

std::string escapeMessage(std::string_view message) {
    std::string result;
    result.reserve(message.size());

    for (const char character : message) {
        switch (character) {
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\\':
                result += "\\\\";
                break;
            default:
                result.push_back(character);
                break;
        }
    }
    return result;
}

SocketHandle connectToHost(const std::string& host,
                           std::uint16_t port) noexcept {
    if (!networkRuntimeReady() || host.empty() || port == 0U) {
        return invalidSocket;
    }

    try {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* addresses = nullptr;
        const std::string service = std::to_string(port);
        if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) !=
            0) {
            return invalidSocket;
        }

        SocketHandle connected = invalidSocket;
        for (addrinfo* current = addresses; current != nullptr;
             current = current->ai_next) {
            const SocketHandle candidate =
                socket(current->ai_family, current->ai_socktype,
                       current->ai_protocol);
            if (candidate == invalidSocket) {
                continue;
            }

#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
            const int enabled = 1;
            setsockopt(candidate, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                       sizeof(enabled));
#endif

            if (connect(candidate, current->ai_addr,
                        static_cast<SocketLength>(current->ai_addrlen)) == 0) {
                connected = candidate;
                break;
            }
            closeSocket(candidate);
        }

        freeaddrinfo(addresses);
        return connected;
    } catch (...) {
        return invalidSocket;
    }
}

bool sendAll(SocketHandle socket, const std::string& data) noexcept {
    std::size_t sent = 0;
    while (sent < data.size()) {
#if defined(_WIN32)
        const std::size_t remaining = data.size() - sent;
        const int chunkSize = static_cast<int>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int result = send(socket, data.data() + sent, chunkSize, 0);
        if (result == SOCKET_ERROR || result == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
#else
#if defined(MSG_NOSIGNAL)
        constexpr int sendFlags = MSG_NOSIGNAL;
#else
        constexpr int sendFlags = 0;
#endif
        const ssize_t result =
            send(socket, data.data() + sent, data.size() - sent, sendFlags);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
#endif
    }
    return true;
}

}  // namespace

struct SocketLogger::Impl {
    explicit Impl(Level level) noexcept
        : defaultLevel(isValidLevel(level) ? level : Level::Info),
          configurationValid(isValidLevel(level)) {}

    mutable std::mutex socketMutex;
    SocketHandle socket{invalidSocket};
    std::atomic<Level> defaultLevel;
    std::atomic<bool> configurationValid;
};

SocketLogger::SocketLogger(std::string host, std::uint16_t port,
                           Level defaultLevel) noexcept {
    try {
        impl_ = std::make_unique<Impl>(defaultLevel);
        impl_->socket = connectToHost(host, port);
    } catch (...) {
        impl_.reset();
    }
}

SocketLogger::~SocketLogger() {
    if (impl_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(impl_->socketMutex);
    shutdownSocket(impl_->socket);
    closeSocket(impl_->socket);
    impl_->socket = invalidSocket;
}

WriteResult SocketLogger::log(std::string_view message) noexcept {
    return log(message, defaultLevel());
}

WriteResult SocketLogger::log(std::string_view message,
                              Level level) noexcept {
    if (!isValidLevel(level)) {
        return WriteResult::InvalidLevel;
    }
    if (impl_ == nullptr ||
        !impl_->configurationValid.load(std::memory_order_relaxed)) {
        return WriteResult::NotReady;
    }
    if (severity(level) < severity(defaultLevel())) {
        return WriteResult::Filtered;
    }

    try {
        std::string record = '[' + makeTimestamp() + "] [" +
                             toString(level) + "] " + escapeMessage(message) +
                             '\n';

        std::lock_guard<std::mutex> lock(impl_->socketMutex);
        if (impl_->socket == invalidSocket) {
            return WriteResult::NotReady;
        }
        if (!sendAll(impl_->socket, record)) {
            shutdownSocket(impl_->socket);
            closeSocket(impl_->socket);
            impl_->socket = invalidSocket;
            return WriteResult::WriteError;
        }
        return WriteResult::Written;
    } catch (...) {
        return WriteResult::WriteError;
    }
}

bool SocketLogger::setDefaultLevel(Level level) noexcept {
    if (impl_ == nullptr || !isValidLevel(level)) {
        return false;
    }
    impl_->defaultLevel.store(level, std::memory_order_relaxed);
    impl_->configurationValid.store(true, std::memory_order_relaxed);
    return true;
}

Level SocketLogger::defaultLevel() const noexcept {
    if (impl_ == nullptr) {
        return Level::Info;
    }
    return impl_->defaultLevel.load(std::memory_order_relaxed);
}

bool SocketLogger::isReady() const noexcept {
    if (impl_ == nullptr ||
        !impl_->configurationValid.load(std::memory_order_relaxed)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->socketMutex);
    return impl_->socket != invalidSocket;
}

}  // namespace journal
