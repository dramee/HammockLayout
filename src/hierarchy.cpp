#include "hammock/hierarchy.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace hammock {

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

    // endsFrom[a] — правые концы отрезков-гамаков, начинающихся в a, по возрастанию.
    std::vector<std::vector<std::size_t>> endsFrom(n + 1);
    for (const Hammock& h : hammocks) {
        endsFrom[h.first].push_back(h.last());
    }
    for (auto& list : endsFrom) {
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

    // Составные — разложимые гамаки, не содержащиеся строго в других разложимых.
    // Отрезки упорядочиваются по началу, при равных началах — по убыванию конца;
    // отрезок вложен в один из предыдущих тогда и только тогда, когда максимум
    // концов предыдущих отрезков не меньше его конца.
    std::ranges::sort(decomposableIndices, [&](std::size_t i, std::size_t j) {
        const Hammock& x = hammocks[i];
        const Hammock& y = hammocks[j];
        return x.first != y.first ? x.first < y.first : x.last() > y.last();
    });
    std::size_t maxLast = 0;
    for (const std::size_t i : decomposableIndices) {
        if (hammocks[i].last() > maxLast) {
            kinds[i] = HammockKind::Compound;
            maxLast = hammocks[i].last();
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
