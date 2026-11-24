#pragma once

#include "hammock/hierarchy.hpp"

#include <vector>

namespace hammock {

struct LayoutPoint {
    double x = 0;
    double y = 0;

    friend bool operator==(const LayoutPoint&, const LayoutPoint&) = default;
};

struct LayoutRect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;

    friend bool operator==(const LayoutRect&, const LayoutRect&) = default;
};

struct SugiyamaLayout {
    std::vector<LayoutRect> vertices;  ///< По VertexId.
    std::vector<LayoutRect> clusters;  ///< По индексу кластера в HammockHierarchy.
    /// Полилиния в исходном направлении дуги. Обратные дуги и петли входят в вершину справа.
    /// Индексы — вершина и позиция в successors().
    std::vector<std::vector<std::vector<LayoutPoint>>> edgeRoutes;
};

/// Строит послойную раскладку сверху вниз. Координаты абсолютные, начало в левом верхнем углу.
/// Вершины имеют фиксированный размер, границы гамаков охватывают их вложенное содержимое.
/// Иерархия должна быть построена для вершин переданного графа.
[[nodiscard]] SugiyamaLayout layoutSugiyama(const Digraph& graph, const HammockHierarchy& hierarchy);

}  // namespace hammock
