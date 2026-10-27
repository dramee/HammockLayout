/// @file examples_test.cpp
/// @brief Сквозная проверка на примерах: гамачные представления графов из examples/
/// совпадают с эталонами в examples/output/.
///
/// Чтобы перегенерировать эталоны после изменения алгоритма, запустите тесты
/// с переменной окружения HAMMOCK_UPDATE_EXAMPLES=1.

#include "hammock/graphml.hpp"
#include "hammock/hammocks.hpp"
#include "hammock/hierarchy.hpp"
#include "hammock/program_graph.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace hammock {
namespace {

namespace fs = std::filesystem;

const fs::path kExamplesDir = HAMMOCK_EXAMPLES_DIR;

std::vector<std::string> exampleNames() {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(kExamplesDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".graphml") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(names);
    return names;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    std::erase(text, '\r');  // эталоны могут быть извлечены из git с переводами строк CRLF
    return text;
}

class Example : public testing::TestWithParam<std::string> {};

TEST_P(Example, MatchesExpectedPresentation) {
    const fs::path input = kExamplesDir / (GetParam() + ".graphml");
    const fs::path expectedPath = kExamplesDir / "output" / (GetParam() + ".graphml");

    const auto document = GraphMLDocument::loadFile(input);
    const auto program = ProgramGraph::withDetectedTerminals(document.graph());
    const auto hammocks = findHammocks(program);
    for (const Hammock& h : hammocks) {
        ASSERT_TRUE(isHammock(program, hammocks.vertices(h), h.start, h.end));
    }

    std::ostringstream out;
    document.writeHierarchical(out, buildHammockPresentation(hammocks));
    const std::string actual = out.str();

    if (std::getenv("HAMMOCK_UPDATE_EXAMPLES") != nullptr) {
        std::ofstream(expectedPath, std::ios::binary) << actual;
        GTEST_SKIP() << "эталон перезаписан: " << expectedPath.string();
    }
    ASSERT_TRUE(fs::exists(expectedPath)) << "нет эталона " << expectedPath.string();
    EXPECT_EQ(actual, readFile(expectedPath));
}

INSTANTIATE_TEST_SUITE_P(Examples, Example, testing::ValuesIn(exampleNames()),
                         [](const testing::TestParamInfo<std::string>& example) { return example.param; });

}  // namespace
}  // namespace hammock
