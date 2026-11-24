#include "hammock/sugiyama.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace hammock {
namespace {

using Member = HammockHierarchy::Member;
constexpr auto kRoot = HammockHierarchy::kRoot;
constexpr double kPadding = 28;
constexpr double kColumnGap = 32;
constexpr double kLayerGap = 52;
constexpr double kLoopGap = 12;
constexpr double kChannelGap = 8;

struct Edge {
    VertexId source;
    VertexId target;
    std::size_t slot;
    std::size_t owner;
    std::size_t from;
    std::size_t to;
    std::vector<LayoutPoint> bends;
};

struct LayerNode {
    LayoutRect box;
    std::size_t rank;
    double upperChannel = 0;
    double lowerChannel = 0;
};

struct Channels {
    std::vector<std::pair<double, double>> vertices;
    std::vector<std::pair<double, double>> clusters;

    auto& at(const Member& member) {
        return member.kind == Member::Kind::Vertex ? vertices[member.index] : clusters[member.index];
    }
};

LayoutRect& boxOf(SugiyamaLayout& layout, const Member& member) {
    return member.kind == Member::Kind::Vertex ? layout.vertices[member.index] : layout.clusters[member.index];
}

// Обратный порядок завершения DFS. Дуги, направленные против этого порядка,
// временно разворачиваем при вычислении слоёв.
std::vector<std::size_t> depthFirstOrder(const std::vector<std::vector<std::size_t>>& successors) {
    struct Frame {
        std::size_t node;
        std::size_t next;
    };
    std::vector<std::size_t> order;
    std::vector<Frame> stack;
    std::vector<bool> seen(successors.size(), false);
    for (std::size_t node = 0; node < successors.size(); ++node) {
        if (seen[node]) {
            continue;
        }
        stack.push_back({node, 0});
        seen[node] = true;
        while (!stack.empty()) {
            Frame& frame = stack.back();
            if (frame.next == successors[frame.node].size()) {
                order.push_back(frame.node);
                stack.pop_back();
                continue;
            }
            const std::size_t next = successors[frame.node][frame.next++];
            if (!seen[next]) {
                seen[next] = true;
                stack.push_back({next, 0});
            }
        }
    }
    std::ranges::reverse(order);
    return order;
}

void orderLayers(std::vector<std::vector<std::size_t>>& layers,
                 const std::vector<std::vector<std::size_t>>& above,
                 const std::vector<std::vector<std::size_t>>& below) {
    std::vector<std::size_t> position(above.size());
    std::vector<double> barycenter(above.size());
    const auto updatePositions = [&](const std::vector<std::size_t>& layer) {
        for (std::size_t i = 0; i < layer.size(); ++i) {
            position[layer[i]] = i;
        }
    };
    for (const auto& layer : layers) {
        updatePositions(layer);
    }
    const auto reorder = [&](auto& layer, const auto& neighbors) {
        for (const std::size_t node : layer) {
            double sum = 0;
            for (const std::size_t neighbor : neighbors[node]) {
                sum += static_cast<double>(position[neighbor]);
            }
            barycenter[node] = neighbors[node].empty() ? static_cast<double>(position[node])
                                                       : sum / static_cast<double>(neighbors[node].size());
        }
        std::ranges::stable_sort(layer, {}, [&](std::size_t node) { return barycenter[node]; });
        updatePositions(layer);
    };
    // Два прохода вниз и два вверх с барицентрической эвристикой.
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 1; i < layers.size(); ++i) {
            reorder(layers[i], above);
        }
        for (std::size_t i = layers.size(); i > 1; --i) {
            reorder(layers[i - 2], below);
        }
    }
}

LayoutRect packLayers(std::vector<LayerNode>& nodes, const std::vector<std::vector<std::size_t>>& layers) {
    std::vector<double> widths(layers.size(), 0);
    std::vector<double> heights(layers.size(), 0);
    for (std::size_t i = 0; i < layers.size(); ++i) {
        for (const std::size_t node : layers[i]) {
            widths[i] += nodes[node].box.width + kColumnGap;
            heights[i] = std::max(heights[i], nodes[node].box.height);
        }
        widths[i] -= kColumnGap;
    }
    const double width = *std::ranges::max_element(widths);
    double y = kPadding;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        double x = kPadding + (width - widths[i]) / 2;
        for (const std::size_t node : layers[i]) {
            LayerNode& item = nodes[node];
            LayoutRect& box = item.box;
            box.x = x;
            box.y = y + (heights[i] - box.height) / 2;
            item.upperChannel = y - kChannelGap;
            item.lowerChannel = y + heights[i] + kChannelGap;
            x += box.width + kColumnGap;
        }
        y += heights[i] + kLayerGap;
    }
    return {0, 0, width + 2 * kPadding, y - kLayerGap + kPadding};
}

