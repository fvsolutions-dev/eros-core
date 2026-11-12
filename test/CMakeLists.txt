cmake_minimum_required(VERSION 3.24)

set(UNITY_DIR "$ENV{UNITY_DIR}" CACHE PATH "Path to Unity test framework")
if(NOT UNITY_DIR)
    set(UNITY_DIR "/opt/Unity" CACHE PATH "" FORCE)
endif()

if(NOT EXISTS "${UNITY_DIR}/src/unity.c")
    message(FATAL_ERROR
        "Unity not found at ${UNITY_DIR}. "
        "Set UNITY_DIR to a Unity checkout, or rebuild the devcontainer to "
        "populate /opt/Unity.")
endif()

function(eros_add_unity_test name source)
    add_executable(${name} ${source} ${UNITY_DIR}/src/unity.c)
    target_include_directories(${name} PRIVATE ${UNITY_DIR}/src)
    target_link_libraries(${name} PRIVATE eros)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

eros_add_unity_test(primitive_test    primitive_test.c)
eros_add_unity_test(pub_sub_test      pub_sub_test.c)
eros_add_unity_test(server_client_test server_client_test.c)
eros_add_unity_test(gateway_test      gateway_test.c)

# eros_stream.c is compiled by the project that uses it, with burst-link-protocol's C sources: here
# fetched at a fixed version. Offline, or with a checkout at hand, point to it instead:
#   -DFETCHCONTENT_SOURCE_DIR_BURST_LINK=<path to burst-link-protocol>
include(FetchContent)
FetchContent_Declare(burst_link
    GIT_REPOSITORY https://github.com/fvsolutions-dev/burst-link-protocol.git
    GIT_TAG        v1.1.6
    GIT_SHALLOW    TRUE
    # Its sources only: no CMakeLists.txt there, so its own build (a Python package) is left alone
    SOURCE_SUBDIR  src)
FetchContent_MakeAvailable(burst_link)

eros_add_unity_test(stream_test stream_test.c)
target_sources(stream_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/eros_c/eros_stream.c
    ${burst_link_SOURCE_DIR}/src/burst/burst_decoder.c
    ${burst_link_SOURCE_DIR}/src/burst/burst_encoder.c
    ${burst_link_SOURCE_DIR}/src/burst/burst_generic.c)
target_include_directories(stream_test PRIVATE ${burst_link_SOURCE_DIR}/src)
