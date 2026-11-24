#include "hammock/hierarchy.hpp"

#include "hammock/linear_components.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace hammock {
namespace {

/// Разреженная таблица: значение идемпотентной операции (min, max) на отрезке за O(1).
template <typename Op>
class SparseTable {
public:
    SparseTable(std::vector<std::size_t> values, Op op) : op_(op) {
        const std::size_t n = values.size();
        levels_.push_back(std::move(values));
        for (std::size_t width = 2; width <= n; width *= 2) {
            const auto& previous = levels_.back();
            std::vector<std::size_t> level(n - width + 1);
            for (std::size_t i = 0; i < level.size(); ++i) {
                level[i] = op_(previous[i], previous[i + width / 2]);
            }
            levels_.push_back(std::move(level));
        }
    }

    /// Значение операции на полуинтервале [lo, hi), lo < hi.
    [[nodiscard]] std::size_t query(std::size_t lo, std::size_t hi) const {
        const auto k = static_cast<std::size_t>(std::bit_width(hi - lo) - 1);
        return op_(levels_[k][lo], levels_[k][hi - (std::size_t{1} << k)]);
    }

private:
    Op op_;
    std::vector<std::vector<std::size_t>> levels_;
};

/// Индексы отрезков @p candidates, которые частично пересекаются хотя бы с одним другим из них.
/// Отрезок [a, c) пересекает [b, d) частично, если a < b < c < d или b < a < d < c.
std::vector<bool> crossingIntervals(const std::vector<Hammock>& candidates, std::size_t n) {
    if (candidates.size() < 2) {
        return std::vector<bool>(candidates.size(), false);
    }
    constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> maxLastFrom(n + 1, 0);     // наибольший конец среди начинающихся в позиции
    std::vector<std::size_t> minFirstTo(n + 1, kNone);  // наименьшее начало среди заканчивающихся в позиции
    for (const Hammock& h : candidates) {
        maxLastFrom[h.first] = std::max(maxLastFrom[h.first], h.last());
        minFirstTo[h.last()] = std::min(minFirstTo[h.last()], h.first);
    }
    const SparseTable maxTable(std::move(maxLastFrom), [](std::size_t x, std::size_t y) { return std::max(x, y); });
    const SparseTable minTable(std::move(minFirstTo), [](std::size_t x, std::size_t y) { return std::min(x, y); });

    std::vector<bool> crossing(candidates.size(), false);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const std::size_t a = candidates[i].first;
        const std::size_t c = candidates[i].last();
        crossing[i] = c - a >= 2 && (maxTable.query(a + 1, c) > c || minTable.query(a + 1, c) < a);
    }
    return crossing;
}

