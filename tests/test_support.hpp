#pragma once

/// @file test_support.hpp
/// @brief Вспомогательные средства тестов: построение графов, генератор
/// случайных графов программ и эталонный перебор гамаков по определению.

#include "hammock/hammocks.hpp"
#include "hammock/linear_components.hpp"
#include "hammock/program_graph.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace hammock::test {

using Edge = std::pair<VertexId, VertexId>;

inline Digraph makeGraph(std::size_t n, const std::vector<Edge>& edges) {
    Digraph graph(n);
    for (const auto& [from, to] : edges) {
        graph.addEdge(from, to);
    }
    return graph;
}

/// Цепочка 0 → 1 → … → n−1.
inline ProgramGraph makeChain(std::size_t n) {
    Digraph graph(n);
    for (VertexId v = 0; v + 1 < n; ++v) {
        graph.addEdge(v, v + 1);
    }
    return {std::move(graph), 0, n - 1};
}

/// Случайный граф программы из @p n ≥ 2 вершин с перемешанными номерами вершин.
/// Допускаются циклы, петли и кратные дуги.
inline ProgramGraph randomProgramGraph(std::mt19937& rng, std::size_t n, std::size_t extraEdges) {
    std::vector<VertexId> id(n);
    std::iota(id.begin(), id.end(), VertexId{0});
    std::shuffle(id.begin(), id.end(), rng);
    // Логическая вершина 0 — вход, n−1 — выход.
    const auto pick = [&](std::size_t lo, std::size_t hi) {
        return std::uniform_int_distribution<std::size_t>(lo, hi)(rng);
    };

    Digraph graph(n);
    for (std::size_t v = 1; v + 1 < n; ++v) {
        graph.addEdge(id[pick(0, v - 1)], id[v]);      // достижимость из входа
        graph.addEdge(id[v], id[pick(v + 1, n - 1)]);  // достижимость выхода
    }
    if (n == 2 || std::bernoulli_distribution(0.3)(rng)) {
        graph.addEdge(id[0], id[n - 1]);
    }
    for (std::size_t i = 0; i < extraEdges && n > 2; ++i) {
        graph.addEdge(id[pick(0, n - 2)], id[pick(1, n - 1)]);
    }
    return {std::move(graph), id[0], id[n - 1]};
}

/// Гамак в каноническом виде: (отсортированные вершины, начало, конец).
using HammockKey = std::tuple<std::vector<VertexId>, VertexId, VertexId>;

inline HammockKey makeKey(std::vector<VertexId> vertices, VertexId start, VertexId end) {
    std::ranges::sort(vertices);
    return {std::move(vertices), start, end};
}

inline std::set<HammockKey> keysOf(const HammockSet& hammocks) {
    std::set<HammockKey> result;
    for (const Hammock& h : hammocks) {
        const auto vertices = hammocks.vertices(h);
        result.insert(makeKey({vertices.begin(), vertices.end()}, h.start, h.end));
    }
    return result;
}

inline std::set<HammockKey> keysOf(const std::vector<LinearComponent>& components) {
    std::set<HammockKey> result;
    for (const LinearComponent& lc : components) {
        result.insert(makeKey(lc.vertices, lc.start, lc.end));
    }
    return result;
}

/// Эталон: перебор всех подмножеств вершин (n ≤ 16) с проверкой определения гамака.
inline std::set<HammockKey> bruteForceHammocks(const ProgramGraph& program) {
    const Digraph& graph = program.graph();
    const std::size_t n = graph.vertexCount();
    std::set<HammockKey> result;
    for (std::uint32_t mask = 1; mask < (std::uint32_t{1} << n); ++mask) {
        std::vector<VertexId> vertices;
        for (VertexId v = 0; v < n; ++v) {
            if ((mask >> v) & 1U) {
                vertices.push_back(v);
            }
        }
        std::set<VertexId> exits;
        for (const VertexId v : vertices) {
            for (const VertexId s : graph.successors(v)) {
                if (((mask >> s) & 1U) == 0) {
                    exits.insert(s);
                }
            }
        }
        if (exits.size() > 1) {
            continue;
        }
        const VertexId q = exits.empty() ? kNoVertex : *exits.begin();
        for (const VertexId p : vertices) {
            if (isHammock(program, vertices, p, q)) {
                result.insert(makeKey(vertices, p, q));
            }
        }
    }
    return result;
}

/// Достижима ли вершина @p to из @p from, если вершина @p removed удалена из графа.
inline bool reachable(const Digraph& graph, VertexId from, VertexId to, VertexId removed = kNoVertex) {
    if (from == removed) {
        return false;
    }
    std::vector<bool> seen(graph.vertexCount(), false);
    std::vector<VertexId> stack{from};
    seen[from] = true;
    while (!stack.empty()) {
        const VertexId v = stack.back();
        stack.pop_back();
        if (v == to) {
            return true;
        }
        for (const VertexId s : graph.successors(v)) {
            if (!seen[s] && s != removed) {
                seen[s] = true;
                stack.push_back(s);
            }
        }
    }
    return false;
}

/// Эталон: линейные компоненты по определению — гамаки, минимальные по включению
/// среди гамаков, у которых нет пути q → p и p, q лежат на каждом пути от входа к выходу.
inline std::set<HammockKey> bruteForceLinearComponents(const ProgramGraph& program) {
    const Digraph& graph = program.graph();
    const auto onEveryPath = [&](VertexId v) {
        return v == program.entry() || v == program.exit() || !reachable(graph, program.entry(), program.exit(), v);
    };

    std::vector<HammockKey> candidates;
    // Линейная компонента определена через конечную вершину q.
    for (const HammockKey& key : bruteForceHammocks(program)) {
        const auto& [vertices, p, q] = key;
        if (q != kNoVertex && !reachable(graph, q, p) && onEveryPath(p) && onEveryPath(q)) {
            candidates.push_back(key);
        }
    }

    std::set<HammockKey> result;
    for (const HammockKey& candidate : candidates) {
        const auto& u = std::get<0>(candidate);
        const bool minimal = std::ranges::none_of(candidates, [&](const HammockKey& other) {
            const auto& w = std::get<0>(other);
            return w.size() < u.size() && std::ranges::includes(u, w);
        });
        if (minimal) {
            result.insert(candidate);
        }
    }
    return result;
}

}  // namespace hammock::test
