#include "hammock/hammocks.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

namespace hammock {

HammockSet::HammockSet(Numbering lNumbering, std::vector<Hammock> hammocks)
    : lNumbering_(std::move(lNumbering)), hammocks_(std::move(hammocks)) {
    for (const Hammock& h : hammocks_) {
        if (h.size == 0 || h.last() > lNumbering_.size()) {
            throw std::invalid_argument("HammockSet: гамак выходит за пределы L-нумерации");
        }
    }
}

std::span<const VertexId> HammockSet::vertices(const Hammock& hammock) const {
    return lNumbering_.order().subspan(hammock.first, hammock.size);
}

bool HammockSet::contains(const Hammock& hammock, VertexId v) const {
    if (v >= lNumbering_.size()) {
        return false;  // в том числе kNoVertex
    }
    const std::size_t l = lNumbering_[v];
    return hammock.first <= l && l < hammock.last();
}

HammockSet findHammocks(const ProgramGraph& program) {
    const Digraph& graph = program.graph();
    const std::size_t n = graph.vertexCount();
    const DepthFirstNumberings dfs = computeDepthFirstNumberings(program);
    const Numbering& m = dfs.m;
    Numbering l = computeLNumbering(program, dfs.n);

    std::vector<Hammock> result;
    // predecessorOf[u] == p  ⇔  u — предшественник текущей начальной вершины p.
    std::vector<VertexId> predecessorOf(n, kNoVertex);

    for (std::size_t first = 0; first < n; ++first) {
        const VertexId p = l.vertexAt(first);
        const std::size_t mp = m[p];
        for (const VertexId u : graph.predecessors(p)) {
            predecessorOf[u] = p;
        }

        // Характеристики текущего множества U = { v : first ≤ L(v) < end } (лемма 5):
        //   R(U) = { u ∈ Г⁻¹(U \ {p}) : L(u) < L(p) }  — при R ≠ ∅ перебор прекращается;
        //   F(U) = max { L(u) : u ∈ Г⁻¹(U \ {p}) }     — все такие u имеют L(u) ≥ L(p);
        //   K(U) = { s ∈ Г(U) : M(s) < M(p) }          — храним единственный элемент;
        //   E(U) = max { L(s) : s ∈ Г(U), M(s) > M(p) }.
        std::size_t fMax = first;
        std::optional<std::size_t> eMax;
        VertexId kVertex = kNoVertex;
        bool containsExit = false;  // все расширения U также содержат выход графа

        for (std::size_t end = first + 1; end <= n; ++end) {
            const VertexId v = l.vertexAt(end - 1);

            // Лемма 4: все вершины гамака с началом p имеют M-номер не меньше M(p),
            // поэтому никакое расширение U, содержащее v, гамаком не является.
            if (m[v] < mp) {
                break;
            }
            if (v == program.exit()) {
                // Множество, содержащее выход графа, может быть только гамаком без конечной вершины.
                containsExit = true;
            }

            if (v != p) {
                const auto predecessors = graph.predecessors(v);
                const bool rNonEmpty = std::ranges::any_of(predecessors, [&](VertexId u) { return l[u] < first; });
                if (rNonEmpty) {
                    break;  // R(U) только растёт с ростом U
                }
                for (const VertexId u : predecessors) {
                    fMax = std::max(fMax, l[u]);
                }
            }

            bool kOverflow = false;
            for (const VertexId s : graph.successors(v)) {
                if (m[s] < mp) {
                    if (kVertex == kNoVertex) {
                        kVertex = s;
                    } else if (kVertex != s) {
                        kOverflow = true;
                        break;
                    }
                } else if (m[s] > mp) {
                    eMax = std::max(eMax.value_or(0), l[s]);
                }
            }
            if (kOverflow) {
                break;  // |K(U)| ≥ 2, и K(U) только растёт с ростом U
            }

            // Лемма 5 (L(p) + |U| = end).
            if (fMax >= end) {
                continue;
            }
            const bool allSuccessorsInside = !eMax.has_value() || *eMax < end;
            VertexId q = kNoVertex;
            if (containsExit) {
                // Гамак без конечной вершины: K(U) = ∅ и E(U) < L(p) + |U|, т. е. Г(U) ⊆ U.
                if (kVertex == kNoVertex && allSuccessorsInside) {
                    result.push_back({p, kNoVertex, first, end - first});
                }
                continue;
            }
            if (kVertex == kNoVertex) {
                // K(U) = ∅, L(q) = L(p) + |U| = E(U).
                if (end == n || eMax != end) {
                    continue;
                }
                q = l.vertexAt(end);
            } else {
                // K(U) = {q}, E(U) < L(p) + |U|.
                if (!allSuccessorsInside) {
                    continue;
                }
                q = kVertex;
            }
            if (predecessorOf[q] == p) {
                continue;  // p ∈ Г(q)
            }
            result.push_back({p, q, first, end - first});
        }
    }
    return {std::move(l), std::move(result)};
}

bool isHammock(const ProgramGraph& program, std::span<const VertexId> vertices, VertexId start, VertexId end) {
    const Digraph& graph = program.graph();
    const bool hasEnd = end != kNoVertex;
    if (vertices.empty() || !graph.contains(start) || (hasEnd && !graph.contains(end))) {
        return false;
    }

    std::vector<bool> inU(graph.vertexCount(), false);
    for (const VertexId v : vertices) {
        if (!graph.contains(v) || inU[v]) {
            return false;
        }
        inU[v] = true;
    }
    if (!inU[start] || (hasEnd && inU[end])) {
        return false;
    }

    bool leavesToEnd = false;
    for (const VertexId v : vertices) {
        // 1) Г⁻¹(U \ {p}) ⊆ U
        if (v != start && !std::ranges::all_of(graph.predecessors(v), [&](VertexId u) { return inU[u]; })) {
            return false;
        }
        // 2) Г(U) \ U = {q} (или ∅ для гамака без конечной вершины)
        for (const VertexId s : graph.successors(v)) {
            if (!inU[s]) {
                if (s != end) {
                    return false;
                }
                leavesToEnd = true;
            }
        }
    }
    // 4) вход ∈ U ⇒ p — вход.
    if (inU[program.entry()] && start != program.entry()) {
        return false;
    }
    if (!hasEnd) {
        return true;  // Г(U) ⊆ U; выход графа при этом заведомо принадлежит U
    }
    // 3) p ∉ Г(q);  5) выход ∉ U.
    return leavesToEnd && !graph.hasEdge(end, start) && !inU[program.exit()];
}

}  // namespace hammock