// Полный набор гамаков CFG либо все его гамаки внутри одной линейной компоненты.
// findHammocks даёт интервалы в порядке (first, last), фильтрация сохраняет порядок.
std::vector<HammockKind> classifyProgramHammocks(const ProgramGraph& program, const HammockSet& hammocks) {
    const Digraph& graph = program.graph();
    const Numbering& l = hammocks.lNumbering();
    const std::size_t n = l.size();
    std::vector<std::vector<std::size_t>> successors(n);
    std::vector<std::vector<std::size_t>> predecessors(n);
    std::vector<std::size_t> leftmostPred(n);
    std::iota(leftmostPred.begin(), leftmostPred.end(), std::size_t{0});
    auto rightmostPred = leftmostPred;
    std::vector<std::size_t> lastSource(n, n);
    for (std::size_t source = 0; source < n; ++source) {
        for (const VertexId v : graph.successors(l.vertexAt(source))) {
            const std::size_t target = l[v];
            // Кратные дуги учитываем один раз. Предшественники идут по возрастанию L.
            if (lastSource[target] != source) {
                lastSource[target] = source;
                successors[source].push_back(target);
                predecessors[target].push_back(source);
                leftmostPred[target] = std::min(leftmostPred[target], source);
                rightmostPred[target] = std::max(rightmostPred[target], source);
            }
        }
    }

    std::vector<std::vector<std::size_t>> byFirst(n);
    std::vector<std::vector<std::size_t>> byLast(n + 1);
    for (std::size_t i = 0; i < hammocks.size(); ++i) {
        byFirst[hammocks[i].first].push_back(i);
        byLast[hammocks[i].last()].push_back(i);
    }

    std::vector<HammockKind> kinds(hammocks.size(), HammockKind::Simple);
    std::vector<bool> canExtendLeft(hammocks.size(), false);
    std::vector<std::size_t> marked(n + 1, n);
    for (std::size_t a = 0; a < n; ++a) {
        // При склейке [e,a) и [a,c) все предшественники стыка a
        // должны попасть в [e,c). Сначала отбираем допустимые левые части.
        std::size_t prefixCount = 0;
        for (const std::size_t i : byLast[a]) {
            const Hammock& prefix = hammocks[i];
            if (prefix.first <= leftmostPred[a] && prefix.end == l.vertexAt(a)) {
                marked[prefix.first] = a;
                ++prefixCount;
            }
        }

        std::size_t lastCut = a;
        std::size_t lastBackEdge = a;
        auto pred = predecessors[a].begin();
        for (const std::size_t i : byFirst[a]) {
            const Hammock& h = hammocks[i];
            const std::size_t c = h.last();
            while (pred != predecessors[a].end() && *pred < c) {
                lastBackEdge = std::max(lastBackEdge, *pred);
                ++pred;
            }
            // Суффикс [b,c) может возвращаться в префикс только дугой в a.
            // Если она блокирует самый правый стык b, то блокирует и более ранние.
            if (lastCut > lastBackEdge) {
                kinds[i] = HammockKind::Decomposable;
                if (c > rightmostPred[a] && prefixCount != 0) {
                    std::size_t allowed = prefixCount;
                    if (h.hasEnd()) {
                        const std::size_t q = l[h.end];
                        // Конец объединения не может совпадать с его началом
                        // или иметь дугу в него.
                        allowed -= marked[q] == a;
                        for (const std::size_t v : successors[q]) {
                            if (v != q) {
                                allowed -= marked[v] == a;
                            }
                        }
                    }
                    canExtendLeft[i] = allowed != 0;
                }
            }
            if (c < n && h.end == l.vertexAt(c)) {
                lastCut = c;
            }
        }
    }

    std::ranges::fill(marked, n);
    std::vector<Hammock> maximal;
    std::vector<std::size_t> maximalIndices;
    for (std::size_t b = 0; b <= n; ++b) {
        std::size_t suffixCount = 0;
        if (b < n) {
            // У гамаков с одним началом конечная вершина однозначно задаёт интервал.
            // Позиция n служит меткой гамака без конечной вершины.
            for (const std::size_t i : byFirst[b]) {
                const Hammock& suffix = hammocks[i];
                if (suffix.last() > rightmostPred[b]) {
                    marked[suffix.hasEnd() ? l[suffix.end] : n] = b;
                    ++suffixCount;
                }
            }
        }
        for (const std::size_t i : byLast[b]) {
            if (kinds[i] != HammockKind::Decomposable || canExtendLeft[i]) {
                continue;
            }
            const Hammock& h = hammocks[i];
            const std::size_t a = h.first;
            if (suffixCount != 0 && h.end == l.vertexAt(b) && a <= leftmostPred[b]) {
                std::size_t allowed = suffixCount - (marked[a] == b);
                for (const std::size_t v : predecessors[a]) {
                    if (v != a) {
                        allowed -= marked[v] == b;
                    }
                }
                if (allowed != 0) {
                    continue;
                }
            }
            maximal.push_back(h);
            maximalIndices.push_back(i);
        }
    }
    const auto crossing = crossingIntervals(maximal, n);
    for (std::size_t i = 0; i < maximal.size(); ++i) {
        if (!crossing[i]) {
            kinds[maximalIndices[i]] = HammockKind::Compound;
        }
    }
    return kinds;
}

HammockHierarchy makePresentation(const HammockSet& hammocks, std::span<const HammockKind> allKinds) {
    std::vector<Hammock> family;
    std::vector<HammockKind> kinds;
    for (std::size_t i = 0; i < hammocks.size(); ++i) {
        if (allKinds[i] != HammockKind::Decomposable && !hammocks[i].isTrivial()) {
            family.push_back(hammocks[i]);
            kinds.push_back(allKinds[i]);
        }
    }
    return {hammocks.lNumbering(), family, kinds};
}

}  // namespace

std::string_view toString(HammockKind kind) noexcept {
    switch (kind) {
        case HammockKind::Simple:
            return "simple";
        case HammockKind::Compound:
            return "compound";
        case HammockKind::Decomposable:
            return "decomposable";
    }
    return "unknown";
}

