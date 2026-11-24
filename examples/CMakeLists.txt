add_executable(hammock_draw_examples EXCLUDE_FROM_ALL draw_sugiyama.cpp)
target_link_libraries(hammock_draw_examples PRIVATE hammock::hammock pugixml::pugixml)
hammock_set_warnings(hammock_draw_examples)

add_custom_target(sugiyama_figures
    COMMAND hammock_draw_examples "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/figures"
    DEPENDS hammock_draw_examples
    VERBATIM)
