#include "journal/socket_logger.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

extern std::atomic<int> failures;

namespace {

#define CHECK_SOCKET(condition)                                                 \
    do {                                                                        \
        if (!(condition)) {                                                      \
            std::cerr << "FAILED: " << __func__ << ':' << __LINE__ << ": "    \
                      << #condition << '\n';                                    \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

#if defined(_WIN32)
using TestSocket = SOCKET;
using TestSocketLength = int;
constexpr TestSocket invalidTestSocket = INVALID_SOCKET;

void closeTestSocket(TestSocket socket) {
    if (socket != invalidTestSocket) {
        closesocket(socket);
    }
}
#else
using TestSocket = int;
using TestSocketLength = socklen_t;
constexpr TestSocket invalidTestSocket = -1;

void closeTestSocket(TestSocket socket) {
    if (socket != invalidTestSocket) {
        close(socket);
    }
}
#endif

struct ListeningSocket {
    TestSocket handle{invalidTestSocket};
    std::uint16_t port{0};
};

ListeningSocket listenOnLoopback() {
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return {};
    }
#endif

    const TestSocket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == invalidTestSocket) {
        return {};
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(listener, reinterpret_cast<const sockaddr*>(&address),
             static_cast<TestSocketLength>(sizeof(address))) != 0 ||
        listen(listener, 1) != 0) {
        closeTestSocket(listener);
        return {};
    }

    TestSocketLength addressLength =
        static_cast<TestSocketLength>(sizeof(address));
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                    &addressLength) != 0) {
        closeTestSocket(listener);
        return {};
    }

    return {listener, ntohs(address.sin_port)};
}

std::string receiveUntilClosed(TestSocket socket) {
    std::string result;
    char buffer[1024]{};
    for (;;) {
#if defined(_WIN32)
        const int received = recv(socket, buffer, sizeof(buffer), 0);
#else
        const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
#endif
        if (received <= 0) {
            break;
        }
        result.append(buffer, static_cast<std::size_t>(received));
    }
    return result;
}

void testSendsAndFiltersRecords() {
    const ListeningSocket listener = listenOnLoopback();
    CHECK_SOCKET(listener.handle != invalidTestSocket);
    if (listener.handle == invalidTestSocket) {
        return;
    }

    TestSocket connection = invalidTestSocket;
    {
        journal::SocketLogger logger("127.0.0.1", listener.port,
                                     journal::Level::Info);
        CHECK_SOCKET(logger.isReady());

        connection = accept(listener.handle, nullptr, nullptr);
        CHECK_SOCKET(connection != invalidTestSocket);
        closeTestSocket(listener.handle);
        if (connection == invalidTestSocket) {
            return;
        }

        CHECK_SOCKET(logger.log("filtered", journal::Level::Debug) ==
                     journal::WriteResult::Filtered);
        CHECK_SOCKET(logger.log("network\nmessage", journal::Level::Info) ==
                     journal::WriteResult::Written);
        CHECK_SOCKET(logger.setDefaultLevel(journal::Level::Debug));
        CHECK_SOCKET(logger.log("details\\tail") ==
                     journal::WriteResult::Written);
    }

    const std::string received = receiveUntilClosed(connection);
    closeTestSocket(connection);
    CHECK_SOCKET(received.find("filtered") == std::string::npos);
    CHECK_SOCKET(received.find("[INFO] network\\nmessage") !=
                 std::string::npos);
    CHECK_SOCKET(received.find("[DEBUG] details\\\\tail") !=
                 std::string::npos);
    CHECK_SOCKET(!received.empty() && received.back() == '\n');
}

void testReportsConnectionAndLevelErrors() {
    journal::SocketLogger unavailable("", 65000U,
                                      journal::Level::Info);
    CHECK_SOCKET(!unavailable.isReady());
    CHECK_SOCKET(unavailable.log("message") ==
                 journal::WriteResult::NotReady);

    const auto invalidLevel = static_cast<journal::Level>(255);
    CHECK_SOCKET(!unavailable.setDefaultLevel(invalidLevel));
    CHECK_SOCKET(unavailable.log("message", invalidLevel) ==
                 journal::WriteResult::InvalidLevel);
}

}  // namespace

void runSocketLoggerTests() {
    testSendsAndFiltersRecords();
    testReportsConnectionAndLevelErrors();
}