LayoutRect layoutLevel(std::span<const Member> members, std::span<const std::size_t> edgeIds,
                       std::vector<Edge>& edges, SugiyamaLayout& layout, Channels& channels) {
    if (members.empty()) {
        return {};
    }
    const std::size_t count = members.size();
    std::vector<std::vector<std::size_t>> successors(count);
    for (const std::size_t i : edgeIds) {
        const Edge& edge = edges[i];
        if (edge.from != edge.to) {
            successors[edge.from].push_back(edge.to);
        }
    }
    const auto order = depthFirstOrder(successors);
    std::vector<std::size_t> orderIndex(count);
    for (std::size_t i = 0; i < count; ++i) {
        orderIndex[order[i]] = i;
        successors[i].clear();
    }

    const auto ranksForward = [&](const Edge& edge) { return orderIndex[edge.from] < orderIndex[edge.to]; };
    for (const std::size_t i : edgeIds) {
        const Edge& edge = edges[i];
        if (edge.from != edge.to) {
            const auto [from, to] = ranksForward(edge) ? std::pair{edge.from, edge.to}
                                                      : std::pair{edge.to, edge.from};
            successors[from].push_back(to);
        }
    }
    std::vector<std::size_t> rank(count, 0);
    for (const std::size_t from : order) {
        for (const std::size_t to : successors[from]) {
            rank[to] = std::max(rank[to], rank[from] + 1);
        }
    }

    std::vector<LayerNode> nodes;
    nodes.reserve(count);
    std::vector<std::vector<std::size_t>> above(count);
    std::vector<std::vector<std::size_t>> below(count);
    for (std::size_t i = 0; i < count; ++i) {
        nodes.push_back({boxOf(layout, members[i]), rank[i]});
    }
    // Фиктивная вершина на каждом промежуточном слое длинной дуги.
    // Идентификаторы фиктивных вершин последовательны и записываются в порядке дуг.
    const auto link = [&](std::size_t upper, std::size_t lower) {
        below[upper].push_back(lower);
        above[lower].push_back(upper);
    };
    for (const std::size_t i : edgeIds) {
        const Edge& edge = edges[i];
        if (edge.from == edge.to) {
            continue;
        }
        const auto [from, to] = ranksForward(edge) ? std::pair{edge.from, edge.to}
                                                  : std::pair{edge.to, edge.from};
        std::size_t previous = from;
        for (std::size_t r = rank[from] + 1; r < rank[to]; ++r) {
            const std::size_t dummy = nodes.size();
            nodes.push_back({{0, 0, kLoopGap, 0}, r});
            above.emplace_back();
            below.emplace_back();
            link(previous, dummy);
            previous = dummy;
        }
        link(previous, to);
    }
    std::vector<std::vector<std::size_t>> layers(*std::ranges::max_element(rank) + 1);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        layers[nodes[i].rank].push_back(i);
    }
    orderLayers(layers, above, below);
    const LayoutRect bounds = packLayers(nodes, layers);
    for (std::size_t i = 0; i < count; ++i) {
        boxOf(layout, members[i]) = nodes[i].box;
        channels.at(members[i]) = {nodes[i].upperChannel, nodes[i].lowerChannel};
    }

    std::size_t dummy = count;
    for (const std::size_t i : edgeIds) {
        Edge& edge = edges[i];
        if (edge.from != edge.to) {
            const auto [low, high] = std::minmax(rank[edge.from], rank[edge.to]);
            for (std::size_t r = low + 1; r < high; ++r) {
                const LayerNode& item = nodes[dummy++];
                const double x = item.box.x + item.box.width / 2;
                edge.bends.push_back({x, item.upperChannel});
                edge.bends.push_back({x, item.lowerChannel});
            }
            if (!ranksForward(edge)) {
                std::ranges::reverse(edge.bends);
            }
        }
    }
    return bounds;
}

enum class Port { Top, Bottom, Right };

void appendPorts(std::vector<LayoutPoint>& route, VertexId v, std::size_t owner, Port port,
                 const SugiyamaLayout& layout, const HammockHierarchy& hierarchy, const Channels& channels) {
    const LayoutRect& node = layout.vertices[v];
    const bool bottom = port != Port::Top;
    double x = node.x + node.width / 2;
    if (port == Port::Right) {
        const double y = node.y + node.height / 2;
        const double right = node.x + node.width;
        route.push_back({right, y});
        x = right + kLoopGap;
        route.push_back({x, y});
    } else {
        route.push_back({x, bottom ? node.y + node.height : node.y});
    }
    auto channel = channels.vertices[v];
    for (std::size_t c = hierarchy.clusterOf(v); c != owner; c = hierarchy.cluster(c).parent) {
        // Горизонтальный участок проходит между рядами. Из кластера дуга
        // выходит через боковой зазор между содержимым и его границей.
        const double y = bottom ? channel.second : channel.first;
        const LayoutRect& box = layout.clusters[c];
        route.push_back({x, y});
        x = box.x + box.width - kPadding / 2;
        route.push_back({x, y});
        route.push_back({x, bottom ? box.y + box.height : box.y});
        channel = channels.clusters[c];
    }
    route.push_back({x, bottom ? channel.second : channel.first});
}

}  // namespace

