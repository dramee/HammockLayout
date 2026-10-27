#include "hammock/hammocks.hpp"

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <random>
#include <set>
#include <vector>

namespace hammock {
namespace {

using test::HammockKey;
using test::makeGraph;
using test::makeKey;

/// Ветвление «если — то — иначе»:
///   0 → 1;  1 → 2, 1 → 3;  2 → 4, 3 → 4;  4 → 5.
ProgramGraph makeIfThenElse() { return {makeGraph(6, {{0, 1}, {1, 2}, {1, 3}, {2, 4}, {3, 4}, {4, 5}}), 0, 5}; }

TEST(Hammocks, ChainHasQuadraticNumberOfHammocks) {
    for (std::size_t n = 2; n <= 40; ++n) {
        const auto program = test::makeChain(n);
        const auto hammocks = findHammocks(program);
        // n(n−1)/2 отрезков с конечной вершиной (замечание в статье) и n «хвостов» [i, n) без неё.
        EXPECT_EQ(hammocks.size(), n * (n + 1) / 2) << "n = " << n;
        const auto withEnd = std::ranges::count_if(hammocks, [](const Hammock& h) { return h.hasEnd(); });
        EXPECT_EQ(static_cast<std::size_t>(withEnd), n * (n - 1) / 2) << "n = " << n;
        for (const Hammock& h : hammocks) {
            EXPECT_TRUE(isHammock(program, hammocks.vertices(h), h.start, h.end));
            EXPECT_EQ(h.hasEnd(), h.last() < n);
        }
    }
}

TEST(Hammocks, IfThenElse) {
    const auto program = makeIfThenElse();
    const std::set<HammockKey> expected{
        makeKey({0}, 0, 1),
        makeKey({2}, 2, 4),
        makeKey({3}, 3, 4),
        makeKey({4}, 4, 5),
        makeKey({1, 2, 3}, 1, 4),
        makeKey({0, 1, 2, 3}, 0, 4),
        makeKey({1, 2, 3, 4}, 1, 5),
        makeKey({0, 1, 2, 3, 4}, 0, 5),
        // Гамаки без конечной вершины: вершины, достижимые из p, если p доминирует над ними.
        makeKey({0, 1, 2, 3, 4, 5}, 0, kNoVertex),
        makeKey({1, 2, 3, 4, 5}, 1, kNoVertex),
        makeKey({4, 5}, 4, kNoVertex),
        makeKey({5}, 5, kNoVertex),
    };
    EXPECT_EQ(test::keysOf(findHammocks(program)), expected);
    EXPECT_EQ(test::bruteForceHammocks(program), expected);
}

TEST(Hammocks, LoopBodyIsNotAHammockWithBackEdgeToStart) {
    // 0 → 1 → 2 → 1 (цикл), 2 → 3. Множество {1} не гамак: его конец 2 ведёт обратно в 1.
    const ProgramGraph program(makeGraph(4, {{0, 1}, {1, 2}, {2, 1}, {2, 3}}), 0, 3);
    const auto keys = test::keysOf(findHammocks(program));
    EXPECT_FALSE(keys.contains(makeKey({1}, 1, 2)));
    EXPECT_TRUE(keys.contains(makeKey({1, 2}, 1, 3)));
    EXPECT_EQ(keys, test::bruteForceHammocks(program));
}

TEST(Hammocks, HammockSetAccessors) {
    const auto program = makeIfThenElse();
    const auto hammocks = findHammocks(program);
    ASSERT_FALSE(hammocks.empty());
    for (const Hammock& h : hammocks) {
        const auto vertices = hammocks.vertices(h);
        ASSERT_EQ(vertices.size(), h.size);
        EXPECT_EQ(vertices.front(), h.start);
        EXPECT_FALSE(hammocks.contains(h, h.end));
        for (const VertexId v : vertices) {
            EXPECT_TRUE(hammocks.contains(h, v));
        }
    }
}

TEST(Hammocks, IsHammockRejectsMalformedInput) {
    const auto program = makeIfThenElse();
    const std::vector<VertexId> body{1, 2, 3};
    EXPECT_TRUE(isHammock(program, body, 1, 4));
    EXPECT_FALSE(isHammock(program, body, 2, 4));                         // в 3 входят не через начало
    EXPECT_FALSE(isHammock(program, body, 1, 5));                         // неверный конец
    EXPECT_FALSE(isHammock(program, std::vector<VertexId>{1, 2}, 1, 4));  // выход также в 3
    EXPECT_FALSE(isHammock(program, std::vector<VertexId>{1, 1}, 1, 4));  // повтор вершины
    EXPECT_FALSE(isHammock(program, std::vector<VertexId>{}, 1, 4));      // пустое множество
    EXPECT_FALSE(isHammock(program, std::vector<VertexId>{0, 1}, 1, 2));  // вход ∈ U, начало ≠ вход

    // Гамаки без конечной вершины.
    const std::vector<VertexId> tail{4, 5};
    EXPECT_TRUE(isHammock(program, tail, 4, kNoVertex));
    EXPECT_FALSE(isHammock(program, tail, 5, kNoVertex));                            // в 4 входят извне
    EXPECT_FALSE(isHammock(program, std::vector<VertexId>{2, 4, 5}, 2, kNoVertex));  // в 4 входят из 3
    EXPECT_FALSE(isHammock(program, std::vector<VertexId>{4}, 4, kNoVertex));        // выход из U в 5
    EXPECT_FALSE(isHammock(program, std::vector<VertexId>{3, 4, 5}, 3, 2));          // выход графа ∈ U
}

// Главная проверка корректности: на тысячах случайных графов программ
// результат алгоритма совпадает с полным перебором по определению.
TEST(Hammocks, MatchesBruteForceOnRandomGraphs) {
    std::mt19937 rng(20251027);
    std::size_t totalHammocks = 0;
    for (std::size_t iteration = 0; iteration < 4000; ++iteration) {
        const std::size_t n = 2 + iteration % 10;
        const auto program = test::randomProgramGraph(rng, n, iteration % 14);
        const auto expected = test::bruteForceHammocks(program);
        const auto actual = test::keysOf(findHammocks(program));
        ASSERT_EQ(actual, expected) << "итерация " << iteration;
        totalHammocks += expected.size();
    }
    EXPECT_GT(totalHammocks, 10000U);  // тест действительно что-то проверяет
}

TEST(Hammocks, LargeRandomGraphsSatisfyDefinition) {
    std::mt19937 rng(42);
    for (std::size_t iteration = 0; iteration < 20; ++iteration) {
        const auto program = test::randomProgramGraph(rng, 300, 150);
        const auto hammocks = findHammocks(program);
        for (const Hammock& h : hammocks) {
            ASSERT_TRUE(isHammock(program, hammocks.vertices(h), h.start, h.end));
        }
    }
}

}  // namespace
}  // namespace hammock
