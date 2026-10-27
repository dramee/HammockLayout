#pragma once

#include "hammock/digraph.hpp"
#include "hammock/hierarchy.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hammock {

/// Ошибка чтения или разбора GraphML.
class GraphMLError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// @brief Документ GraphML: исходный XML и извлечённый из него ориентированный граф.
class GraphMLDocument {
public:
    /// Загружает документ из файла.
    /// @throws GraphMLError при ошибке чтения, синтаксиса XML или структуры GraphML.
    [[nodiscard]] static GraphMLDocument loadFile(const std::filesystem::path& path);

    /// Загружает документ из строки.
    /// @throws GraphMLError при ошибке синтаксиса XML или структуры GraphML.
    [[nodiscard]] static GraphMLDocument loadString(std::string_view xml);

    GraphMLDocument(GraphMLDocument&&) noexcept;
    GraphMLDocument& operator=(GraphMLDocument&&) noexcept;
    GraphMLDocument(const GraphMLDocument&) = delete;
    GraphMLDocument& operator=(const GraphMLDocument&) = delete;
    ~GraphMLDocument();

    /// Граф: вершина i соответствует i-му узлу документа в порядке появления.
    [[nodiscard]] const Digraph& graph() const noexcept;

    /// Идентификатор GraphML-узла, соответствующего вершине @p v.
    [[nodiscard]] const std::string& nodeId(VertexId v) const;

    /// Вершина, соответствующая узлу с идентификатором @p id.
    [[nodiscard]] std::optional<VertexId> findNode(std::string_view id) const;

    /// Записывает иерархический GraphML: исходные узлы и рёбра, сгруппированные
    /// во вложенные графы по кластерам иерархии @p hierarchy. Атрибут hammock.kind
    /// кластера — вид гамака ("simple" или "compound"), подпись — «Простой гамак K (p → q)»
    /// или «Составной гамак K (p → q)».
    void writeHierarchical(std::ostream& out, const HammockHierarchy& hierarchy) const;

private:
    struct Impl;
    explicit GraphMLDocument(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace hammock
