#pragma once

#include "journal/logger.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace journal_app {

struct Message {
    std::string text;
    std::optional<journal::Level> level;
};

enum class InputKind {
    Message,
    Empty,
    Help,
    Quit,
    Invalid,
};

struct ParsedInput {
    InputKind kind{InputKind::Empty};
    Message message;
};

[[nodiscard]] std::optional<journal::Level> parseLevelName(
    std::string_view text) noexcept;

// Supported input:
//   message text          -> use the default level
//   [info] message text   -> use the explicitly specified level
//   /help, /quit
[[nodiscard]] ParsedInput parseInput(std::string_view line);

}  // namespace journal_app
