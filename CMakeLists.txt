cmake_minimum_required(VERSION 3.20)

project(HammockLayout
    VERSION 1.0.0
    DESCRIPTION "Выделение гамаков ориентированного графа и экспорт иерархии в GraphML"
    LANGUAGES CXX)

option(HAMMOCK_BUILD_TESTS "Собирать модульные тесты" ${PROJECT_IS_TOP_LEVEL})
option(HAMMOCK_WARNINGS_AS_ERRORS "Считать предупреждения компилятора ошибками" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Тип сборки" FORCE)
endif()

include(cmake/Dependencies.cmake)
include(cmake/CompilerWarnings.cmake)

# ---------------------------------------------------------------------------
# Библиотека
# ---------------------------------------------------------------------------
add_library(hammock
    src/digraph.cpp
    src/program_graph.cpp
    src/numbering.cpp
    src/hammocks.cpp
    src/linear_components.cpp
    src/hierarchy.cpp
    src/graphml.cpp)
add_library(hammock::hammock ALIAS hammock)

target_include_directories(hammock
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_compile_features(hammock PUBLIC cxx_std_20)
# pugixml скрыт за pimpl и не виден в публичных заголовках.
target_link_libraries(hammock PRIVATE pugixml::pugixml)
hammock_set_warnings(hammock)

# ---------------------------------------------------------------------------
# Тесты
# ---------------------------------------------------------------------------
if(HAMMOCK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
