add_executable(hammock_tests
    digraph_test.cpp
    program_graph_test.cpp
    numbering_test.cpp
    hammocks_test.cpp
    linear_components_test.cpp
    hierarchy_test.cpp
    graphml_test.cpp
    examples_test.cpp)

target_link_libraries(hammock_tests PRIVATE hammock::hammock pugixml::pugixml GTest::gtest_main)
hammock_set_warnings(hammock_tests)
# Каталог примеров: входные графы и эталонные результаты для examples_test.cpp.
target_compile_definitions(hammock_tests PRIVATE HAMMOCK_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")

include(GoogleTest)
gtest_discover_tests(hammock_tests DISCOVERY_MODE PRE_TEST)
