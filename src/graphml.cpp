#include "hammock/graphml.hpp"

#include "hammock/sugiyama.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hammock {
namespace {

constexpr const char* kGraphMLNamespace = "http://graphml.graphdrawing.org/xmlns";
constexpr const char* kXsiNamespace = "http://www.w3.org/2001/XMLSchema-instance";
constexpr const char* kSchemaLocation =
    "http://graphml.graphdrawing.org/xmlns http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd";

/// Хеш строк с поддержкой гетерогенного поиска по std::string_view.
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

using StringIndex = std::unordered_map<std::string, VertexId, StringHash, std::equal_to<>>;
using StringSet = std::unordered_set<std::string, StringHash, std::equal_to<>>;

/// Локальное имя элемента без префикса пространства имён.
std::string_view localName(const pugi::xml_node& node) {
    const std::string_view name = node.name();
    const auto colon = name.find(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

bool isElement(const pugi::xml_node& node, std::string_view name) {
    return node.type() == pugi::node_element && localName(node) == name;
}

/// Номер строки (с 1), соответствующий смещению @p offset в тексте.
std::size_t lineAt(std::string_view text, std::ptrdiff_t offset) {
    const auto end = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(offset, 0, std::ssize(text)));
    return static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(end), '\n')) +
           1;
}

/// Копирует узел без вложенного графа: атрибуты, <data>, <port> и т. п.
pugi::xml_node appendShallowCopy(pugi::xml_node parent, const pugi::xml_node& source) {
    pugi::xml_node copy = parent.append_child(source.name());
    for (const pugi::xml_attribute& attribute : source.attributes()) {
        copy.append_copy(attribute);
    }
    for (const pugi::xml_node& child : source.children()) {
        if (!isElement(child, "graph")) {
            copy.append_copy(child);
        }
    }
    return copy;
}

std::string labelPrefix(HammockKind kind) {
    switch (kind) {
        case HammockKind::Simple:
            return "Простой гамак";
        case HammockKind::Compound:
            return "Составной гамак";
        case HammockKind::Decomposable:
            break;
    }
    return "Гамак";
}

/// Возвращает @p base или ближайший вариант base_1, base_2, …, отсутствующий в @p used; добавляет его в @p used.
std::string makeUnique(std::string base, StringSet& used) {
    std::string candidate = base;
    for (std::size_t suffix = 1; used.contains(candidate); ++suffix) {
        candidate = base + "_" + std::to_string(suffix);
    }
    used.insert(candidate);
    return candidate;
}

}  // namespace

struct GraphMLDocument::Impl {
    struct EdgeRecord {
        VertexId source;
        VertexId target;
        std::size_t slot;
        pugi::xml_node element;
    };

    pugi::xml_document document;
    pugi::xml_node root;   // <graphml>
    pugi::xml_node graph;  // <graph> верхнего уровня

    Digraph digraph;
    std::vector<std::string> ids;
    std::vector<pugi::xml_node> nodeElements;
    std::vector<EdgeRecord> edges;
    StringIndex index;
    StringSet containerIds;  // узлы со вложенными графами (не являются вершинами)
    StringSet usedIds;       // все идентификаторы узлов, графов и рёбер документа

    void parse(std::string_view text);
    void collectGraph(const pugi::xml_node& graphElement, std::vector<pugi::xml_node>& pendingEdges);
    void resolveEdges(const std::vector<pugi::xml_node>& pendingEdges);
};

void GraphMLDocument::Impl::parse(std::string_view text) {
    const pugi::xml_parse_result result = document.load_buffer(text.data(), text.size());
    if (!result) {
        throw GraphMLError("ошибка разбора XML в строке " + std::to_string(lineAt(text, result.offset)) + ": " +
                           result.description());
    }

    root = document.document_element();
    if (!isElement(root, "graphml")) {
        throw GraphMLError("корневой элемент документа должен быть <graphml>");
    }

    for (const pugi::xml_node& child : root.children()) {
        if (isElement(child, "graph")) {
            if (graph) {
                throw GraphMLError("документ содержит несколько графов верхнего уровня; поддерживается один");
            }
            graph = child;
        }
    }
    if (!graph) {
        throw GraphMLError("документ не содержит элемента <graph>");
    }

    std::vector<pugi::xml_node> pendingEdges;
    collectGraph(graph, pendingEdges);
    resolveEdges(pendingEdges);
}

