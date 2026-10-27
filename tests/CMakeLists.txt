add_executable(hammock_tests
    digraph_test.cpp
    program_graph_test.cpp
    numbering_test.cpp
    hammocks_test.cpp
    linear_components_test.cpp
    hierarchy_test.cpp
    graphml_test.cpp)

target_link_libraries(hammock_tests PRIVATE hammock::hammock pugixml::pugixml GTest::gtest_main)
hammock_set_warnings(hammock_tests)

include(GoogleTest)
gtest_discover_tests(hammock_tests DISCOVERY_MODE PRE_TEST)

# Сквозная проверка консольной утилиты на примерах.
if(HAMMOCK_BUILD_CLI)
    file(GLOB example_files CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/examples/*.graphml)
    foreach(example IN LISTS example_files)
        get_filename_component(name ${example} NAME_WE)
        set(actual ${CMAKE_CURRENT_BINARY_DIR}/${name}.graphml)
        set(expected ${PROJECT_SOURCE_DIR}/examples/output/${name}.graphml)
        add_test(NAME cli.${name} COMMAND hammock-layout ${example} --verify --quiet -o ${actual})
        set_tests_properties(cli.${name} PROPERTIES FIXTURES_SETUP output.${name})

        # Эталонный результат в examples/output должен совпадать с текущим выводом утилиты.
        add_test(NAME golden.${name} COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol ${actual} ${expected})
        set_tests_properties(golden.${name} PROPERTIES FIXTURES_REQUIRED output.${name})
    endforeach()
endif()
