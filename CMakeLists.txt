cmake_minimum_required(VERSION 3.16)
project(mcl_link LANGUAGES C)

option(MCL_LINK_BUILD_TESTS "Build host-side reference tests" ON)

add_library(mcl_link STATIC
    src/link.c)
target_include_directories(mcl_link PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(mcl_link PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO)

if(MCL_LINK_BUILD_TESTS)
    enable_testing()

    add_executable(mcl_link_test_link tests/test_link.c)
    target_link_libraries(mcl_link_test_link PRIVATE mcl_link)
    set_target_properties(mcl_link_test_link PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_link_test COMMAND mcl_link_test_link)

    add_executable(mcl_link_validate_transport tools/validate_transport_registry.c)
    set_target_properties(mcl_link_validate_transport PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_link_transport_registry COMMAND mcl_link_validate_transport
        "${CMAKE_CURRENT_SOURCE_DIR}/registries/transport-ids-v0.1.json")

    # Coexistence test with mcl-wire
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../mcl-wire/include/mcl/wire.h")
        add_executable(mcl_link_test_coexistence
            tests/test_coexistence.c
            "${CMAKE_CURRENT_SOURCE_DIR}/../mcl-wire/src/wire.c")
        target_include_directories(mcl_link_test_coexistence PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../mcl-wire/include")
        target_link_libraries(mcl_link_test_coexistence PRIVATE mcl_link)
        set_target_properties(mcl_link_test_coexistence PROPERTIES
            C_STANDARD 99
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS NO)
        add_test(NAME mcl_link_coexistence COMMAND mcl_link_test_coexistence)
    endif()
endif()
