#include "input_parser.hpp"
#include "journal/file_logger.hpp"
#include "journal_application.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace {

void printUsage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <journal-file> <debug|info|error>\n";
}

void printHelp() {
    std::cout
        << "Enter a message and press Enter.\n"
        << "Message without a level: service started\n"
        << "Message with a level:    [error] connection failed\n"
        << "Available levels: debug, info, error\n"
        << "Commands: /help, /quit\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printUsage(argv[0]);
        return 1;
    }

    const auto defaultLevel = journal_app::parseLevelName(argv[2]);
    if (!defaultLevel.has_value()) {
        std::cerr << "Unknown default level: " << argv[2] << '\n';
        printUsage(argv[0]);
        return 2;
    }

    journal::FileLogger logger(argv[1], *defaultLevel);
    if (!logger.isReady()) {
        std::cerr << "Cannot open journal file: " << argv[1] << '\n';
        return 3;
    }

    try {
        journal_app::JournalApplication application(logger, std::cerr);
        printHelp();

        std::string line;
        for (;;) {
            std::cout << "> " << std::flush;
            if (!std::getline(std::cin, line)) {
                std::cout << '\n';
                break;
            }

            journal_app::ParsedInput input = journal_app::parseInput(line);
            switch (input.kind) {
                case journal_app::InputKind::Message:
                    if (!application.submit(std::move(input.message))) {
                        std::cerr << "The journal writer has already stopped\n";
                    }
                    break;
                case journal_app::InputKind::Empty:
                    break;
                case journal_app::InputKind::Help:
                    printHelp();
                    break;
                case journal_app::InputKind::Quit:
                    application.stop();
                    return application.hasWriteErrors() ? 4 : 0;
                case journal_app::InputKind::Invalid:
                    std::cerr << "Invalid input. Use [level] message or /help\n";
                    break;
            }
        }

        application.stop();
        return application.hasWriteErrors() ? 4 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Cannot start writer thread: " << error.what() << '\n';
        return 5;
    }
}
