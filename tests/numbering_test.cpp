#include "hammock/numbering.hpp"

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <random>
#include <stdexcept>
#include <vector>

namespace hammock {
namespace {

using test::makeGraph;

std::vector<VertexId> toVector(std::span<const VertexId> s) { return {s.begin(), s.end()}; }

TEST(Numbering, BuildsInversePermutation) {
    const Numbering numbering({2, 0, 1});
    EXPECT_EQ(numbering[2], 0U);
    EXPECT_EQ(numbering[0], 1U);
    EXPECT_EQ(numbering.vertexAt(2), 1U);
    EXPECT_THROW(Numbering({0, 0}), std::invalid_argument);
    EXPECT_THROW(Numbering({0, 5}), std::invalid_argument);
}

TEST(DepthFirstNumberings, MatchesManualTraversal) {
    // 0 → 1 → 3 → 4, 0 → 2 → 3: путь 0,1,3,4; затем 2.
    const ProgramGraph program(makeGraph(5, {{0, 1}, {0, 2}, {1, 3}, {2, 3}, {3, 4}}), 0, 4);
    const auto dfs = computeDepthFirstNumberings(program);
    EXPECT_EQ(toVector(dfs.m.order()), (std::vector<VertexId>{0, 1, 3, 4, 2}));
    // Порядок удаления из пути: 4, 3, 1, 2, 0; N-нумерация — обратный ему.
    EXPECT_EQ(toVector(dfs.n.order()), (std::vector<VertexId>{0, 2, 1, 3, 4}));
}

TEST(DepthFirstNumberings, NIsTopologicalOnAcyclicGraphs) {
    std::mt19937 rng(7);
    for (std::size_t iteration = 0; iteration < 200; ++iteration) {
        // Ациклический граф: дуги только от меньшего логического номера к большему.
        const std::size_t n = 2 + iteration % 30;
        Digraph graph(n);
        for (VertexId v = 1; v + 1 < n; ++v) {
            graph.addEdge(std::uniform_int_distribution<VertexId>(0, v - 1)(rng), v);
            graph.addEdge(v, std::uniform_int_distribution<VertexId>(v + 1, n - 1)(rng));
        }
        graph.addEdge(0, n - 1);
        const ProgramGraph program(std::move(graph), 0, n - 1);
        const auto dfs = computeDepthFirstNumberings(program);
        for (VertexId v = 0; v < n; ++v) {
            for (const VertexId s : program.graph().successors(v)) {
                EXPECT_LT(dfs.n[v], dfs.n[s]);
            }
        }
        EXPECT_EQ(dfs.m[0], 0U);
        EXPECT_EQ(dfs.n[0], 0U);
        EXPECT_EQ(dfs.n[n - 1], n - 1);
    }
}

TEST(LNumbering, StartsFromNNumberingAndKeepsEntryFirst) {
    std::mt19937 rng(11);
    for (std::size_t iteration = 0; iteration < 200; ++iteration) {
        const auto program = test::randomProgramGraph(rng, 2 + iteration % 40, iteration % 50);
        const auto dfs = computeDepthFirstNumberings(program);
        const auto l = computeLNumbering(program, dfs.n);
        ASSERT_EQ(l.size(), program.vertexCount());
        EXPECT_EQ(l.vertexAt(0), program.entry());
    }
}

// Лемма 4: каждый гамак — отрезок L-нумерации, начинающийся с его начальной вершины,
// причём M-номера его вершин не меньше M-номера начальной.
TEST(LNumbering, EveryHammockIsAnLBlock) {
    std::mt19937 rng(3);
    for (std::size_t iteration = 0; iteration < 500; ++iteration) {
        const auto program = test::randomProgramGraph(rng, 2 + iteration % 9, iteration % 12);
        const auto dfs = computeDepthFirstNumberings(program);
        const auto l = computeLNumbering(program, dfs.n);
        for (const auto& [vertices, start, end] : test::bruteForceHammocks(program)) {
            for (const VertexId v : vertices) {
                EXPECT_GE(l[v], l[start]);
                EXPECT_LT(l[v], l[start] + vertices.size());
                EXPECT_GE(dfs.m[v], dfs.m[start]);
            }
            if (end != kNoVertex) {
                EXPECT_TRUE(dfs.m[end] < dfs.m[start] || l[end] == l[start] + vertices.size());
            }
        }
    }
}

}  // namespace
}  // namespace hammock
