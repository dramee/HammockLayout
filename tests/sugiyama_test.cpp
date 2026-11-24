#include "hammock/sugiyama.hpp"

#include "hammock/graphml.hpp"
#include "test_support.hpp"

#include <gtest/gtest.h>
#include <pugixml.hpp>

#include <sstream>

namespace hammock {
namespace {

bool contains(const LayoutRect& parent, const LayoutRect& child) {
    return child.x > parent.x && child.y > parent.y &&
           child.x + child.width < parent.x + parent.width && child.y + child.height < parent.y + parent.height;
}

TEST(Sugiyama, RanksDiamondAndRoutesLongEdges) {
    const ProgramGraph program(test::makeGraph(4, {{0, 1}, {0, 2}, {0, 3}, {1, 3}, {2, 3}}), 0, 3);
    const HammockHierarchy hierarchy(Numbering({0, 1, 2, 3}), {{0, kNoVertex, 0, 4}});
    const auto layout = layoutSugiyama(program.graph(), hierarchy);
    const auto& v = layout.vertices;
    EXPECT_LT(v[0].y, v[1].y);
    EXPECT_EQ(v[1].y, v[2].y);
    EXPECT_LT(v[2].y, v[3].y);
    EXPECT_GE(std::max(v[1].x, v[2].x), std::min(v[1].x, v[2].x) + v[1].width);

    const auto& route = layout.edgeRoutes[0][2];
    ASSERT_EQ(route.size(), 6U);  // Порты, границы рядов и проход через колонку фиктивной вершины.
    EXPECT_EQ(route.front(), (LayoutPoint{v[0].x + v[0].width / 2, v[0].y + v[0].height}));
    EXPECT_EQ(route.back(), (LayoutPoint{v[3].x + v[3].width / 2, v[3].y}));
    EXPECT_LT(route[2].y, v[1].y);
    EXPECT_GT(route[3].y, v[1].y + v[1].height);
    EXPECT_EQ(route[2].x, route[3].x);
}

TEST(Sugiyama, ContainsNestedHammocksAndKeepsEdgeDirection) {
    const ProgramGraph program(test::makeGraph(6, {{0, 1}, {1, 2}, {1, 5}, {2, 2}, {2, 3},
                                                  {2, 3}, {3, 4}, {4, 1}}), 0, 5);
    const auto hierarchy = buildHammockPresentation(findHammocks(program));
    const auto layout = layoutSugiyama(program.graph(), hierarchy);
    const auto repeated = layoutSugiyama(program.graph(), hierarchy);
    EXPECT_EQ(layout.vertices, repeated.vertices);
    EXPECT_EQ(layout.clusters, repeated.clusters);
    EXPECT_EQ(layout.edgeRoutes, repeated.edgeRoutes);

    const auto loop = hierarchy.clusterOf(1);
    ASSERT_NE(loop, HammockHierarchy::kRoot);
    EXPECT_TRUE(contains(layout.clusters[loop], layout.vertices[1]));
    const auto singleton = hierarchy.clusterOf(2);
    ASSERT_NE(singleton, loop);
    ASSERT_NE(singleton, HammockHierarchy::kRoot);
    EXPECT_TRUE(contains(layout.clusters[loop], layout.clusters[singleton]));
    EXPECT_TRUE(contains(layout.clusters[singleton], layout.vertices[2]));
    EXPECT_LT(layout.vertices[1].y, layout.vertices[4].y);
    const auto& back = layout.edgeRoutes[4][0];
    EXPECT_GT(back.front().y, back.back().y);
    EXPECT_EQ(back.front().y, layout.vertices[4].y);
    const auto& target = layout.vertices[1];
    EXPECT_EQ(back.back(), (LayoutPoint{target.x + target.width, target.y + target.height / 2}));
    ASSERT_GE(back.size(), 2U);
    EXPECT_EQ(back[back.size() - 2].y, back.back().y);
    EXPECT_GT(back[back.size() - 2].x, back.back().x);

    const auto& selfLoop = layout.edgeRoutes[2][0];
    ASSERT_EQ(selfLoop.size(), 5U);
    const auto& loopNode = layout.vertices[2];
    EXPECT_EQ(selfLoop.back(), (LayoutPoint{loopNode.x + loopNode.width, loopNode.y + loopNode.height / 2}));
    EXPECT_EQ(selfLoop[3].y, selfLoop.back().y);
    EXPECT_GT(selfLoop[3].x, selfLoop.back().x);
    EXPECT_EQ(layout.edgeRoutes[2].size(), 3U);  // Петля и обе кратные дуги.

    const auto& outgoing = layout.edgeRoutes[1][1];
    const double boundary = layout.clusters[loop].y + layout.clusters[loop].height;
    EXPECT_TRUE(std::ranges::any_of(outgoing, [&](const LayoutPoint& p) { return p.y == boundary; }));
}

TEST(Sugiyama, ExportsLayoutAndPreservesGraphMLTopology) {
    const auto input = GraphMLDocument::loadString(R"(<graphml><graph edgedefault="directed">
        <node id="entry"/><node id="body"/><node id="exit"/>
        <edge id="in" source="entry" target="body"/>
        <edge id="loop" source="body" target="body"/>
        <edge id="out" source="body" target="exit"/>
        </graph></graphml>)");
    const ProgramGraph program(input.graph(), *input.findNode("entry"), *input.findNode("exit"));
    const auto hierarchy = buildHammockPresentation(findHammocks(program));
    std::ostringstream output;
    input.writeSugiyama(output, hierarchy);

    const auto parsed = GraphMLDocument::loadString(output.str());
    EXPECT_EQ(parsed.graph().vertexCount(), input.graph().vertexCount());
    EXPECT_EQ(parsed.graph().edgeCount(), input.graph().edgeCount());
    const auto body = parsed.findNode("body");
    ASSERT_TRUE(body.has_value());
    EXPECT_TRUE(parsed.graph().hasEdge(*body, *body));
    pugi::xml_document xml;
    ASSERT_TRUE(xml.load_string(output.str().c_str()));
    EXPECT_EQ(xml.select_nodes("//node/data[@key='hl_x']").size(),
              program.vertexCount() + hierarchy.clusters().size());
    EXPECT_EQ(xml.select_nodes("//edge/data[@key='hl_points']").size(), program.graph().edgeCount());
    EXPECT_STREQ(xml.select_node("//edge[@id='out']").node().attribute("source").as_string(), "body");
    EXPECT_STREQ(xml.select_node("//edge[@id='out']").node().attribute("target").as_string(), "exit");
}

}  // namespace
}  // namespace hammock
