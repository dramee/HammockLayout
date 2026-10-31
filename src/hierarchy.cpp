#include "hammock/hierarchy.hpp"

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
        if (kinds[i] != HammockKind::Decomposable) {
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
    const std::vector<HammockKind> allKinds = classifyHammocks(hammocks);
    std::vector<Hammock> family;
    std::vector<HammockKind> kinds;
    for (std::size_t i = 0; i < hammocks.size(); ++i) {
        if (allKinds[i] != HammockKind::Decomposable) {
            family.push_back(hammocks[i]);
            kinds.push_back(allKinds[i]);
        }
    }
    return {hammocks.lNumbering(), family, kinds};
}

}  // namespace hammock
