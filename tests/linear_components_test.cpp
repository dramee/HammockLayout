#include "hammock/linear_components.hpp"

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <random>
#include <set>

namespace hammock {
namespace {

using test::HammockKey;
using test::makeGraph;
using test::makeKey;

TEST(LinearComponents, ChainHasNMinusOneComponents) {
    // Замечание в статье: граф без разветвлений содержит n−1 линейную компоненту.
    for (std::size_t n = 2; n <= 30; ++n) {
        const auto components = findLinearComponents(test::makeChain(n));
        ASSERT_EQ(components.size(), n - 1);
        for (VertexId v = 0; v + 1 < n; ++v) {
            EXPECT_EQ(components[v].start, v);
            EXPECT_EQ(components[v].end, v + 1);
            EXPECT_EQ(components[v].vertices, std::vector<VertexId>{v});
        }
    }
}

TEST(LinearComponents, IfThenElseFollowedByLoop) {
    // 0 → 1; 1 → 2 | 3; 2, 3 → 4; 4 → 5 → 4 (цикл); 5 → 6.
    const ProgramGraph program(makeGraph(7, {{0, 1}, {1, 2}, {1, 3}, {2, 4}, {3, 4}, {4, 5}, {5, 4}, {5, 6}}), 0, 6);
    const std::set<HammockKey> expected{
        makeKey({0}, 0, 1),
        makeKey({1, 2, 3}, 1, 4),
        makeKey({4, 5}, 4, 6),
    };
    EXPECT_EQ(test::keysOf(findLinearComponents(program)), expected);
    EXPECT_EQ(test::bruteForceLinearComponents(program), expected);
}

TEST(LinearComponents, ComponentsFormAChainFromEntryToExit) {
    std::mt19937 rng(5);
    for (std::size_t iteration = 0; iteration < 300; ++iteration) {
        const auto program = test::randomProgramGraph(rng, 2 + iteration % 60, iteration % 40);
        const auto components = findLinearComponents(program);
        ASSERT_FALSE(components.empty());
        EXPECT_EQ(components.front().start, program.entry());
        EXPECT_EQ(components.back().end, program.exit());
        std::size_t covered = 0;
        for (std::size_t i = 0; i < components.size(); ++i) {
            covered += components[i].vertices.size();
            EXPECT_TRUE(isHammock(program, components[i].vertices, components[i].start, components[i].end));
            if (i + 1 < components.size()) {
                EXPECT_EQ(components[i].end, components[i + 1].start);
            }
        }
        EXPECT_EQ(covered, program.vertexCount() - 1);  // разбиение X \ {выход}
    }
}

TEST(LinearComponents, MatchesBruteForceOnRandomGraphs) {
    std::mt19937 rng(1975);
    for (std::size_t iteration = 0; iteration < 2000; ++iteration) {
        const std::size_t n = 2 + iteration % 9;
        const auto program = test::randomProgramGraph(rng, n, iteration % 11);
        ASSERT_EQ(test::keysOf(findLinearComponents(program)), test::bruteForceLinearComponents(program))
            << "итерация " << iteration;
    }
}

}  // namespace
}  // namespace hammock
