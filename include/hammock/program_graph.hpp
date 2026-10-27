#pragma once

#include "hammock/digraph.hpp"

#include <stdexcept>
#include <string>

namespace hammock {

/// Ошибка: граф не удовлетворяет определению графа программы.
class ProgramGraphError : public std::runtime_error {
public:
    explicit ProgramGraphError(const std::string& message, VertexId vertex = kNoVertex)
        : std::runtime_error(message), vertex_(vertex) {}

    /// Вершина, нарушающая инвариант (kNoVertex, если ошибка не связана с конкретной вершиной).
    [[nodiscard]] VertexId vertex() const noexcept { return vertex_; }

private:
    VertexId vertex_;
};


class ProgramGraph {
public:
    /// @throws ProgramGraphError если инварианты нарушены.
    ProgramGraph(Digraph graph, VertexId entry, VertexId exit);

    /// Определяет вход и выход автоматически: вход — единственная вершина
    /// без предшественников, выход — единственная вершина без преемников.
    /// @throws ProgramGraphError если такие вершины не единственны или инварианты нарушены.
    [[nodiscard]] static ProgramGraph withDetectedTerminals(Digraph graph);

    [[nodiscard]] const Digraph& graph() const noexcept { return graph_; }
    [[nodiscard]] VertexId entry() const noexcept { return entry_; }
    [[nodiscard]] VertexId exit() const noexcept { return exit_; }
    [[nodiscard]] std::size_t vertexCount() const noexcept { return graph_.vertexCount(); }

private:
    void validate() const;

    Digraph graph_;
    VertexId entry_;
    VertexId exit_;
};

}  // namespace hammock