void GraphMLDocument::Impl::collectGraph(const pugi::xml_node& graphElement,
                                         std::vector<pugi::xml_node>& pendingEdges) {
    if (const char* id = graphElement.attribute("id").as_string(nullptr)) {
        usedIds.insert(id);
    }
    const std::string_view edgeDefault = graphElement.attribute("edgedefault").as_string("directed");
    if (edgeDefault != "directed" && edgeDefault != "undirected") {
        throw GraphMLError("недопустимое значение edgedefault=\"" + std::string(edgeDefault) + "\"");
    }

    for (const pugi::xml_node& child : graphElement.children()) {
        if (isElement(child, "node")) {
            const std::string id = child.attribute("id").as_string();
            if (id.empty()) {
                throw GraphMLError("у элемента <node> отсутствует атрибут id");
            }
            if (index.contains(id) || containerIds.contains(id)) {
                throw GraphMLError("повторяющийся идентификатор узла \"" + id + "\"");
            }
            usedIds.insert(id);

            pugi::xml_node subgraph;
            for (const pugi::xml_node& grandChild : child.children()) {
                if (isElement(grandChild, "graph")) {
                    subgraph = grandChild;
                    break;
                }
            }
            if (subgraph) {
                containerIds.insert(id);
                collectGraph(subgraph, pendingEdges);
            } else {
                const VertexId v = digraph.addVertex();
                index.emplace(id, v);
                ids.push_back(id);
                nodeElements.push_back(child);
            }
        } else if (isElement(child, "edge")) {
            const std::string_view directed = child.attribute("directed").as_string("");
            const bool isDirected = directed.empty() ? edgeDefault == "directed" : directed == "true";
            if (!isDirected) {
                throw GraphMLError("неориентированное ребро " + std::string(child.attribute("source").as_string()) +
                                   " — " + std::string(child.attribute("target").as_string()) +
                                   ": гамаки определены только для ориентированных графов");
            }
            if (const char* id = child.attribute("id").as_string(nullptr)) {
                usedIds.insert(id);
            }
            pendingEdges.push_back(child);
        } else if (isElement(child, "hyperedge")) {
            throw GraphMLError("гиперрёбра (<hyperedge>) не поддерживаются");
        }
    }
}

void GraphMLDocument::Impl::resolveEdges(const std::vector<pugi::xml_node>& pendingEdges) {
    const auto resolve = [this](const pugi::xml_node& edge, const char* attributeName) {
        const std::string id = edge.attribute(attributeName).as_string();
        if (id.empty()) {
            throw GraphMLError(std::string("у ребра отсутствует атрибут ") + attributeName);
        }
        if (const auto it = index.find(id); it != index.end()) {
            return it->second;
        }
        if (containerIds.contains(id)) {
            throw GraphMLError("ребро ссылается на узел-контейнер \"" + id +
                               "\"; рёбра к узлам со вложенными графами не поддерживаются");
        }
        throw GraphMLError("ребро ссылается на несуществующий узел \"" + id + "\"");
    };

    edges.reserve(pendingEdges.size());
    for (const pugi::xml_node& edge : pendingEdges) {
        const VertexId source = resolve(edge, "source");
        const VertexId target = resolve(edge, "target");
        const std::size_t slot = digraph.successors(source).size();
        digraph.addEdge(source, target);
        edges.push_back({source, target, slot, edge});
    }
}

GraphMLDocument::GraphMLDocument(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GraphMLDocument::GraphMLDocument(GraphMLDocument&&) noexcept = default;
GraphMLDocument& GraphMLDocument::operator=(GraphMLDocument&&) noexcept = default;
GraphMLDocument::~GraphMLDocument() = default;

GraphMLDocument GraphMLDocument::loadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw GraphMLError("не удалось открыть файл " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw GraphMLError("ошибка чтения файла " + path.string());
    }
    return loadString(buffer.str());
}

GraphMLDocument GraphMLDocument::loadString(std::string_view xml) {
    auto impl = std::make_unique<Impl>();
    impl->parse(xml);
    return GraphMLDocument(std::move(impl));
}

const Digraph& GraphMLDocument::graph() const noexcept { return impl_->digraph; }

const std::string& GraphMLDocument::nodeId(VertexId v) const { return impl_->ids.at(v); }

std::optional<VertexId> GraphMLDocument::findNode(std::string_view id) const {
    if (const auto it = impl_->index.find(id); it != impl_->index.end()) {
        return it->second;
    }
    return std::nullopt;
}

void GraphMLDocument::writeHierarchical(std::ostream& out, const HammockHierarchy& hierarchy) const {
    writeHierarchical(out, hierarchy, nullptr);
}

void GraphMLDocument::writeSugiyama(std::ostream& out, const HammockHierarchy& hierarchy) const {
    const SugiyamaLayout layout = layoutSugiyama(graph(), hierarchy);
    writeHierarchical(out, hierarchy, &layout);
}

