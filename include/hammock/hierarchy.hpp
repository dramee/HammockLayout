#pragma once

#include <span>
#include <string_view>
#include <vector>
#include "hammock/hammocks.hpp"

namespace hammock {

/// Вид гамака (В. Н. Касьянов, «Оптимизирующие преобразования программ», 1988, гл. 8, п. 3.1.4).
enum class HammockKind {
    /// Простой (неразложимый) гамак: не является объединением двух непересекающихся гамаков.
    Simple,
    /// Составной гамак: максимальный разложимый гамак.
    Compound,
    /// Разложимый, но не составной гамак (в гамачное представление не входит).
    Decomposable,
};

/// Текстовое имя вида гамака ("simple", "compound", "decomposable").
[[nodiscard]] std::string_view toString(HammockKind kind) noexcept;

/// @brief Классифицирует каждый гамак множества @p hammocks (результат — в том же порядке).
///
/// Гамак H разложим, если H = A ∪ B для непересекающихся гамаков A и B, т. е. его
/// отрезок L-нумерации [a, c) делится точкой b на гамаки [a, b) и [b, c). Иначе
/// говоря, H как граф программы состоит более чем из одной линейной компоненты.
///
/// Разложимый гамак составной, если он максимален — его нельзя продолжить соседним
/// гамаком так, чтобы объединение снова было гамаком (H не является частью цепочки
/// линейных компонент большего гамака), — и не пересекается частично ни с одним
/// другим максимальным разложимым гамаком. Последнее условие нужно для тела цикла
/// из трёх и более частей (`while (c) { a; m; b; }`): {a, m} и {m, b} максимальны и
/// пересекаются, а всё тело гамаком не является; такие части остаются простыми
/// гамаками внутри цикла. На графах без таких циклов определение совпадает с книгой
/// (пример 8.6, свойства 8.12–8.15).
///
/// Сложность O(h log h + n log n) плюс просмотр гамаков с общими концами.
[[nodiscard]] std::vector<HammockKind> classifyHammocks(const HammockSet& hammocks);

/// Возвращает простые и составные гамаки — гамачное представление графа.
[[nodiscard]] std::vector<Hammock> selectPresentationHammocks(const HammockSet& hammocks);

/// @brief Дерево вложенных гамаков (кластеров).
class HammockHierarchy {
public:
    /// Номер «корня» — графа верхнего уровня.
    static constexpr std::size_t kRoot = static_cast<std::size_t>(-1);

    /// Элемент уровня иерархии: вершина или вложенный кластер.
    struct Member {
        enum class Kind { Vertex, Cluster };
        Kind kind;
        std::size_t index;  ///< VertexId для вершины, номер кластера для кластера

        friend bool operator==(const Member&, const Member&) = default;
    };

    struct Cluster {
        Hammock hammock;
        HammockKind kind = HammockKind::Simple;  ///< вид гамака по глоссарию
        std::size_t parent = kRoot;              ///< объемлющий кластер или kRoot
        std::size_t depth = 0;                   ///< глубина вложенности (0 — верхний уровень)
        std::vector<std::size_t> children;       ///< непосредственно вложенные кластеры
        std::vector<VertexId> ownVertices;       ///< вершины, не входящие во вложенные кластеры
        std::vector<Member> members;             ///< вершины и вложенные кластеры в порядке L-нумерации
    };

    /// Строит дерево по ламинарному семейству гамаков за O(c log c + n), c — число кластеров.
    /// @param kinds виды гамаков в порядке @p family (пустой — все считаются простыми).
    /// @throws std::invalid_argument если семейство не ламинарно, содержит повторы
    ///         или размер @p kinds не совпадает с размером @p family.
    HammockHierarchy(const Numbering& lNumbering, const std::vector<Hammock>& family,
                     std::span<const HammockKind> kinds = {});

    [[nodiscard]] std::span<const Cluster> clusters() const noexcept { return clusters_; }
    [[nodiscard]] const Cluster& cluster(std::size_t index) const { return clusters_.at(index); }

    /// Кластеры верхнего уровня.
    [[nodiscard]] std::span<const std::size_t> topLevelClusters() const noexcept { return topClusters_; }

    /// Вершины верхнего уровня (не входящие ни в один кластер).
    [[nodiscard]] std::span<const VertexId> topLevelVertices() const noexcept { return topVertices_; }

    /// Вершины и кластеры верхнего уровня в порядке L-нумерации.
    [[nodiscard]] std::span<const Member> topLevelMembers() const noexcept { return topMembers_; }

    /// Элементы уровня @p cluster (kRoot — верхний уровень) в порядке L-нумерации.
    [[nodiscard]] std::span<const Member> members(std::size_t cluster) const {
        return cluster == kRoot ? std::span<const Member>(topMembers_) : clusters_.at(cluster).members;
    }

    /// Самый внутренний кластер, содержащий вершину @p v, или kRoot.
    [[nodiscard]] std::size_t clusterOf(VertexId v) const { return clusterOf_.at(v); }

    /// Наименьший общий предок двух узлов дерева (кластеров или kRoot).
    [[nodiscard]] std::size_t lowestCommonAncestor(std::size_t a, std::size_t b) const;

private:
    std::vector<Cluster> clusters_;
    std::vector<std::size_t> topClusters_;
    std::vector<VertexId> topVertices_;
    std::vector<Member> topMembers_;
    std::vector<std::size_t> clusterOf_;
};

/// @brief Строит гамачное представление: дерево всех простых и составных гамаков
/// (любого размера, в том числе одновершинных).
[[nodiscard]] HammockHierarchy buildHammockPresentation(const HammockSet& hammocks);

}  // namespace hammock
