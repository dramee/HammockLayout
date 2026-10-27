#pragma once

#include "hammock/digraph.hpp"
#include "hammock/program_graph.hpp"

#include <span>
#include <vector>

namespace hammock {

/// @brief Нумерация вершин — биекция X → {0, …, n−1}.
class Numbering {
public:
    Numbering() = default;

    /// Строит нумерацию по перестановке вершин: вершина order[i] получает номер i.
    /// @throws std::invalid_argument если @p order не является перестановкой {0, …, n−1}.
    explicit Numbering(std::vector<VertexId> order);

    /// Номер вершины @p v.
    [[nodiscard]] std::size_t operator[](VertexId v) const { return number_[v]; }

    /// Вершина с номером @p number.
    [[nodiscard]] VertexId vertexAt(std::size_t number) const { return order_[number]; }

    /// Вершины в порядке возрастания номеров.
    [[nodiscard]] std::span<const VertexId> order() const noexcept { return order_; }

    [[nodiscard]] std::size_t size() const noexcept { return order_.size(); }

private:
    std::vector<VertexId> order_;
    std::vector<std::size_t> number_;
};

/// Нумерации, получаемые одним обходом в глубину от входа.
struct DepthFirstNumberings {
    /// M-нумерация: порядок включения вершин в путь (прямой порядок обхода).
    Numbering m;
    /// N-нумерация: порядок, обратный порядку удаления вершин из пути
    /// (обратный порядок завершения обхода).
    Numbering n;
};


[[nodiscard]] DepthFirstNumberings computeDepthFirstNumberings(const ProgramGraph& program);


[[nodiscard]] Numbering computeLNumbering(const ProgramGraph& program, const Numbering& nNumbering);

}  // namespace hammock
