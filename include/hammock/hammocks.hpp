#pragma once


#include "hammock/digraph.hpp"
#include "hammock/numbering.hpp"
#include "hammock/program_graph.hpp"

#include <span>
#include <vector>

namespace hammock {

/// @brief Гамак графа программы.

struct Hammock {
    VertexId start = kNoVertex;  ///< начальная вершина p ∈ U
    VertexId end = kNoVertex;    ///< конечная вершина q ∉ U или kNoVertex, если её нет
    std::size_t first = 0;       ///< L-номер начальной вершины
    std::size_t size = 0;        ///< число вершин |U|

    /// L-номер, следующий за последней вершиной гамака.
    [[nodiscard]] std::size_t last() const noexcept { return first + size; }

    /// Есть ли у гамака конечная вершина.
    [[nodiscard]] bool hasEnd() const noexcept { return end != kNoVertex; }

    friend bool operator==(const Hammock&, const Hammock&) = default;
};

/// @brief Множество гамаков вместе с L-нумерацией, относительно которой они заданы.
class HammockSet {
public:
    HammockSet(Numbering lNumbering, std::vector<Hammock> hammocks);

    [[nodiscard]] std::span<const Hammock> hammocks() const noexcept { return hammocks_; }
    [[nodiscard]] std::size_t size() const noexcept { return hammocks_.size(); }
    [[nodiscard]] bool empty() const noexcept { return hammocks_.empty(); }
    [[nodiscard]] const Hammock& operator[](std::size_t i) const { return hammocks_[i]; }
    [[nodiscard]] auto begin() const noexcept { return hammocks_.begin(); }
    [[nodiscard]] auto end() const noexcept { return hammocks_.end(); }

    /// L-нумерация вершин графа.
    [[nodiscard]] const Numbering& lNumbering() const noexcept { return lNumbering_; }

    /// Вершины гамака @p hammock в порядке L-нумерации.
    [[nodiscard]] std::span<const VertexId> vertices(const Hammock& hammock) const;

    /// Принадлежит ли вершина @p v гамаку @p hammock (O(1)).
    [[nodiscard]] bool contains(const Hammock& hammock, VertexId v) const;

private:
    Numbering lNumbering_;
    std::vector<Hammock> hammocks_;
};

[[nodiscard]] HammockSet findHammocks(const ProgramGraph& program);


[[nodiscard]] bool isHammock(const ProgramGraph& program, std::span<const VertexId> vertices, VertexId start,
                             VertexId end);

}  // namespace hammock
