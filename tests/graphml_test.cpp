#include "hammock/graphml.hpp"

#include "hammock/hammocks.hpp"
#include "hammock/hierarchy.hpp"

#include <gtest/gtest.h>
#include <pugixml.hpp>

#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace hammock {
namespace {

constexpr const char* kIfThenElse = R"(<?xml version="1.0" encoding="UTF-8"?>
<graphml xmlns="http://graphml.graphdrawing.org/xmlns">
  <key id="d0" for="node" attr.name="label" attr.type="string"/>
  <key id="w" for="edge" attr.name="weight" attr.type="double"/>
  <graph id="G" edgedefault="directed">
    <node id="start"><data key="d0">Начало</data></node>
    <node id="cond"><data key="d0">x &gt; 0</data></node>
    <node id="then"/>
    <node id="else"/>
    <node id="join"/>
    <node id="end"/>
    <edge id="e0" source="start" target="cond"/>
    <edge id="e1" source="cond" target="then"><data key="w">0.5</data></edge>
    <edge id="e2" source="cond" target="else"/>
    <edge id="e3" source="then" target="join"/>
    <edge id="e4" source="else" target="join"/>
    <edge id="e5" source="join" target="end"/>
  </graph>
</graphml>
)";

std::string writeHierarchy(const GraphMLDocument& document) {
    const auto program = ProgramGraph::withDetectedTerminals(document.graph());
    const auto hierarchy = buildHammockPresentation(findHammocks(program));
    std::ostringstream out;
    document.writeHierarchical(out, hierarchy);
    return out.str();
}

/// Цепочка идентификаторов узлов-кластеров, содержащих элемент (от внутреннего к внешнему).
std::vector<std::string> enclosingClusters(pugi::xml_node element) {
    std::vector<std::string> result;
    for (pugi::xml_node n = element.parent(); n; n = n.parent()) {
        if (std::string_view(n.name()) == "node") {
            result.emplace_back(n.attribute("id").as_string());
        }
    }
    return result;
}

TEST(GraphML, ReadsNodesAndEdges) {
    const auto document = GraphMLDocument::loadString(kIfThenElse);
    const Digraph& graph = document.graph();
    EXPECT_EQ(graph.vertexCount(), 6U);
    EXPECT_EQ(graph.edgeCount(), 6U);
    EXPECT_EQ(document.nodeId(0), "start");
    ASSERT_TRUE(document.findNode("join").has_value());
    EXPECT_TRUE(graph.hasEdge(*document.findNode("then"), *document.findNode("join")));
    EXPECT_FALSE(document.findNode("missing").has_value());
}

TEST(GraphML, FlattensNestedInputGraphs) {
    const auto document = GraphMLDocument::loadString(R"(<graphml>
      <graph id="G" edgedefault="directed">
        <node id="a"/>
        <node id="group">
          <graph id="group:" edgedefault="directed">
            <node id="b"/>
            <node id="c"/>
            <edge source="b" target="c"/>
          </graph>
        </node>
        <edge source="a" target="b"/>
      </graph>
    </graphml>)");
    EXPECT_EQ(document.graph().vertexCount(), 3U);
    EXPECT_EQ(document.graph().edgeCount(), 2U);
    EXPECT_FALSE(document.findNode("group").has_value());
}

TEST(GraphML, ReportsErrors) {
    const auto expectError = [](const std::string& xml, const std::string& fragment) {
        try {
            (void)GraphMLDocument::loadString(xml);
            ADD_FAILURE() << "ожидалась ошибка для: " << xml;
        } catch (const GraphMLError& error) {
            EXPECT_NE(std::string(error.what()).find(fragment), std::string::npos) << error.what();
        }
    };
    expectError("<graphml><graph>", "строке 1");
    expectError("<root/>", "<graphml>");
    expectError("<graphml/>", "<graph>");
    expectError("<graphml><graph/><graph/></graphml>", "несколько графов");
    expectError(R"(<graphml><graph edgedefault="undirected"><node id="a"/><node id="b"/>
                   <edge source="a" target="b"/></graph></graphml>)",
                "неориентированное");
    expectError(R"(<graphml><graph><node id="a"/><node id="a"/></graph></graphml>)", "повторяющийся");
    expectError(R"(<graphml><graph><node id="a"/><edge source="a" target="x"/></graph></graphml>)", "несуществующий");
    expectError(R"(<graphml><graph><node id="a"/><node id="g"><graph/></node>
                   <edge source="a" target="g"/></graph></graphml>)",
                "контейнер");
    expectError(R"(<graphml><graph><hyperedge/></graph></graphml>)", "гиперрёбра");
    expectError(R"(<graphml><graph><node/></graph></graphml>)", "id");
}

TEST(GraphML, AcceptsExplicitlyDirectedEdgesInUndirectedGraph) {
    const auto document = GraphMLDocument::loadString(R"(<graphml><graph edgedefault="undirected">
        <node id="a"/><node id="b"/><edge source="a" target="b" directed="true"/></graph></graphml>)");
    EXPECT_EQ(document.graph().edgeCount(), 1U);
}

