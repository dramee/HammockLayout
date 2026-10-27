#include "cli_options.hpp"

namespace hammock::cli {

Options parseArguments(std::span<const std::string_view> arguments) {
    Options options;
    bool hasInput = false;

    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string_view argument = arguments[i];
        const auto value = [&]() -> std::string_view {
            if (i + 1 >= arguments.size()) {
                throw UsageError("параметру " + std::string(argument) + " требуется значение");
            }
            return arguments[++i];
        };

        if (argument == "-h" || argument == "--help") {
            options.showHelp = true;
        } else if (argument == "--version") {
            options.showVersion = true;
        } else if (argument == "-o" || argument == "--output") {
            options.output = std::filesystem::path(value());
        } else if (argument == "--entry") {
            options.entryId = std::string(value());
        } else if (argument == "--exit") {
            options.exitId = std::string(value());
        } else if (argument == "-l" || argument == "--list") {
            options.list = true;
        } else if (argument == "--verify") {
            options.verify = true;
        } else if (argument == "-q" || argument == "--quiet") {
            options.quiet = true;
        } else if (argument.starts_with("-") && argument != "-") {
            throw UsageError("неизвестный параметр " + std::string(argument));
        } else if (hasInput) {
            throw UsageError("указано более одного входного файла");
        } else {
            options.input = std::filesystem::path(argument);
            hasInput = true;
        }
    }

    if (!hasInput && !options.showHelp && !options.showVersion) {
        throw UsageError("не указан входной файл GraphML");
    }
    return options;
}

std::string usage(std::string_view programName) {
    std::string text = "Использование: ";
    text += programName;
    text += R"( [параметры] <вход.graphml>

Выделяет гамаки ориентированного графа (алгоритм В. Н. Касьянова, ДАН СССР, 1975)
и записывает гамачное представление графа — иерархию вложенных простых и составных
гамаков — в виде иерархического GraphML, где каждый гамак является вложенным графом.

Параметры:
  -o, --output <файл>     выходной GraphML (по умолчанию — стандартный вывод)
      --entry <id>        узел-вход графа программы (по умолчанию — единственный
                          узел без входящих рёбер)
      --exit <id>         узел-выход графа программы (по умолчанию — единственный
                          узел без исходящих рёбер)
  -l, --list              вывести список линейных компонент и гамаков; GraphML
                          записывается только при указании --output
      --verify            проверить каждый найденный гамак по определению
  -q, --quiet             не выводить сводку в стандартный поток ошибок
  -h, --help              показать эту справку
      --version           показать версию
)";
    return text;
}

}  // namespace hammock::cli
