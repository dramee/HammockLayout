/// @file main.cpp
/// @brief Консольная утилита hammock-layout: GraphML → гамаки → иерархический GraphML.

#include "cli_options.hpp"

#include "hammock/graphml.hpp"
#include "hammock/hammocks.hpp"
#include "hammock/hierarchy.hpp"
#include "hammock/linear_components.hpp"
#include "hammock/program_graph.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

using namespace hammock;

constexpr std::string_view kVersion = "1.0.0";

enum ExitCode : int { kSuccess = 0, kFailure = 1, kUsage = 2 };

VertexId requireNode(const GraphMLDocument& document, const std::string& id, std::string_view role) {
    if (const auto v = document.findNode(id)) {
        return *v;
    }
    throw ProgramGraphError("узел-" + std::string(role) + " \"" + id + "\" отсутствует в графе");
}

ProgramGraph makeProgramGraphImpl(const GraphMLDocument& document, const cli::Options& options) {
    if (!options.entryId && !options.exitId) {
        return ProgramGraph::withDetectedTerminals(document.graph());
    }
    const Digraph& graph = document.graph();
    const auto unique = [&](bool wantSource) {
        VertexId found = kNoVertex;
        for (VertexId v = 0; v < graph.vertexCount(); ++v) {
            const bool matches = wantSource ? graph.predecessors(v).empty() : graph.successors(v).empty();
            if (matches) {
                if (found != kNoVertex) {
                    throw ProgramGraphError(std::string("невозможно однозначно определить ") +
                                            (wantSource ? "вход; укажите --entry" : "выход; укажите --exit"));
                }
                found = v;
            }
        }
        return found;
    };
    const VertexId entry = options.entryId ? requireNode(document, *options.entryId, "вход") : unique(true);
    const VertexId exit = options.exitId ? requireNode(document, *options.exitId, "выход") : unique(false);
    return ProgramGraph(graph, entry, exit);
}

/// Строит граф программы; в сообщение об ошибке добавляет идентификатор узла GraphML.
ProgramGraph makeProgramGraph(const GraphMLDocument& document, const cli::Options& options) {
    try {
        return makeProgramGraphImpl(document, options);
    } catch (const ProgramGraphError& error) {
        if (error.vertex() == kNoVertex) {
            throw;
        }
        throw ProgramGraphError(std::string(error.what()) + " (узел \"" + document.nodeId(error.vertex()) + "\")",
                                error.vertex());
    }
}

void printVertexSet(std::ostream& out, const GraphMLDocument& document, std::span<const VertexId> vertices) {
    out << '{';
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        out << (i == 0 ? "" : ", ") << document.nodeId(vertices[i]);
    }
    out << '}';
}

void printReport(std::ostream& out, const GraphMLDocument& document, const ProgramGraph& program,
                 const HammockSet& hammocks, const std::vector<LinearComponent>& components) {
    out << "Граф: " << program.vertexCount() << " вершин, " << program.graph().edgeCount() << " дуг; вход "
        << document.nodeId(program.entry()) << ", выход " << document.nodeId(program.exit()) << "\n\n";

    out << "Линейные компоненты (" << components.size() << "):\n";
    for (std::size_t i = 0; i < components.size(); ++i) {
        const LinearComponent& lc = components[i];
        out << "  " << i + 1 << ". " << document.nodeId(lc.start) << " → " << document.nodeId(lc.end) << ": ";
        printVertexSet(out, document, lc.vertices);
        out << '\n';
    }

    const std::vector<HammockKind> kinds = classifyHammocks(hammocks);
    out << "\nГамаки (" << hammocks.size() << "):\n";
    for (std::size_t i = 0; i < hammocks.size(); ++i) {
        const Hammock& h = hammocks[i];
        out << "  " << i + 1 << ". " << document.nodeId(h.start) << " → "
            << (h.hasEnd() ? document.nodeId(h.end) : std::string("∅")) << ", |U| = " << h.size << ", "
            << toString(kinds[i]) << ": ";
        printVertexSet(out, document, hammocks.vertices(h));
        out << '\n';
    }
}

void verifyHammocks(const ProgramGraph& program, const HammockSet& hammocks) {
    for (const Hammock& h : hammocks) {
        if (!isHammock(program, hammocks.vertices(h), h.start, h.end)) {
            throw std::logic_error("самопроверка: найденное множество не удовлетворяет определению гамака");
        }
    }
}

int run(const cli::Options& options) {
    const GraphMLDocument document = GraphMLDocument::loadFile(options.input);
    const ProgramGraph program = makeProgramGraph(document, options);

    const HammockSet hammocks = findHammocks(program);
    const std::vector<LinearComponent> components = findLinearComponents(program);
    if (options.verify) {
        verifyHammocks(program, hammocks);
    }

    if (options.list) {
        printReport(std::cout, document, program, hammocks, components);
    }

    std::size_t clusterCount = 0;
    if (!options.list || options.output) {
        const HammockHierarchy hierarchy = buildHammockPresentation(hammocks);
        clusterCount = hierarchy.clusters().size();

        if (options.output) {
            std::ofstream out(*options.output, std::ios::binary);
            if (!out) {
                throw GraphMLError("не удалось открыть для записи " + options.output->string());
            }
            document.writeHierarchical(out, hierarchy);
        } else {
            document.writeHierarchical(std::cout, hierarchy);
        }
    }

    if (!options.quiet) {
        std::cerr << "Вершин: " << program.vertexCount() << ", дуг: " << program.graph().edgeCount()
                  << ", гамаков: " << hammocks.size() << ", линейных компонент: " << components.size();
        if (!options.list || options.output) {
            std::cerr << ", гамаков в гамачном представлении: " << clusterCount;
        }
        std::cerr << '\n';
    }
    return kSuccess;
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const std::string_view programName = argc > 0 ? argv[0] : "hammock-layout";
    const std::vector<std::string_view> arguments(argv + std::min(argc, 1), argv + argc);

    cli::Options options;
    try {
        options = cli::parseArguments(arguments);
    } catch (const cli::UsageError& error) {
        std::cerr << "Ошибка: " << error.what() << "\n\n" << cli::usage(programName);
        return kUsage;
    }
    if (options.showHelp) {
        std::cout << cli::usage(programName);
        return kSuccess;
    }
    if (options.showVersion) {
        std::cout << "hammock-layout " << kVersion << '\n';
        return kSuccess;
    }

    try {
        return run(options);
    } catch (const GraphMLError& error) {
        std::cerr << "Ошибка GraphML (" << options.input.string() << "): " << error.what() << '\n';
    } catch (const ProgramGraphError& error) {
        std::cerr << "Граф не является графом программы: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
    }
    return kFailure;
}