TEST(GraphML, WritesHierarchicalGraph) {
    const auto document = GraphMLDocument::loadString(kIfThenElse);
    const std::string output = writeHierarchy(document);

    pugi::xml_document xml;
    ASSERT_TRUE(xml.load_string(output.c_str())) << output;
    const pugi::xml_node graphml = xml.child("graphml");
    ASSERT_TRUE(graphml);
    EXPECT_STREQ(graphml.attribute("xmlns").as_string(), "http://graphml.graphdrawing.org/xmlns");

    // Исходные ключи сохранены, подпись кластеров использует существующий ключ label.
    EXPECT_TRUE(graphml.find_child_by_attribute("key", "id", "d0"));
    EXPECT_TRUE(graphml.find_child_by_attribute("key", "id", "w"));
    EXPECT_TRUE(graphml.find_child_by_attribute("key", "attr.name", "hammock.kind"));
    EXPECT_FALSE(graphml.find_child_by_attribute("key", "id", "hl_label"));

    // Гамачное представление: составной гамак X (весь граф) содержит простые гамаки
    // {start}, {cond, then, else} ⊃ {then}, {else}, {join}, {end} — гамаки любого размера.
    std::map<std::string, std::vector<std::string>> parents;
    for (const pugi::xpath_node& node : xml.select_nodes("//node")) {
        parents[node.node().attribute("id").as_string()] = enclosingClusters(node.node());
    }
    EXPECT_EQ(parents.size(), 13U);  // 6 исходных узлов + 7 кластеров
    for (const char* id : {"start", "cond", "join", "end"}) {
        EXPECT_EQ(parents[id].size(), 2U) << id;
    }
    EXPECT_EQ(parents["then"].size(), 3U);
    EXPECT_EQ(parents["else"].size(), 3U);
    const std::string whole = parents["start"].back();
    const std::string branch = parents["cond"].front();  // {cond, then, else}
    EXPECT_EQ(parents["then"][1], branch);
    EXPECT_EQ(parents["else"][1], branch);
    EXPECT_NE(parents["then"].front(), parents["else"].front());
    for (const char* id : {"start", "cond", "then", "else", "join", "end"}) {
        EXPECT_EQ(parents[id].back(), whole) << id;
    }

    // Данные узлов и рёбер перенесены.
    EXPECT_EQ(std::string(xml.select_node("//node[@id='start']/data").node().text().as_string()), "Начало");
    EXPECT_EQ(std::string(xml.select_node("//edge[@id='e1']/data[@key='w']").node().text().as_string()), "0.5");

    // Ребро объявлено в графе наименьшего общего предка концов.
    EXPECT_EQ(enclosingClusters(xml.select_node("//edge[@id='e1']").node()), parents["cond"]);
    EXPECT_EQ(enclosingClusters(xml.select_node("//edge[@id='e0']").node()), std::vector<std::string>{whole});
    EXPECT_EQ(enclosingClusters(xml.select_node("//edge[@id='e5']").node()), std::vector<std::string>{whole});

    // У каждого кластера есть описание гамака.
    for (const pugi::xpath_node& cluster : xml.select_nodes("//node[graph]")) {
        const std::string kind = cluster.node().find_child_by_attribute("data", "key", "hl_kind").text().as_string();
        EXPECT_TRUE(kind == "simple" || kind == "compound") << kind;
    }
}

TEST(GraphML, OutputCanBeReadBack) {
    const auto document = GraphMLDocument::loadString(kIfThenElse);
    const auto reread = GraphMLDocument::loadString(writeHierarchy(document));
    ASSERT_EQ(reread.graph().vertexCount(), document.graph().vertexCount());
    ASSERT_EQ(reread.graph().edgeCount(), document.graph().edgeCount());
    for (VertexId v = 0; v < document.graph().vertexCount(); ++v) {
        const auto mapped = reread.findNode(document.nodeId(v));
        ASSERT_TRUE(mapped.has_value());
        EXPECT_EQ(reread.graph().successors(*mapped).size(), document.graph().successors(v).size());
    }
}

TEST(GraphML, HammockWithoutEndHasNoEndAttribute) {
    const auto document = GraphMLDocument::loadString(kIfThenElse);
    pugi::xml_document xml;
    ASSERT_TRUE(xml.load_string(writeHierarchy(document).c_str()));
    const pugi::xml_node root = xml.select_node("/graphml/graph/node[graph]").node();
    ASSERT_TRUE(root);
    EXPECT_STREQ(root.find_child_by_attribute("data", "key", "hl_kind").text().as_string(), "compound");
    EXPECT_STREQ(root.find_child_by_attribute("data", "key", "hl_start").text().as_string(), "start");
    EXPECT_FALSE(root.find_child_by_attribute("data", "key", "hl_end"));
}

TEST(GraphML, GeneratesUniqueClusterIds) {
    // Идентификатор hammock1 уже занят исходным узлом.
    const auto document = GraphMLDocument::loadString(R"(<graphml><graph edgedefault="directed">
        <node id="hammock1"/><node id="b"/><node id="c"/><node id="d"/>
        <edge source="hammock1" target="b"/><edge source="b" target="c"/>
        <edge source="b" target="d"/><edge source="c" target="d"/></graph></graphml>)");
    const std::string output = writeHierarchy(document);
    pugi::xml_document xml;
    ASSERT_TRUE(xml.load_string(output.c_str()));
    std::map<std::string, int> counts;
    for (const pugi::xpath_node& node : xml.select_nodes("//node")) {
        ++counts[node.node().attribute("id").as_string()];
    }
    for (const auto& [id, count] : counts) {
        EXPECT_EQ(count, 1) << id;
    }
}

}  // namespace
}  // namespace hammock
