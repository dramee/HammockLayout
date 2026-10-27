#include "hammock/hierarchy.hpp"

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

namespace hammock {
namespace {

bool crosses(const Hammock& a, const Hammock& b) {
    const bool intersect = a.first < b.last() && b.first < a.last();
    const bool nested = (a.first <= b.first && b.last() <= a.last()) || (b.first <= a.first && a.last() <= b.last());
    return intersect && !nested;
}

bool isLaminar(const std::vector<Hammock>& family) {
    for (std::size_t i = 0; i < family.size(); ++i) {
        for (std::size_t j = i + 1; j < family.size(); ++j) {
            if (crosses(family[i], family[j])) {
                return false;
            }
        }
    }
    return true;
}

// Гамачное представление: классификация совпадает с определениями глоссария,
// проверенными перебором пар гамаков как множеств вершин.
TEST(PresentationHammocks, MatchesGlossaryDefinitions) {
    std::mt19937 rng(31);
    for (std::size_t iteration = 0; iteration < 1500; ++iteration) {
        const auto program = test::randomProgramGraph(rng, 2 + iteration % 8, iteration % 12);
        std::vector<std::vector<VertexId>> sets;
        for (const auto& key : test::bruteForceHammocks(program)) {
            sets.push_back(std::get<0>(key));
        }
        const auto disjointUnion = [](const std::vector<VertexId>& a, const std::vector<VertexId>& b) {
            std::vector<VertexId> result;
            std::ranges::set_union(a, b, std::back_inserter(result));
            return result.size() == a.size() + b.size() ? result : std::vector<VertexId>{};
        };
        std::set<std::vector<VertexId>> decomposable;
        for (std::size_t i = 0; i < sets.size(); ++i) {
            for (std::size_t j = i + 1; j < sets.size(); ++j) {
                const auto u = disjointUnion(sets[i], sets[j]);
                if (!u.empty() && std::ranges::find(sets, u) != sets.end()) {
                    decomposable.insert(u);
                }
            }
        }
        std::map<std::vector<VertexId>, HammockKind> expected;
        for (const auto& u : sets) {
            if (!decomposable.contains(u)) {
                expected[u] = HammockKind::Simple;
                continue;
            }
            const bool maximal = std::ranges::none_of(decomposable, [&](const std::vector<VertexId>& w) {
                return w.size() > u.size() && std::ranges::includes(w, u);
            });
            expected[u] = maximal ? HammockKind::Compound : HammockKind::Decomposable;
        }

        const auto hammocks = findHammocks(program);
        const auto kinds = classifyHammocks(hammocks);
        ASSERT_EQ(kinds.size(), expected.size());
        for (std::size_t i = 0; i < hammocks.size(); ++i) {
            const auto vertices = hammocks.vertices(hammocks[i]);
            std::vector<VertexId> key(vertices.begin(), vertices.end());
            std::ranges::sort(key);
            ASSERT_EQ(kinds[i], expected.at(key)) << "итерация " << iteration;
        }
    }
}

TEST(PresentationHammocks, FormsHierarchyOnRandomGraphs) {
    std::mt19937 rng(37);
    for (std::size_t iteration = 0; iteration < 2000; ++iteration) {
        const auto program = test::randomProgramGraph(rng, 2 + iteration % 60, iteration % 45);
        const auto family = selectPresentationHammocks(findHammocks(program));
        ASSERT_TRUE(isLaminar(family)) << "итерация " << iteration;
    }
}

TEST(PresentationHammocks, WholeGraphIsTheCompoundRoot) {
    // Весь граф — гамак без конечной вершины; для цепочки он составной,
    // а простые гамаки — только одноэлементные.
    const auto hammocks = findHammocks(test::makeChain(5));
    const auto kinds = classifyHammocks(hammocks);
    std::size_t compound = 0;
    for (std::size_t i = 0; i < hammocks.size(); ++i) {
        if (kinds[i] == HammockKind::Compound) {
            ++compound;
            EXPECT_EQ(hammocks[i].size, 5U);
            EXPECT_FALSE(hammocks[i].hasEnd());
        }
        if (kinds[i] == HammockKind::Simple) {
            EXPECT_EQ(hammocks[i].size, 1U);
        }
    }
    EXPECT_EQ(compound, 1U);
    EXPECT_EQ(selectPresentationHammocks(hammocks).size(), 6U);
}

TEST(PresentationHammocks, IncludesHammocksOfAnySize) {
    // Цепочка 0 → 1 → 2 → 3 → 4: составной гамак (весь граф) содержит пять
    // простых одновершинных гамаков — каждая вершина в своём кластере.
    const auto hierarchy = buildHammockPresentation(findHammocks(test::makeChain(5)));
    ASSERT_EQ(hierarchy.clusters().size(), 6U);
    ASSERT_EQ(hierarchy.topLevelClusters().size(), 1U);
    const auto& root = hierarchy.cluster(hierarchy.topLevelClusters()[0]);
    EXPECT_EQ(root.kind, HammockKind::Compound);
    EXPECT_EQ(root.hammock.size, 5U);
    EXPECT_EQ(root.children.size(), 5U);
    EXPECT_TRUE(root.ownVertices.empty());
    EXPECT_TRUE(hierarchy.topLevelVertices().empty());
    for (const std::size_t child : root.children) {
        EXPECT_EQ(hierarchy.cluster(child).kind, HammockKind::Simple);
        EXPECT_EQ(hierarchy.cluster(child).hammock.size, 1U);
        EXPECT_EQ(hierarchy.cluster(child).ownVertices.size(), 1U);
    }
}

TEST(HammockHierarchy, BuildsTreeAndAssignsEveryVertexOnce) {
    std::mt19937 rng(29);
    for (std::size_t iteration = 0; iteration < 200; ++iteration) {
        const auto program = test::randomProgramGraph(rng, 2 + iteration % 40, iteration % 30);
        const auto hammocks = findHammocks(program);
        const auto hierarchy = buildHammockPresentation(hammocks);
        EXPECT_EQ(hierarchy.clusters().size(), selectPresentationHammocks(hammocks).size());

        std::vector<int> seen(program.vertexCount(), 0);
        std::size_t clusterMembers = 0;
        const auto visit = [&](std::size_t level) {
            for (const auto& member : hierarchy.members(level)) {
                if (member.kind == HammockHierarchy::Member::Kind::Vertex) {
                    ++seen[member.index];
                    EXPECT_EQ(hierarchy.clusterOf(member.index), level);
                } else {
                    ++clusterMembers;
                    EXPECT_EQ(hierarchy.cluster(member.index).parent, level);
                }
            }
        };
        visit(HammockHierarchy::kRoot);
        for (std::size_t c = 0; c < hierarchy.clusters().size(); ++c) {
            const auto& cluster = hierarchy.cluster(c);
            EXPECT_NE(cluster.kind, HammockKind::Decomposable);
            // Каждая вершина кластера принадлежит его гамаку.
            for (const VertexId v : cluster.ownVertices) {
                EXPECT_TRUE(hammocks.contains(cluster.hammock, v));
            }
            visit(c);
        }
        EXPECT_TRUE(std::ranges::all_of(seen, [](int count) { return count == 1; }));
        EXPECT_EQ(clusterMembers, hierarchy.clusters().size());
    }
}

TEST(HammockHierarchy, LowestCommonAncestor) {
    const auto hammocks = findHammocks(test::makeChain(5));
    // Вершины в L-порядке: 0 1 2 3 4. Кластеры: A = [0,4), B = [0,2) ⊂ A, C = [2,4) ⊂ A.
    const Hammock a{0, 4, 0, 4};
    const Hammock b{0, 2, 0, 2};
    const Hammock c{2, 4, 2, 2};
    const HammockHierarchy hierarchy(hammocks.lNumbering(), {c, a, b});
    ASSERT_EQ(hierarchy.clusters().size(), 3U);
    const auto indexOf = [&](const Hammock& h) {
        const auto clusters = hierarchy.clusters();
        return static_cast<std::size_t>(
            std::ranges::find_if(clusters, [&](const auto& cluster) { return cluster.hammock == h; }) -
            clusters.begin());
    };
    const std::size_t ia = indexOf(a);
    const std::size_t ib = indexOf(b);
    const std::size_t ic = indexOf(c);
    EXPECT_EQ(hierarchy.clusterOf(0), ib);
    EXPECT_EQ(hierarchy.clusterOf(3), ic);
    EXPECT_EQ(hierarchy.cluster(ib).parent, ia);
    EXPECT_EQ(hierarchy.cluster(ic).parent, ia);
    EXPECT_EQ(hierarchy.lowestCommonAncestor(ib, ic), ia);
    EXPECT_EQ(hierarchy.lowestCommonAncestor(ib, ib), ib);
    EXPECT_EQ(hierarchy.lowestCommonAncestor(ib, ia), ia);
    EXPECT_EQ(hierarchy.lowestCommonAncestor(ic, HammockHierarchy::kRoot), HammockHierarchy::kRoot);
    EXPECT_EQ(hierarchy.clusterOf(4), HammockHierarchy::kRoot);
    EXPECT_EQ(hierarchy.topLevelVertices().size(), 1U);
    EXPECT_EQ(hierarchy.topLevelClusters().size(), 1U);
}

TEST(HammockHierarchy, RejectsNonLaminarFamilies) {
    const auto hammocks = findHammocks(test::makeChain(5));
    const Hammock x{0, 2, 0, 2};
    const Hammock y{1, 3, 1, 2};
    EXPECT_THROW(HammockHierarchy(hammocks.lNumbering(), {x, y}), std::invalid_argument);
    EXPECT_THROW(HammockHierarchy(hammocks.lNumbering(), {x, x}), std::invalid_argument);
    EXPECT_THROW(HammockHierarchy(hammocks.lNumbering(), {Hammock{0, 9, 3, 7}}), std::invalid_argument);
}

}  // namespace
}  // namespace hammock
