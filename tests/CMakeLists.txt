# SPDX-License-Identifier: MIT

# One test executable per test type: the suites are split by what they exercise, so a failure names
# the kind of thing that broke. `gtest_discover_tests` registers each TEST with CTest individually,
# which keeps a failure down to a single case rather than a whole binary.

include(GoogleTest)

find_package(GTest CONFIG REQUIRED)

# Fixtures shared by the suites.
add_library(topic_test_support INTERFACE)
target_include_directories(topic_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(topic_test_support INTERFACE GTest::gtest GTest::gtest_main)

# The trie and the router exist only when the router feature is on, so the suites that exercise them
# are gated the same way the library sources are.
# Each suite is named after the library source it exercises, so a failing case names both the kind of
# test and the unit at fault.
set(_topic_ut_sources
    ut/cache_tests.cxx
    ut/filter_tests.cxx
    ut/qos_tests.cxx
    ut/topic_match_tests.cxx
    ut/topic_path_tests.cxx
)

if(${PROJECT_NAME}_ROUTER)
    list(APPEND _topic_ut_sources ut/matcher_tests.cxx ut/trie_tests.cxx ut/router_tests.cxx)
endif()

# --- unit tests: one unit at a time, no I/O ---
add_executable(topic_ut ${_topic_ut_sources})
target_link_libraries(topic_ut PRIVATE ba7lya::topic topic_test_support)
gtest_discover_tests(topic_ut)

if(${PROJECT_NAME}_ROUTER)
    # --- integration tests: several components interoperating ---
    add_executable(topic_integration integration/router_matching_tests.cxx)
    target_link_libraries(topic_integration PRIVATE ba7lya::topic topic_test_support)
    gtest_discover_tests(topic_integration)
endif()

# --- end-to-end tests: whole flows through the public API ---
# The router-free flow runs in every configuration; the typed-session flow needs the router, so it
# is built only when the feature is on.
set(_topic_e2e_sources e2e/topic_api_tests.cxx)

if(${PROJECT_NAME}_ROUTER)
    list(APPEND _topic_e2e_sources e2e/typed_subscription_tests.cxx)
endif()

add_executable(topic_e2e ${_topic_e2e_sources})
target_link_libraries(topic_e2e PRIVATE ba7lya::topic topic_test_support)
gtest_discover_tests(topic_e2e)
