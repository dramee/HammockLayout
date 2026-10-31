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

using VertexSet = std::vector<VertexId>;

/// Эталонная классификация гамаков перебором пар гамаков как множеств вершин:
/// разложимый — объединение двух непересекающихся гамаков; составной — разложимый,
/// который нельзя объединить с непересекающимся гамаком в больший гамак и который
/// не пересекается частично ни с одним другим таким же.
std::map<VertexSet, HammockKind> bruteForceKinds(const ProgramGraph& program) {
    std::vector<VertexSet> sets;
    for (const auto& key : test::bruteForceHammocks(program)) {
        sets.push_back(std::get<0>(key));
    }
    const auto isHammockSet = [&](const VertexSet& u) { return std::ranges::find(sets, u) != sets.end(); };

    std::set<VertexSet> decomposable;
    std::set<VertexSet> extendable;
    for (std::size_t i = 0; i < sets.size(); ++i) {
        for (std::size_t j = i + 1; j < sets.size(); ++j) {
            VertexSet u;
            std::ranges::set_union(sets[i], sets[j], std::back_inserter(u));
            if (u.size() == sets[i].size() + sets[j].size() && isHammockSet(u)) {
                decomposable.insert(u);
                extendable.insert(sets[i]);
                extendable.insert(sets[j]);
            }
        }
    }
    std::vector<VertexSet> maximal;
    for (const auto& u : decomposable) {
        if (!extendable.contains(u)) {
            maximal.push_back(u);
        }
    }
    const auto partiallyOverlap = [](const VertexSet& x, const VertexSet& y) {
        VertexSet common;
        std::ranges::set_intersection(x, y, std::back_inserter(common));
        return !common.empty() && common.size() < x.size() && common.size() < y.size();
    };

    std::map<VertexSet, HammockKind> kinds;
    for (const auto& u : sets) {
        kinds[u] = decomposable.contains(u) ? HammockKind::Decomposable : HammockKind::Simple;
    }
    for (const auto& u : maximal) {
        if (std::ranges::none_of(maximal, [&](const VertexSet& w) { return partiallyOverlap(u, w); })) {
            kinds[u] = HammockKind::Compound;
        }
    }
    return kinds;
}

std::vector<Hammock> presentation(const ProgramGraph& program) {
    return selectPresentationHammocks(findHammocks(program));
}

// Классификация совпадает с определениями, проверенными перебором.
TEST(PresentationHammocks, MatchesDefinitionsByBruteForce) {
    std::mt19937 rng(31);
    for (std::size_t iteration = 0; iteration < 1500; ++iteration) {
        const auto program = iteration % 2 == 0 ? test::randomProgramGraph(rng, 2 + iteration % 8, iteration % 12)
                                                : test::randomStructuredProgram(rng, 9);
        const auto expected = bruteForceKinds(program);

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
        const auto program = iteration % 2 == 0 ? test::randomProgramGraph(rng, 2 + iteration % 60, iteration % 45)
                                                : test::randomStructuredProgram(rng, 80);
        ASSERT_TRUE(isLaminar(presentation(program))) << "итерация " << iteration;
    }
}

// Пример 8.6 книги (рис. 8.8): 11 простых гамаков [1:4], {2,4}, {3}, [5:12], [13:15],
// [7:9], {6,12}, {8}, {10}, {11}, {16} и два составных — [1:16] и [7:11].
TEST(PresentationHammocks, BookExample) {
    Digraph graph(16);
    for (const auto& [from, to] : std::vector<std::pair<VertexId, VertexId>>{
             {1, 2},  {1, 3},  {2, 2},   {2, 4},   {4, 2},   {4, 5},   {3, 5},   {5, 6},   {5, 7},
             {7, 8},  {7, 9},  {9, 7},   {8, 10},  {9, 10},  {10, 10}, {10, 11}, {11, 11}, {11, 13},
             {6, 12}, {12, 6}, {12, 13}, {13, 14}, {13, 15}, {14, 13}, {15, 14}, {14, 16}, {15, 16}}) {
        graph.addEdge(from - 1, to - 1);  // в книге вершины нумеруются с 1
    }
    const ProgramGraph program(std::move(graph), 0, 15);
    const auto range = [](VertexId a, VertexId b) {
        VertexSet set;
        for (VertexId v = a; v <= b; ++v) {
            set.push_back(v - 1);
        }
        return set;
    };
    const std::set<VertexSet> expectedSimple{range(1, 4), {1, 3}, {2}, range(5, 12), range(13, 15), range(7, 9),
                                             {5, 11},     {7},    {9}, {10},         {15}};
    const std::set<VertexSet> expectedCompound{range(1, 16), range(7, 11)};

    const auto hammocks = findHammocks(program);
    const auto kinds = classifyHammocks(hammocks);
    std::set<VertexSet> simple;
    std::set<VertexSet> compound;
    for (std::size_t i = 0; i < hammocks.size(); ++i) {
        const auto vertices = hammocks.vertices(hammocks[i]);
        VertexSet key(vertices.begin(), vertices.end());
        std::ranges::sort(key);
        if (kinds[i] == HammockKind::Simple) {
            simple.insert(key);
        } else if (kinds[i] == HammockKind::Compound) {
            compound.insert(key);
        }
    }
    EXPECT_EQ(simple, expectedSimple);
    EXPECT_EQ(compound, expectedCompound);
}

// Тело цикла из трёх частей: {a, m} и {m, b} — максимальные разложимые гамаки,
// которые частично пересекаются (всё тело не гамак из-за дуги h → a). Составными
// они не считаются, и a, m, b остаются простыми гамаками внутри цикла.
TEST(PresentationHammocks, LoopBodyOfThreeParts) {
    // 0 вход, 1 h, 2 a, 3 m, 4 b, 5 выход.
    const ProgramGraph program(test::makeGraph(6, {{0, 1}, {1, 2}, {1, 5}, {2, 3}, {3, 4}, {4, 1}}), 0, 5);
    const auto expected = bruteForceKinds(program);
    EXPECT_EQ(expected.at(VertexSet{2, 3}), HammockKind::Decomposable);
    EXPECT_EQ(expected.at(VertexSet{3, 4}), HammockKind::Decomposable);

    const auto hierarchy = buildHammockPresentation(findHammocks(program));
    const auto loop = hierarchy.clusterOf(1);  // гамак цикла {h, a, m, b}
    ASSERT_NE(loop, HammockHierarchy::kRoot);
    EXPECT_EQ(hierarchy.cluster(loop).hammock.size, 4U);
    EXPECT_EQ(hierarchy.cluster(loop).kind, HammockKind::Simple);
    EXPECT_EQ(hierarchy.cluster(loop).children.size(), 3U);  // {a}, {m}, {b}
    for (const VertexId v : {2U, 3U, 4U}) {
        EXPECT_EQ(hierarchy.cluster(hierarchy.clusterOf(v)).parent, loop);
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
