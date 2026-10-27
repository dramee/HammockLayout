#include "hammock/program_graph.hpp"

#include "test_support.hpp"

#include <gtest/gtest.h>

namespace hammock {
namespace {

using test::makeGraph;

TEST(ProgramGraph, AcceptsValidGraph) {
    const ProgramGraph program(makeGraph(4, {{0, 1}, {1, 2}, {2, 1}, {2, 3}}), 0, 3);
    EXPECT_EQ(program.entry(), 0U);
    EXPECT_EQ(program.exit(), 3U);
    EXPECT_EQ(program.vertexCount(), 4U);
}

TEST(ProgramGraph, DetectsTerminals) {
    const auto program = ProgramGraph::withDetectedTerminals(makeGraph(3, {{2, 0}, {0, 1}}));
    EXPECT_EQ(program.entry(), 2U);
    EXPECT_EQ(program.exit(), 1U);
}

TEST(ProgramGraph, RejectsAmbiguousTerminals) {
    EXPECT_THROW((void)ProgramGraph::withDetectedTerminals(makeGraph(4, {{0, 2}, {1, 2}, {2, 3}})), ProgramGraphError);
    EXPECT_THROW((void)ProgramGraph::withDetectedTerminals(makeGraph(4, {{0, 1}, {1, 2}, {1, 3}})), ProgramGraphError);
}

TEST(ProgramGraph, RejectsEdgesIntoEntryOrOutOfExit) {
    EXPECT_THROW(ProgramGraph(makeGraph(3, {{0, 1}, {1, 0}, {1, 2}}), 0, 2), ProgramGraphError);
    EXPECT_THROW(ProgramGraph(makeGraph(3, {{0, 1}, {1, 2}, {2, 1}}), 0, 2), ProgramGraphError);
}

TEST(ProgramGraph, RejectsUnreachableVertex) {
    try {
        ProgramGraph(makeGraph(4, {{0, 1}, {1, 3}, {2, 3}}), 0, 3);
        FAIL() << "ожидалось исключение";
    } catch (const ProgramGraphError& error) {
        EXPECT_EQ(error.vertex(), 2U);
    }
}

TEST(ProgramGraph, RejectsDeadEnd) {
    // Из вершин 1 и 2 (цикл) выход недостижим.
    EXPECT_THROW(ProgramGraph(makeGraph(4, {{0, 1}, {1, 2}, {2, 1}, {0, 3}}), 0, 3), ProgramGraphError);
}

TEST(ProgramGraph, RejectsSameEntryAndExitOrUnknownVertex) {
    EXPECT_THROW(ProgramGraph(Digraph(1), 0, 0), ProgramGraphError);
    EXPECT_THROW(ProgramGraph(makeGraph(2, {{0, 1}}), 0, 7), ProgramGraphError);
}

}  // namespace
}  // namespace hammock
