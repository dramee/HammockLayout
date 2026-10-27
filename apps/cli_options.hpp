#pragma once

/// @file cli_options.hpp
/// @brief Разбор аргументов командной строки утилиты hammock-layout.

#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hammock::cli {

/// Ошибка в аргументах командной строки.
class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Options {
    std::filesystem::path input;
    std::optional<std::filesystem::path> output;
    std::optional<std::string> entryId;
    std::optional<std::string> exitId;
    bool list = false;
    bool verify = false;
    bool quiet = false;
    bool showHelp = false;
    bool showVersion = false;
};

/// Разбирает аргументы (без имени программы).
/// @throws UsageError при некорректных аргументах.
[[nodiscard]] Options parseArguments(std::span<const std::string_view> arguments);

/// Текст справки.
[[nodiscard]] std::string usage(std::string_view programName);

}  // namespace hammock::cli
