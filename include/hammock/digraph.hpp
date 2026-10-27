#pragma once


#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace hammock {

/// Номер вершины графа: целое число из диапазона [0, vertexCount()).
using VertexId = std::size_t;

/// Значение «нет вершины».
inline constexpr VertexId kNoVertex = std::numeric_limits<VertexId>::max();

class Digraph {
public:
    Digraph() = default;

    /// Создаёт граф из @p vertexCount изолированных вершин.
    explicit Digraph(std::size_t vertexCount);

    /// Добавляет новую изолированную вершину и возвращает её номер.
    VertexId addVertex();

    /// Добавляет дугу @p from → @p to.
    /// @throws std::out_of_range если одна из вершин не существует.
    void addEdge(VertexId from, VertexId to);

    [[nodiscard]] std::size_t vertexCount() const noexcept { return successors_.size(); }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return edgeCount_; }

    /// Список непосредственных преемников Г(v) в порядке добавления дуг.
    [[nodiscard]] std::span<const VertexId> successors(VertexId v) const;

    /// Список непосредственных предшественников Г⁻¹(v) в порядке добавления дуг.
    [[nodiscard]] std::span<const VertexId> predecessors(VertexId v) const;

    /// Проверяет наличие дуги @p from → @p to за O(min(deg⁺(from), deg⁻(to))).
    [[nodiscard]] bool hasEdge(VertexId from, VertexId to) const;

    [[nodiscard]] bool contains(VertexId v) const noexcept { return v < vertexCount(); }

private:
    void checkVertex(VertexId v) const;

    std::vector<std::vector<VertexId>> successors_;
    std::vector<std::vector<VertexId>> predecessors_;
    std::size_t edgeCount_ = 0;
};

}  // namespace hammock
