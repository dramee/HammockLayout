#include "hammock/digraph.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace hammock {

Digraph::Digraph(std::size_t vertexCount) : successors_(vertexCount), predecessors_(vertexCount) {}

VertexId Digraph::addVertex() {
    successors_.emplace_back();
    predecessors_.emplace_back();
    return successors_.size() - 1;
}

void Digraph::addEdge(VertexId from, VertexId to) {
    checkVertex(from);
    checkVertex(to);
    successors_[from].push_back(to);
    predecessors_[to].push_back(from);
    ++edgeCount_;
}

std::span<const VertexId> Digraph::successors(VertexId v) const {
    checkVertex(v);
    return successors_[v];
}

std::span<const VertexId> Digraph::predecessors(VertexId v) const {
    checkVertex(v);
    return predecessors_[v];
}

bool Digraph::hasEdge(VertexId from, VertexId to) const {
    checkVertex(from);
    checkVertex(to);
    const auto& out = successors_[from];
    const auto& in = predecessors_[to];
    if (out.size() <= in.size()) {
        return std::ranges::find(out, to) != out.end();
    }
    return std::ranges::find(in, from) != in.end();
}

void Digraph::checkVertex(VertexId v) const {
    if (!contains(v)) {
        throw std::out_of_range("Digraph: вершина " + std::to_string(v) + " не существует (всего " +
                                std::to_string(vertexCount()) + ")");
    }
}

}  // namespace hammock
