#include "hammock/digraph.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace hammock {
namespace {

std::vector<VertexId> toVector(std::span<const VertexId> s) { return {s.begin(), s.end()}; }

TEST(Digraph, StoresSuccessorsAndPredecessors) {
    Digraph graph(3);
    graph.addEdge(0, 1);
    graph.addEdge(0, 2);
    graph.addEdge(1, 2);

    EXPECT_EQ(graph.vertexCount(), 3U);
    EXPECT_EQ(graph.edgeCount(), 3U);
    EXPECT_EQ(toVector(graph.successors(0)), (std::vector<VertexId>{1, 2}));
    EXPECT_EQ(toVector(graph.predecessors(2)), (std::vector<VertexId>{0, 1}));
    EXPECT_TRUE(graph.predecessors(0).empty());
    EXPECT_TRUE(graph.hasEdge(1, 2));
    EXPECT_FALSE(graph.hasEdge(2, 1));
}

TEST(Digraph, AllowsLoopsAndParallelEdges) {
    Digraph graph(2);
    graph.addEdge(0, 0);
    graph.addEdge(0, 1);
    graph.addEdge(0, 1);
    EXPECT_EQ(graph.edgeCount(), 3U);
    EXPECT_EQ(graph.successors(0).size(), 3U);
    EXPECT_EQ(graph.predecessors(1).size(), 2U);
    EXPECT_TRUE(graph.hasEdge(0, 0));
}

TEST(Digraph, AddVertexReturnsSequentialIds) {
    Digraph graph;
    EXPECT_EQ(graph.addVertex(), 0U);
    EXPECT_EQ(graph.addVertex(), 1U);
    EXPECT_EQ(graph.vertexCount(), 2U);
}

TEST(Digraph, RejectsUnknownVertices) {
    Digraph graph(2);
    EXPECT_THROW(graph.addEdge(0, 2), std::out_of_range);
    EXPECT_THROW((void)graph.successors(5), std::out_of_range);
    EXPECT_THROW((void)graph.predecessors(5), std::out_of_range);
    EXPECT_FALSE(graph.contains(2));
}

}  // namespace
}  // namespace hammock
