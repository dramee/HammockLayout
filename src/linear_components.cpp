#include "hammock/linear_components.hpp"

#include "hammock/numbering.hpp"

#include <algorithm>
#include <stdexcept>

namespace hammock {
namespace {

struct Bicomponent {
    VertexId root = kNoVertex;
    std::vector<VertexId> vertices;
    bool singleEntry = true;
};

}  // namespace

std::vector<LinearComponent> findLinearComponents(const ProgramGraph& program) {
    const Digraph& graph = program.graph();
    const std::size_t n = graph.vertexCount();
    const Numbering nNumbering = computeDepthFirstNumberings(program).n;

    // Этап 1. Бикомпоненты в порядке возрастания N-номеров их бивершин.
    constexpr auto kUnassigned = static_cast<std::size_t>(-1);
    std::vector componentOf(n, kUnassigned);
    std::vector<Bicomponent> components;
    std::vector<VertexId> stack;

    for (const VertexId p : nNumbering.order()) {
        if (componentOf[p] != kUnassigned) {
            continue;
        }
        const std::size_t index = components.size();
        Bicomponent& component = components.emplace_back();
        component.root = p;

        // S(p): вершины, из которых p достижима в графе без уже просмотренных вершин.
        stack.assign(1, p);
        componentOf[p] = index;
        while (!stack.empty()) {
            const VertexId v = stack.back();
            stack.pop_back();
            component.vertices.push_back(v);
            for (const VertexId u : graph.predecessors(v)) {
                if (componentOf[u] == kUnassigned) {
                    componentOf[u] = index;
                    stack.push_back(u);
                }
            }
        }

        // Лемма 2: S(p) одновходова ⇔ ни у одной вершины S(p) \ {p} нет
        // предшественника с N-номером меньше N(p).
        const std::size_t np = nNumbering[p];
        for (const VertexId v : component.vertices) {
            if (v != p && std::ranges::any_of(graph.predecessors(v), [&](VertexId u) { return nNumbering[u] < np; })) {
                component.singleEntry = false;
                break;
            }
        }
        std::ranges::sort(component.vertices, {}, [&](VertexId v) { return nNumbering[v]; });
    }

    // Этап 2. Выделение вершин p_1, …, p_{k+1}:
    //   H(p) = max { N(q) : q — бивершина, Г(S(p')) ∩ S(q) ≠ ∅ для бивершины p' с N(p') < N(p) }.
    std::vector<std::size_t> pivots;  // индексы бикомпонент S(p_i)
    std::size_t h = 0;                // H для текущей бивершины (N(входа) = 0)
    for (std::size_t c = 0; c < components.size(); ++c) {
        const Bicomponent& component = components[c];
        if (component.singleEntry && nNumbering[component.root] == h) {
            pivots.push_back(c);
        }
        for (const VertexId v : component.vertices) {
            for (const VertexId s : graph.successors(v)) {
                h = std::max(h, nNumbering[components[componentOf[s]].root]);
            }
        }
    }

    if (pivots.empty() || components[pivots.front()].root != program.entry() ||
        components[pivots.back()].root != program.exit()) {
        throw std::logic_error("findLinearComponents: нарушены инварианты графа программы");
    }

    // Лемма 3: U_i = ∪ { S(q) : N(p_i) ≤ N(q) < N(p_{i+1}) }.
    std::vector<LinearComponent> result;
    result.reserve(pivots.size() - 1);
    for (std::size_t i = 0; i + 1 < pivots.size(); ++i) {
        LinearComponent& lc = result.emplace_back();
        lc.start = components[pivots[i]].root;
        lc.end = components[pivots[i + 1]].root;
        for (std::size_t c = pivots[i]; c < pivots[i + 1]; ++c) {
            lc.vertices.insert(lc.vertices.end(), components[c].vertices.begin(), components[c].vertices.end());
        }
    }
    return result;
}

}  // namespace hammock
