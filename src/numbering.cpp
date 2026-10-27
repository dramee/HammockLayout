#include "hammock/numbering.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace hammock {

Numbering::Numbering(std::vector<VertexId> order) : order_(std::move(order)), number_(order_.size(), kNoVertex) {
    for (std::size_t i = 0; i < order_.size(); ++i) {
        const VertexId v = order_[i];
        if (v >= order_.size() || number_[v] != kNoVertex) {
            throw std::invalid_argument("Numbering: последовательность вершин не является перестановкой");
        }
        number_[v] = i;
    }
}

DepthFirstNumberings computeDepthFirstNumberings(const ProgramGraph& program) {
    const Digraph& graph = program.graph();
    const std::size_t n = graph.vertexCount();

    struct PathEntry {
        VertexId vertex;
        std::size_t nextSuccessor;  // позиция первого непросмотренного преемника
    };

    std::vector<bool> visited(n, false);
    std::vector<VertexId> preorder;
    std::vector<VertexId> postorder;
    preorder.reserve(n);
    postorder.reserve(n);

    std::vector<PathEntry> path;
    path.reserve(n);
    path.push_back({program.entry(), 0});
    visited[program.entry()] = true;
    preorder.push_back(program.entry());

    while (!path.empty()) {
        PathEntry& top = path.back();
        const auto successors = graph.successors(top.vertex);
        // Ищем преемника, который ранее не включался в путь.
        while (top.nextSuccessor < successors.size() && visited[successors[top.nextSuccessor]]) {
            ++top.nextSuccessor;
        }
        if (top.nextSuccessor < successors.size()) {
            const VertexId next = successors[top.nextSuccessor++];
            visited[next] = true;
            preorder.push_back(next);
            path.push_back({next, 0});  // ссылка top после push_back недействительна
        } else {
            postorder.push_back(top.vertex);
            path.pop_back();
        }
    }

    // Граф программы гарантирует достижимость всех вершин из входа.
    if (preorder.size() != n) {
        throw std::logic_error("computeDepthFirstNumberings: не все вершины достижимы из входа");
    }
    std::ranges::reverse(postorder);
    return {Numbering(std::move(preorder)), Numbering(std::move(postorder))};
}

Numbering computeLNumbering(const ProgramGraph& program, const Numbering& nNumbering) {
    const Digraph& graph = program.graph();
    const std::size_t n = graph.vertexCount();

    std::vector<VertexId> order(nNumbering.order().begin(), nNumbering.order().end());
    std::vector<std::size_t> position(n);
    for (std::size_t i = 0; i < n; ++i) {
        position[order[i]] = i;
    }

    std::vector<bool> inS(n, false);
    std::vector<VertexId> sMembers;
    std::vector<VertexId> stack;
    std::vector<VertexId> buffer;
    sMembers.reserve(n);
    stack.reserve(n);
    buffer.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        const VertexId p = order[i];

        // S(p): обратный обход от p по вершинам с L-номером не меньше L(p).
        sMembers.clear();
        stack.assign(1, p);
        inS[p] = true;
        while (!stack.empty()) {
            const VertexId v = stack.back();
            stack.pop_back();
            sMembers.push_back(v);
            for (const VertexId u : graph.predecessors(v)) {
                if (!inS[u] && position[u] >= i) {
                    inS[u] = true;
                    stack.push_back(u);
                }
            }
        }

        // Устойчивое разбиение суффикса [i, n): сначала S(p), затем остальные.
        // Вершина p остаётся на позиции i, так как она первая в S(p).
        if (sMembers.size() > 1 && sMembers.size() < n - i) {
            buffer.clear();
            for (std::size_t j = i; j < n; ++j) {
                if (inS[order[j]]) {
                    buffer.push_back(order[j]);
                }
            }
            for (std::size_t j = i; j < n; ++j) {
                if (!inS[order[j]]) {
                    buffer.push_back(order[j]);
                }
            }
            std::ranges::copy(buffer, order.begin() + static_cast<std::ptrdiff_t>(i));
            for (std::size_t j = i; j < n; ++j) {
                position[order[j]] = j;
            }
        }

        for (const VertexId v : sMembers) {
            inS[v] = false;
        }
    }
    return Numbering(std::move(order));
}

}  // namespace hammock