SugiyamaLayout layoutSugiyama(const Digraph& graph, const HammockHierarchy& hierarchy) {
    const std::size_t n = graph.vertexCount();
    const std::size_t top = hierarchy.clusters().size();
    SugiyamaLayout layout;
    layout.vertices.assign(n, {0, 0, 104, 36});
    layout.clusters.resize(top);
    layout.edgeRoutes.resize(n);
    Channels channels;
    channels.vertices.resize(n);
    channels.clusters.resize(top);
    std::vector<std::size_t> vertexSlot(n);
    std::vector<std::size_t> clusterSlot(top);
    std::size_t vertexCount = 0;
    for (std::size_t level = 0; level <= top; ++level) {
        const auto members = hierarchy.members(level == top ? kRoot : level);
        for (std::size_t i = 0; i < members.size(); ++i) {
            const Member& member = members[i];
            if (member.kind == Member::Kind::Vertex) {
                vertexSlot.at(member.index) = i;
                ++vertexCount;
            } else {
                clusterSlot[member.index] = i;
            }
        }
    }
    if (vertexCount != n) {
        throw std::invalid_argument("layoutSugiyama: иерархия и граф содержат разное число вершин");
    }
    const auto project = [&](VertexId v, std::size_t owner) {
        std::size_t c = hierarchy.clusterOf(v);
        if (c == owner) {
            return vertexSlot[v];
        }
        while (hierarchy.cluster(c).parent != owner) {
            c = hierarchy.cluster(c).parent;
        }
        return clusterSlot[c];
    };
    std::vector<Edge> edges;
    edges.reserve(graph.edgeCount());
    std::vector<std::vector<std::size_t>> levelEdges(top + 1);
    for (VertexId v = 0; v < n; ++v) {
        const auto successors = graph.successors(v);
        layout.edgeRoutes[v].resize(successors.size());
        for (std::size_t slot = 0; slot < successors.size(); ++slot) {
            const VertexId w = successors[slot];
            const auto owner = hierarchy.lowestCommonAncestor(hierarchy.clusterOf(v), hierarchy.clusterOf(w));
            levelEdges[owner == kRoot ? top : owner].push_back(edges.size());
            edges.push_back({v, w, slot, owner, project(v, owner), project(w, owner), {}});
        }
    }

    // Сначала размеры дочерних кластеров, затем их положение на уровне родителя.
    for (std::size_t i = top; i > 0; --i) {
        const std::size_t c = i - 1;
        layout.clusters[c] = layoutLevel(hierarchy.members(c), levelEdges[c], edges, layout, channels);
    }
    layoutLevel(hierarchy.topLevelMembers(), levelEdges[top], edges, layout, channels);
    for (std::size_t c = 0; c < top; ++c) {
        const LayoutRect parent = layout.clusters[c];
        for (const Member& member : hierarchy.members(c)) {
            LayoutRect& box = boxOf(layout, member);
            box.x += parent.x;
            box.y += parent.y;
            auto& [upper, lower] = channels.at(member);
            upper += parent.y;
            lower += parent.y;
        }
    }

    for (const Edge& edge : edges) {
        auto& route = layout.edgeRoutes[edge.source][edge.slot];
        const LayoutRect& source = layout.vertices[edge.source];
        if (edge.source == edge.target) {
            const double right = source.x + source.width;
            const double middle = source.y + source.height / 2;
            const double x = source.x + source.width / 2;
            route = {{x, source.y}, {x, source.y - kLoopGap}, {right + kLoopGap, source.y - kLoopGap},
                     {right + kLoopGap, middle}, {right, middle}};
            continue;
        }
        const auto members = hierarchy.members(edge.owner);
        const bool forward = boxOf(layout, members[edge.from]).y < boxOf(layout, members[edge.to]).y;
        appendPorts(route, edge.source, edge.owner, forward ? Port::Bottom : Port::Top, layout, hierarchy, channels);
        const LayoutRect offset = edge.owner == kRoot ? LayoutRect{} : layout.clusters[edge.owner];
        for (const LayoutPoint& bend : edge.bends) {
            route.push_back({bend.x + offset.x, bend.y + offset.y});
        }
        std::vector<LayoutPoint> targetPorts;
        appendPorts(targetPorts, edge.target, edge.owner, forward ? Port::Top : Port::Right, layout, hierarchy,
                    channels);
        route.insert(route.end(), targetPorts.rbegin(), targetPorts.rend());
    }
    return layout;
}

}  // namespace hammock
