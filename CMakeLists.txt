cmake_minimum_required(VERSION 3.16)
project(mcl_link LANGUAGES C)

set(MCL_LINK_VERSION 1.0.0)

option(MCL_LINK_BUILD_TESTS "Build host-side reference tests" ON)

add_library(mcl_link STATIC
    src/link.c
    src/contact.c
    src/rendezvous.c
    src/handoff.c
    src/control.c
    src/negotiation.c)
# See the install section: a plain source path cannot be exported.
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

    add_executable(mcl_link_test_contact tests/test_contact.c)
    target_link_libraries(mcl_link_test_contact PRIVATE mcl_link)
    set_target_properties(mcl_link_test_contact PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_link_contact COMMAND mcl_link_test_contact)

    # What an attacker achieves. SECURITY.md lists both the weaknesses v1.0
    # ships with and the things that would be defects; this exercises the
    # second list so it is not left as prose nobody has run.
    add_executable(mcl_link_test_adversarial tests/test_adversarial.c)
    target_link_libraries(mcl_link_test_adversarial PRIVATE mcl_link)
    set_target_properties(mcl_link_test_adversarial PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_link_adversarial COMMAND mcl_link_test_adversarial)

    add_executable(mcl_link_test_rendezvous tests/test_rendezvous.c)
    target_link_libraries(mcl_link_test_rendezvous PRIVATE mcl_link)
    set_target_properties(mcl_link_test_rendezvous PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_link_rendezvous COMMAND mcl_link_test_rendezvous)

    add_executable(mcl_link_test_control tests/test_control.c)
    target_link_libraries(mcl_link_test_control PRIVATE mcl_link)
    add_test(NAME mcl_link_control COMMAND mcl_link_test_control)

    add_executable(mcl_link_test_negotiation tests/test_negotiation.c)
    target_link_libraries(mcl_link_test_negotiation PRIVATE mcl_link)
    set_target_properties(mcl_link_test_negotiation PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_link_negotiation COMMAND mcl_link_test_negotiation)

    add_executable(mcl_link_test_handoff tests/test_handoff.c)
    target_link_libraries(mcl_link_test_handoff PRIVATE mcl_link)
    set_target_properties(mcl_link_test_handoff PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_link_handoff COMMAND mcl_link_test_handoff)

    add_executable(mcl_link_test_frame tests/test_link_frame.c)
    target_link_libraries(mcl_link_test_frame PRIVATE mcl_link)
    set_target_properties(mcl_link_test_frame PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_link_frame COMMAND mcl_link_test_frame)

    # Pins the PUBLISHED vectors against the decoder. test_handoff.c mirrors
    # them as C arrays, which proves nothing about the JSON an independent
    # implementer would download; the two could drift invisibly.
    add_executable(mcl_link_validate_handoff_vectors
        tools/validate_handoff_vectors.c)
    target_link_libraries(mcl_link_validate_handoff_vectors PRIVATE mcl_link)
    set_target_properties(mcl_link_validate_handoff_vectors PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_link_handoff_vectors
        COMMAND mcl_link_validate_handoff_vectors
        "${CMAKE_CURRENT_SOURCE_DIR}/conformance/vectors/handoff-v0.1.json")

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

# --- Installation and package export ---
#
# See the note in mcl-wire/CMakeLists.txt: an external consumer must not have to
# know these repositories are siblings.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

target_include_directories(mcl_link PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

install(TARGETS mcl_link
    EXPORT mcl_linkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/mcl DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT mcl_linkTargets
    FILE mcl_linkTargets.cmake
    NAMESPACE mcl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mcl_link)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/mcl_linkConfigVersion.cmake"
    VERSION ${MCL_LINK_VERSION}
    COMPATIBILITY SameMajorVersion)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mcl_linkConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/mcl_linkConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mcl_link)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/mcl_linkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/mcl_linkConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mcl_link)
