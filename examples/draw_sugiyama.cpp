#include "hammock/graphml.hpp"
#include "hammock/sugiyama.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;
using hammock::LayoutRect;

constexpr const char* kSvgNamespace = "http://www.w3.org/2000/svg";
constexpr double kMargin = 12;

pugi::xml_node rectangle(pugi::xml_node parent, const LayoutRect& box, const char* fill, const char* stroke,
                        double radius) {
    auto rect = parent.append_child("rect");
    rect.append_attribute("x").set_value(box.x);
    rect.append_attribute("y").set_value(box.y);
    rect.append_attribute("width").set_value(box.width);
    rect.append_attribute("height").set_value(box.height);
    rect.append_attribute("rx").set_value(radius);
    rect.append_attribute("fill") = fill;
    rect.append_attribute("stroke") = stroke;
    return rect;
}

pugi::xml_node text(pugi::xml_node parent, double x, double y, const char* label) {
    auto element = parent.append_child("text");
    element.append_attribute("x").set_value(x);
    element.append_attribute("y").set_value(y);
    element.text().set(label);
    return element;
}

void arrowMarker(pugi::xml_node defs, const char* id, const char* color) {
    auto marker = defs.append_child("marker");
    marker.append_attribute("id") = id;
    marker.append_attribute("viewBox") = "0 0 10 10";
    marker.append_attribute("refX") = 10;
    marker.append_attribute("refY") = 5;
    marker.append_attribute("markerWidth") = 7;
    marker.append_attribute("markerHeight") = 7;
    marker.append_attribute("orient") = "auto";
    auto arrow = marker.append_child("path");
    arrow.append_attribute("d") = "M 0,0 L 10,5 L 0,10 Z";
    arrow.append_attribute("fill") = color;
}

void draw(const fs::path& input, const fs::path& output, const char* title) {
    const auto graphml = hammock::GraphMLDocument::loadFile(input);
    const auto& graph = graphml.graph();
    const hammock::ProgramGraph program(graph, graphml.findNode("entry").value(),
                                        graphml.findNode("exit").value());
    const auto hierarchy = hammock::buildHammockPresentation(hammock::findHammocks(program));
    const auto layout = hammock::layoutSugiyama(graph, hierarchy);

    double width = 0;
    double height = 0;
    const auto extendBounds = [&](const LayoutRect& box) {
        width = std::max(width, box.x + box.width);
        height = std::max(height, box.y + box.height);
    };
    std::ranges::for_each(layout.vertices, extendBounds);
    std::ranges::for_each(layout.clusters, extendBounds);
    for (const auto& routes : layout.edgeRoutes) {
        for (const auto& route : routes) {
            for (const auto& p : route) {
                width = std::max(width, p.x);
                height = std::max(height, p.y);
            }
        }
    }
    width += kMargin;
    height += kMargin;

    pugi::xml_document image;
    auto svg = image.append_child("svg");
    svg.append_attribute("xmlns") = kSvgNamespace;
    svg.append_attribute("width").set_value(width);
    svg.append_attribute("height").set_value(height);
    svg.append_attribute("viewBox") = ("0 0 " + std::to_string(width) + " " + std::to_string(height)).c_str();
    svg.append_attribute("role") = "img";
    svg.append_child("title").text().set(title);
    svg.append_child("desc").text().set(
        "Граф потока управления. Цветные рамки показывают гамаки, пунктирные стрелки показывают обратные дуги.");
    rectangle(svg, {0, 0, width, height}, "#ffffff", "none", 0);
    auto defs = svg.append_child("defs");
    arrowMarker(defs, "forward-arrow", "#42536a");
    arrowMarker(defs, "back-arrow", "#a54128");

    auto clusters = svg.append_child("g");
    auto captions = svg.append_child("g");
    captions.append_attribute("font-family") = "Arial, sans-serif";
    captions.append_attribute("font-size") = 12;
    captions.append_attribute("font-weight") = 600;
    for (std::size_t i = 0; i < layout.clusters.size(); ++i) {
        const LayoutRect& box = layout.clusters[i];
        const bool compound = hierarchy.cluster(i).kind == hammock::HammockKind::Compound;
        const char* color = compound ? "#35577a" : "#32664e";
        rectangle(clusters, box, compound ? "#f2f6fe" : "#f3faf5", color, 9);
        const auto label = (compound ? "Составной гамак " : "Простой гамак ") + std::to_string(i + 1);
        text(captions, box.x + 10, box.y + 15, label.c_str()).append_attribute("fill") = color;
    }

    auto edges = svg.insert_child_before("g", captions);
    edges.append_attribute("fill") = "none";
    edges.append_attribute("stroke-width") = 1.4;
    edges.append_attribute("stroke-linejoin") = "round";
    for (hammock::VertexId v = 0; v < graph.vertexCount(); ++v) {
        const auto successors = graph.successors(v);
        for (std::size_t i = 0; i < successors.size(); ++i) {
            const auto& route = layout.edgeRoutes[v][i];
            const bool feedback = successors[i] == v || route.front().y > route.back().y;
            std::string points;
            for (const auto& point : route) {
                if (!points.empty()) {
                    points += ' ';
                }
                points += std::to_string(point.x) + ',' + std::to_string(point.y);
            }
            auto line = edges.append_child("polyline");
            line.append_attribute("points") = points.c_str();
            line.append_attribute("stroke") = feedback ? "#a54128" : "#42536a";
            line.append_attribute("marker-end") = feedback ? "url(#back-arrow)" : "url(#forward-arrow)";
            if (feedback) {
                line.append_attribute("stroke-dasharray") = "5 3";
            }
        }
    }

    auto nodes = svg.append_child("g");
    nodes.append_attribute("font-family") = "Consolas, monospace";
    nodes.append_attribute("font-size") = 12;
    nodes.append_attribute("text-anchor") = "middle";
    nodes.append_attribute("fill") = "#182536";
    for (hammock::VertexId v = 0; v < graph.vertexCount(); ++v) {
        const LayoutRect& box = layout.vertices[v];
        const std::string label = std::to_string(v + 1);
        const bool terminal = v == program.entry() || v == program.exit();
        auto node = nodes.append_child("g");
        node.append_attribute("data-node-id") = graphml.nodeId(v).c_str();
        rectangle(node, box, terminal ? "#deebfb" : "#ffffff", "#586c86", 6);
        const double fontSize = std::min(12.0, (box.width - 12) / (0.66 * static_cast<double>(label.size())));
        text(node, box.x + box.width / 2, box.y + box.height / 2 + fontSize / 3, label.c_str())
            .append_attribute("font-size").set_value(fontSize);
    }
    std::ofstream stream(output, std::ios::binary);
    image.save(stream, "  ");
    if (!stream) {
        throw std::runtime_error("Не удалось записать " + output.string());
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Использование: hammock_draw_examples <каталог GraphML> <каталог рисунков>\n";
        return 1;
    }
    const fs::path input = argv[1];
    const fs::path output = argv[2];
    try {
        fs::create_directories(output);
        draw(input / "if_then_else.graphml", output / "if_then_else.svg", "Ветвление if/else");
        draw(input / "sequence_in_branch.graphml", output / "sequence_in_branch.svg", "Последовательность в ветви");
        draw(input / "nested_loops.graphml", output / "nested_loops.svg", "Вложенные циклы");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
