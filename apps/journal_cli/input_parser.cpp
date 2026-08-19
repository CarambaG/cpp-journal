#include "input_parser.hpp"

#include <cctype>

namespace journal_app {
namespace {

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

bool equalsIgnoreCase(std::string_view left,
                      std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto leftCharacter = static_cast<unsigned char>(left[index]);
        const auto rightCharacter = static_cast<unsigned char>(right[index]);
        if (std::tolower(leftCharacter) != std::tolower(rightCharacter)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<journal::Level> parseLevelName(
    std::string_view text) noexcept {
    text = trim(text);
    if (equalsIgnoreCase(text, "debug")) {
        return journal::Level::Debug;
    }
    if (equalsIgnoreCase(text, "info")) {
        return journal::Level::Info;
    }
    if (equalsIgnoreCase(text, "error")) {
        return journal::Level::Error;
    }
    return std::nullopt;
}

ParsedInput parseInput(std::string_view line) {
    line = trim(line);
    if (line.empty()) {
        return {InputKind::Empty, {}};
    }
    if (line == "/quit" || line == "/exit") {
        return {InputKind::Quit, {}};
    }
    if (line == "/help") {
        return {InputKind::Help, {}};
    }

    if (line.front() != '[') {
        return {InputKind::Message, {std::string(line), std::nullopt}};
    }

    const std::size_t closingBracket = line.find(']');
    if (closingBracket == std::string_view::npos) {
        return {InputKind::Invalid, {}};
    }

    const auto level = parseLevelName(line.substr(1, closingBracket - 1));
    const std::string_view message = trim(line.substr(closingBracket + 1));
    if (!level.has_value() || message.empty()) {
        return {InputKind::Invalid, {}};
    }

    return {InputKind::Message, {std::string(message), level}};
}

}  // namespace journal_app