std::vector<HammockKind> classifyHammocks(const HammockSet& hammocks) {
    const std::size_t n = hammocks.lNumbering().size();

    // endsFrom[a] — правые концы отрезков-гамаков, начинающихся в a;
    // startsTo[c] — левые концы отрезков-гамаков, заканчивающихся в c (по возрастанию).
    std::vector<std::vector<std::size_t>> endsFrom(n + 1);
    std::vector<std::vector<std::size_t>> startsTo(n + 1);
    for (const Hammock& h : hammocks) {
        endsFrom[h.first].push_back(h.last());
        startsTo[h.last()].push_back(h.first);
    }
    for (auto& list : endsFrom) {
        std::ranges::sort(list);
    }
    for (auto& list : startsTo) {
        std::ranges::sort(list);
    }
    const auto isHammockInterval = [&](std::size_t a, std::size_t c) {
        return std::ranges::binary_search(endsFrom[a], c);
    };

    // [a, c) = [a, b) ∪ [b, c) для некоторого b.
    const auto decomposable = [&](const Hammock& h) {
        for (const std::size_t b : endsFrom[h.first]) {
            if (b >= h.last()) {
                break;
            }
            if (isHammockInterval(b, h.last())) {
                return true;
            }
        }
        return false;
    };
    std::vector<HammockKind> kinds;
    std::vector<std::size_t> decomposableIndices;
    kinds.reserve(hammocks.size());
    for (std::size_t i = 0; i < hammocks.size(); ++i) {
        if (decomposable(hammocks[i])) {
            kinds.push_back(HammockKind::Decomposable);
            decomposableIndices.push_back(i);
        } else {
            kinds.push_back(HammockKind::Simple);
        }
    }

    // Максимальный разложимый гамак нельзя продолжить соседним гамаком:
    // ни [a, c) ∪ [c, d), ни [e, a) ∪ [a, c) не является гамаком.
    const auto extendable = [&](const Hammock& h) {
        return std::ranges::any_of(endsFrom[h.last()], [&](std::size_t d) { return isHammockInterval(h.first, d); }) ||
               std::ranges::any_of(startsTo[h.first], [&](std::size_t e) { return isHammockInterval(e, h.last()); });
    };
    std::vector<std::size_t> maximalIndices;
    std::vector<Hammock> maximal;
    for (const std::size_t i : decomposableIndices) {
        if (!extendable(hammocks[i])) {
            maximalIndices.push_back(i);
            maximal.push_back(hammocks[i]);
        }
    }

    // Частично пересекающиеся максимальные разложимые гамаки (тело цикла a; L; b)
    // составными не считаются: иначе гамачное представление не было бы иерархией.
    const std::vector<bool> crossing = crossingIntervals(maximal, n);
    for (std::size_t k = 0; k < maximalIndices.size(); ++k) {
        if (!crossing[k]) {
            kinds[maximalIndices[k]] = HammockKind::Compound;
        }
    }
    return kinds;
}

std::vector<Hammock> selectPresentationHammocks(const HammockSet& hammocks) {
    const std::vector<HammockKind> kinds = classifyHammocks(hammocks);
    std::vector<Hammock> result;
    for (std::size_t i = 0; i < hammocks.size(); ++i) {
        if (kinds[i] != HammockKind::Decomposable && !hammocks[i].isTrivial()) {
            result.push_back(hammocks[i]);
        }
    }
    return result;
}

