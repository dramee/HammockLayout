#include "hammock/program_graph.hpp"

#include <utility>
#include <vector>

namespace hammock {
namespace {

enum class Direction { Forward, Backward };

/// Множество вершин, достижимых из @p start по дугам (Forward) или против дуг (Backward).
std::vector<bool> reachableFrom(const Digraph& graph, VertexId start, Direction direction) {
    std::vector<bool> visited(graph.vertexCount(), false);
    std::vector<VertexId> stack{start};
    visited[start] = true;
    while (!stack.empty()) {
        const VertexId v = stack.back();
        stack.pop_back();
        const auto next = direction == Direction::Forward ? graph.successors(v) : graph.predecessors(v);
        for (const VertexId w : next) {
            if (!visited[w]) {
                visited[w] = true;
                stack.push_back(w);
            }
        }
    }
    return visited;
}

std::string vertexName(VertexId v) { return "#" + std::to_string(v); }

}  // namespace

ProgramGraph::ProgramGraph(Digraph graph, VertexId entry, VertexId exit)
    : graph_(std::move(graph)), entry_(entry), exit_(exit) {
    validate();
}

ProgramGraph ProgramGraph::withDetectedTerminals(Digraph graph) {
    VertexId entry = kNoVertex;
    VertexId exit = kNoVertex;
    for (VertexId v = 0; v < graph.vertexCount(); ++v) {
        if (graph.predecessors(v).empty()) {
            if (entry != kNoVertex) {
                throw ProgramGraphError("в графе несколько вершин без предшественников (" + vertexName(entry) + ", " +
                                            vertexName(v) + "); укажите вход явно",
                                        v);
            }
            entry = v;
        }
        if (graph.successors(v).empty()) {
            if (exit != kNoVertex) {
                throw ProgramGraphError("в графе несколько вершин без преемников (" + vertexName(exit) + ", " +
                                            vertexName(v) + "); укажите выход явно",
                                        v);
            }
            exit = v;
        }
    }
    if (entry == kNoVertex) {
        throw ProgramGraphError("в графе нет вершины без предшественников, которая могла бы быть входом");
    }
    if (exit == kNoVertex) {
        throw ProgramGraphError("в графе нет вершины без преемников, которая могла бы быть выходом");
    }
    return ProgramGraph(std::move(graph), entry, exit);
}

void ProgramGraph::validate() const {
    if (!graph_.contains(entry_)) {
        throw ProgramGraphError("вход " + vertexName(entry_) + " не является вершиной графа", entry_);
    }
    if (!graph_.contains(exit_)) {
        throw ProgramGraphError("выход " + vertexName(exit_) + " не является вершиной графа", exit_);
    }
    if (entry_ == exit_) {
        throw ProgramGraphError("вход и выход графа программы должны различаться");
    }
    if (!graph_.predecessors(entry_).empty()) {
        throw ProgramGraphError("во вход " + vertexName(entry_) + " заходят дуги", entry_);
    }
    if (!graph_.successors(exit_).empty()) {
        throw ProgramGraphError("из выхода " + vertexName(exit_) + " исходят дуги", exit_);
    }

    const auto fromEntry = reachableFrom(graph_, entry_, Direction::Forward);
    const auto toExit = reachableFrom(graph_, exit_, Direction::Backward);
    for (VertexId v = 0; v < graph_.vertexCount(); ++v) {
        if (!fromEntry[v]) {
            throw ProgramGraphError("вершина " + vertexName(v) + " недостижима из входа", v);
        }
        if (!toExit[v]) {
            throw ProgramGraphError("из вершины " + vertexName(v) + " недостижим выход", v);
        }
    }
}

}  // namespace hammock
