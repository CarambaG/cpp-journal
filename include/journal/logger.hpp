#pragma once

#include <cstdint>
#include <string_view>

namespace journal {

// The declaration order defines the severity order used for filtering.
enum class Level : std::uint8_t {
    Debug = 0,
    Info = 1,
    Error = 2,
};

enum class WriteResult : std::uint8_t {
    Written,
    Filtered,
    InvalidLevel,
    NotReady,
    WriteError,
};

[[nodiscard]] const char* toString(Level level) noexcept;
[[nodiscard]] const char* toString(WriteResult result) noexcept;

// A common interface allows another destination (for example, a socket) to be
// introduced without changing client code.
class ILogger {
public:
    virtual ~ILogger() = default;

    [[nodiscard]] virtual WriteResult log(std::string_view message) noexcept = 0;
    [[nodiscard]] virtual WriteResult log(std::string_view message,
                                          Level level) noexcept = 0;

    [[nodiscard]] virtual bool setDefaultLevel(Level level) noexcept = 0;
    [[nodiscard]] virtual Level defaultLevel() const noexcept = 0;
    [[nodiscard]] virtual bool isReady() const noexcept = 0;
};

}  // namespace journal