HammockHierarchy::HammockHierarchy(const Numbering& lNumbering, const std::vector<Hammock>& family,
                                   const std::span<const HammockKind> kinds) {
    const std::size_t n = lNumbering.size();
    if (!kinds.empty() && kinds.size() != family.size()) {
        throw std::invalid_argument("HammockHierarchy: число видов не совпадает с числом гамаков");
    }
    std::vector<std::size_t> order(family.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::sort(order, [&](std::size_t i, std::size_t j) {
        const Hammock& x = family[i];
        const Hammock& y = family[j];
        return x.first != y.first ? x.first < y.first : x.size > y.size;
    });

    clusters_.reserve(family.size());
    std::vector<std::size_t> open;  // цепочка вложенных кластеров, содержащих текущую позицию
    for (const std::size_t index : order) {
        const Hammock& h = family[index];
        if (h.size == 0 || h.last() > n) {
            throw std::invalid_argument("HammockHierarchy: гамак выходит за пределы L-нумерации");
        }
        while (!open.empty() && clusters_[open.back()].hammock.last() <= h.first) {
            open.pop_back();
        }
        Cluster cluster;
        cluster.hammock = h;
        cluster.kind = kinds.empty() ? HammockKind::Simple : kinds[index];
        if (!open.empty()) {
            const Hammock& outer = clusters_[open.back()].hammock;
            if (outer.first == h.first && outer.size == h.size) {
                throw std::invalid_argument("HammockHierarchy: гамак встречается дважды");
            }
            if (outer.last() < h.last()) {
                throw std::invalid_argument("HammockHierarchy: семейство гамаков не ламинарно");
            }
            cluster.parent = open.back();
            cluster.depth = clusters_[open.back()].depth + 1;
            clusters_[open.back()].children.push_back(clusters_.size());
        } else {
            topClusters_.push_back(clusters_.size());
        }
        open.push_back(clusters_.size());
        clusters_.push_back(std::move(cluster));
    }

    // Распределение вершин по самым внутренним кластерам (кластеры уже упорядочены по началу).
    clusterOf_.assign(n, kRoot);
    open.clear();
    std::size_t next = 0;
    for (std::size_t position = 0; position < n; ++position) {
        while (!open.empty() && clusters_[open.back()].hammock.last() <= position) {
            open.pop_back();
        }
        while (next < clusters_.size() && clusters_[next].hammock.first == position) {
            const Member member{Member::Kind::Cluster, next};
            (open.empty() ? topMembers_ : clusters_[open.back()].members).push_back(member);
            open.push_back(next++);
        }
        const VertexId v = lNumbering.vertexAt(position);
        const Member member{Member::Kind::Vertex, v};
        if (open.empty()) {
            topVertices_.push_back(v);
            topMembers_.push_back(member);
        } else {
            clusterOf_[v] = open.back();
            clusters_[open.back()].ownVertices.push_back(v);
            clusters_[open.back()].members.push_back(member);
        }
    }
}

std::size_t HammockHierarchy::lowestCommonAncestor(std::size_t a, std::size_t b) const {
    const auto depthOf = [this](const std::size_t c) { return c == kRoot ? 0 : clusters_.at(c).depth + 1; };
    while (depthOf(a) > depthOf(b)) {
        a = clusters_[a].parent;
    }
    while (depthOf(b) > depthOf(a)) {
        b = clusters_[b].parent;
    }
    while (a != b) {
        a = clusters_[a].parent;
        b = clusters_[b].parent;
    }
    return a;
}

HammockHierarchy buildHammockPresentation(const HammockSet& hammocks) {
    return makePresentation(hammocks, classifyHammocks(hammocks));
}

HammockHierarchy buildHammockPresentation(const ProgramGraph& program) {
    const auto components = findLinearComponents(program);
    if (components.size() == 1 && components.front().vertices.size() > 1) {
        // Вся программа до выходной вершины уже образует одну компоненту.
        const HammockSet found = findHammocks(program);
        return makePresentation(found, classifyProgramHammocks(program, found));
    }

    const Digraph& graph = program.graph();
    const std::size_t n = program.vertexCount();
    std::vector<VertexId> order;
    order.reserve(n);
    // Граф разбивается на гамак без выходной вершины CFG и одноэлементный гамак из неё.
    std::vector<Hammock> family{{program.entry(), kNoVertex, 0, n}};
    std::vector<HammockKind> kinds{HammockKind::Compound};
    std::vector<VertexId> localId(n, kNoVertex);
    for (const LinearComponent& component : components) {
        const std::vector<VertexId>& vertices = component.vertices;
        const std::size_t size = vertices.size();
        if (size == 1) {
            if (graph.hasEdge(component.start, component.start)) {
                family.push_back({component.start, component.end, order.size(), 1, true});
                kinds.push_back(HammockKind::Simple);
            }
            order.push_back(component.start);
            continue;
        }

        for (VertexId v = 0; v < size; ++v) {
            localId[vertices[v]] = v;
        }
        // Начало компоненты может иметь внутренних предшественников.
        // Вспомогательный вход сохраняет эти дуги и удовлетворяет контракту ProgramGraph.
        const VertexId localEntry = size;
        const VertexId localExit = size + 1;
        Digraph localGraph(size + 2);
        localGraph.addEdge(localEntry, localId[component.start]);
        for (VertexId v = 0; v < size; ++v) {
            for (const VertexId successor : graph.successors(vertices[v])) {
                localGraph.addEdge(v, successor == component.end ? localExit : localId[successor]);
            }
        }
        const ProgramGraph localProgram(std::move(localGraph), localEntry, localExit);
        const HammockSet found = findHammocks(localProgram);
        const Numbering& localOrder = found.lNumbering();
        std::vector<std::size_t> position(size);
        for (const VertexId v : localOrder.order()) {
            if (v < size) {
                position[v] = order.size();
                order.push_back(vertices[v]);
            }
        }

        // Продолжения за пределы компоненты пропускаем при отборе её гамаков.
        std::vector<Hammock> inside;
        for (const Hammock& h : found) {
            if (!found.contains(h, localEntry) && !found.contains(h, localExit)) {
                inside.push_back(h);
            }
        }
        const HammockSet localHammocks(localOrder, std::move(inside));
        const auto localKinds = classifyProgramHammocks(localProgram, localHammocks);
        for (std::size_t i = 0; i < localHammocks.size(); ++i) {
            const Hammock& h = localHammocks[i];
            if (localKinds[i] != HammockKind::Decomposable && !h.isTrivial()) {
                const VertexId end = h.end == localExit ? component.end : vertices.at(h.end);
                family.push_back({vertices[h.start], end, position[h.start], h.size, h.singletonHasLoop});
                kinds.push_back(localKinds[i]);
            }
        }
        for (const VertexId v : vertices) {
            localId[v] = kNoVertex;
        }
    }
    // Выход CFG образует последнюю одноэлементную компоненту без петли.
    order.push_back(program.exit());
    return {Numbering(std::move(order)), family, kinds};
}

}  // namespace hammock