void GraphMLDocument::writeHierarchical(std::ostream& out, const HammockHierarchy& hierarchy,
                                        const SugiyamaLayout* layout) const {
    const Impl& source = *impl_;
    const auto clusters = hierarchy.clusters();

    pugi::xml_document result;
    pugi::xml_node declaration = result.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";

    // <graphml>: сохраняем объявления пространств имён исходного документа (например, yEd).
    pugi::xml_node graphml = result.append_child("graphml");
    for (const pugi::xml_attribute& attribute : source.root.attributes()) {
        graphml.append_copy(attribute);
    }
    if (!graphml.attribute("xmlns")) {
        graphml.append_attribute("xmlns") = kGraphMLNamespace;
    }
    if (!graphml.attribute("xmlns:xsi") && !graphml.attribute("xsi:schemaLocation")) {
        graphml.append_attribute("xmlns:xsi") = kXsiNamespace;
        graphml.append_attribute("xsi:schemaLocation") = kSchemaLocation;
    }

    // Порядок содержимого <graphml> по схеме: desc?, key*, (data | graph)*.
    StringSet keyIds;
    std::string labelKey;
    for (const pugi::xml_node& child : source.root.children()) {
        if (isElement(child, "desc") || isElement(child, "key")) {
            graphml.append_copy(child);
        }
        if (isElement(child, "key")) {
            keyIds.insert(child.attribute("id").as_string());
            const std::string_view domain = child.attribute("for").as_string("all");
            if (labelKey.empty() && std::strcmp(child.attribute("attr.name").as_string(), "label") == 0 &&
                (domain == "node" || domain == "all")) {
                labelKey = child.attribute("id").as_string();
            }
        }
    }

    const auto declareKey = [&](const std::string& baseId, const char* name, const char* type,
                                const char* domain = "node") {
        const std::string id = makeUnique(baseId, keyIds);
        pugi::xml_node key = graphml.append_child("key");
        key.append_attribute("id") = id.c_str();
        key.append_attribute("for") = domain;
        key.append_attribute("attr.name") = name;
        key.append_attribute("attr.type") = type;
        return id;
    };
    if (labelKey.empty()) {
        labelKey = declareKey("hl_label", "label", "string");
    }
    const std::string kindKey = declareKey("hl_kind", "hammock.kind", "string");
    const std::string startKey = declareKey("hl_start", "hammock.start", "string");
    const std::string endKey = declareKey("hl_end", "hammock.end", "string");
    const std::string sizeKey = declareKey("hl_size", "hammock.size", "int");
    const std::string depthKey = declareKey("hl_depth", "hammock.depth", "int");

    struct GeometryKeys {
        std::string x, y, width, height, points;
    };
    GeometryKeys geometry;
    if (layout != nullptr) {
        const auto layoutKey = [&](const char* id, const char* name, const char* type, const char* domain) {
            for (const pugi::xml_node& key : source.root.children()) {
                if (isElement(key, "key") && std::string_view(key.attribute("attr.name").as_string()) == name &&
                    std::string_view(key.attribute("attr.type").as_string()) == type &&
                    std::string_view(key.attribute("for").as_string()) == domain) {
                    return std::string(key.attribute("id").as_string());
                }
            }
            return declareKey(id, name, type, domain);
        };
        geometry = {layoutKey("hl_x", "layout.x", "double", "node"),
                    layoutKey("hl_y", "layout.y", "double", "node"),
                    layoutKey("hl_width", "layout.width", "double", "node"),
                    layoutKey("hl_height", "layout.height", "double", "node"),
                    layoutKey("hl_points", "layout.points", "string", "edge")};
    }

    for (const pugi::xml_node& child : source.root.children()) {
        if (isElement(child, "data")) {
            graphml.append_copy(child);
        }
    }

    // <graph> верхнего уровня: атрибуты исходного графа, кроме подсказок разбора parse.*,
    // которые после перестроения структуры стали бы неверными.
    pugi::xml_node topGraph = graphml.append_child("graph");
    for (const pugi::xml_attribute& attribute : source.graph.attributes()) {
        const std::string_view name = attribute.name();
        if (name != "edgedefault" && !name.starts_with("parse.")) {
            topGraph.append_copy(attribute);
        }
    }
    topGraph.append_attribute("edgedefault") = "directed";
    for (const pugi::xml_node& child : source.graph.children()) {
        if (isElement(child, "desc") || isElement(child, "data")) {
            topGraph.append_copy(child);
        }
    }

    // Идентификаторы узлов-кластеров, не совпадающие с существующими.
    StringSet usedIds = source.usedIds;
    std::vector<std::string> clusterIds;
    clusterIds.reserve(clusters.size());
    for (std::size_t c = 0; c < clusters.size(); ++c) {
        clusterIds.push_back(makeUnique("hammock" + std::to_string(c + 1), usedIds));
    }

    // Рёбра группируются по наименьшему общему предку концов.
    std::vector<std::vector<const Impl::EdgeRecord*>> edgesByCluster(clusters.size());
    std::vector<const Impl::EdgeRecord*> topEdges;
    for (const Impl::EdgeRecord& edge : source.edges) {
        const std::size_t owner =
            hierarchy.lowestCommonAncestor(hierarchy.clusterOf(edge.source), hierarchy.clusterOf(edge.target));
        (owner == HammockHierarchy::kRoot ? topEdges : edgesByCluster[owner]).push_back(&edge);
    }

    const auto appendData = [](pugi::xml_node node, const std::string& key, const std::string& value) {
        pugi::xml_node data = node.append_child("data");
        data.append_attribute("key") = key.c_str();
        data.text().set(value.c_str());
    };
    const auto layoutData = [](pugi::xml_node element, const std::string& key) {
        for (const pugi::xml_node& child : element.children()) {
            if (isElement(child, "data") && std::string_view(child.attribute("key").as_string()) == key) {
                return child.text();
            }
        }
        pugi::xml_node child = element.append_child("data");
        child.append_attribute("key") = key.c_str();
        return child.text();
    };
    const auto writeBox = [&](pugi::xml_node node, const LayoutRect& box) {
        layoutData(node, geometry.x).set(box.x);
        layoutData(node, geometry.y).set(box.y);
        layoutData(node, geometry.width).set(box.width);
        layoutData(node, geometry.height).set(box.height);
    };

    // Узел-кластер: описание гамака и вложенный граф с его содержимым.
    const auto appendClusterNode = [&](pugi::xml_node parent, std::size_t c) {
        const HammockHierarchy::Cluster& cluster = clusters[c];
        const std::string& startId = source.ids.at(cluster.hammock.start);
        // У гамака без конечной вершины атрибут hammock.end не записывается.
        const std::string endId = cluster.hammock.hasEnd() ? source.ids.at(cluster.hammock.end) : std::string();

        pugi::xml_node node = parent.append_child("node");
        node.append_attribute("id") = clusterIds[c].c_str();
        const std::string prefix = labelPrefix(cluster.kind);
        const std::string span = cluster.hammock.hasEnd() ? startId + " → " + endId : startId;
        appendData(node, labelKey, prefix + " " + std::to_string(c + 1) + " (" + span + ")");
        appendData(node, kindKey, std::string(toString(cluster.kind)));
        appendData(node, startKey, startId);
        if (cluster.hammock.hasEnd()) {
            appendData(node, endKey, endId);
        }
        appendData(node, sizeKey, std::to_string(cluster.hammock.size));
        appendData(node, depthKey, std::to_string(cluster.depth));
        if (layout != nullptr) {
            writeBox(node, layout->clusters[c]);
        }

        pugi::xml_node nested = node.append_child("graph");
        nested.append_attribute("id") = makeUnique(clusterIds[c] + ":", usedIds).c_str();
        nested.append_attribute("edgedefault") = "directed";
        return nested;
    };

    // Обход дерева кластеров в глубину с явным стеком: уровень = (элемент <graph>, кластер).
    struct Level {
        pugi::xml_node graphElement;
        std::size_t cluster;
    };
    std::vector<Level> pending{{topGraph, HammockHierarchy::kRoot}};
    while (!pending.empty()) {
        Level level = pending.back();
        pending.pop_back();
        for (const HammockHierarchy::Member& member : hierarchy.members(level.cluster)) {
            if (member.kind == HammockHierarchy::Member::Kind::Vertex) {
                const auto node = appendShallowCopy(level.graphElement, source.nodeElements.at(member.index));
                if (layout != nullptr) {
                    writeBox(node, layout->vertices[member.index]);
                }
            } else {
                pending.push_back({appendClusterNode(level.graphElement, member.index), member.index});
            }
        }
        const auto& levelEdges = level.cluster == HammockHierarchy::kRoot ? topEdges : edgesByCluster[level.cluster];
        for (const Impl::EdgeRecord* edge : levelEdges) {
            const auto copy = level.graphElement.append_copy(edge->element);
            if (layout != nullptr) {
                const auto& points = layout->edgeRoutes[edge->source][edge->slot];
                std::ostringstream value;
                value.imbue(std::locale::classic());
                for (std::size_t i = 0; i < points.size(); ++i) {
                    if (i != 0) {
                        value << ' ';
                    }
                    value << points[i].x << ',' << points[i].y;
                }
                layoutData(copy, geometry.points).set(value.str().c_str());
            }
        }
    }

    result.save(out, "  ", pugi::format_indent, pugi::encoding_utf8);
    if (!out) {
        throw GraphMLError("ошибка записи GraphML");
    }
}

}  // namespace hammock
