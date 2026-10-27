#pragma once

#include "hammock/digraph.hpp"
#include "hammock/program_graph.hpp"

#include <vector>

namespace hammock {

/// Линейная компонента графа программы.
struct LinearComponent {
    VertexId start = kNoVertex;      ///< начальная вершина p_i
    VertexId end = kNoVertex;        ///< конечная вершина p_{i+1}
    std::vector<VertexId> vertices;  ///< вершины компоненты в порядке N-нумерации
};


[[nodiscard]] std::vector<LinearComponent> findLinearComponents(const ProgramGraph& program);

}  // namespace hammock
