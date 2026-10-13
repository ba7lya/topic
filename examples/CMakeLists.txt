# SPDX-License-Identifier: MIT
#
# Examples: runnable programs that show the engine from the outside.
#
# Each example is its own executable behind the `BUILD_EXAMPLE` option, so a reader can build one
# and run it without pulling in the test or benchmark dependencies.

set(_topic_engine_examples basic_matching)

add_executable(topic_engine_example_basic_matching basic_matching.cxx)
target_link_libraries(topic_engine_example_basic_matching PRIVATE ba7lya::topic_engine)

# The router is a feature of the library, so the examples that use it follow the same switch.
if(${PROJECT_NAME}_ROUTER)
    list(APPEND _topic_engine_examples typed_router)

    add_executable(topic_engine_example_typed_router typed_router.cxx)
    target_link_libraries(topic_engine_example_typed_router PRIVATE ba7lya::topic_engine)
endif()

message(STATUS "Examples: ${_topic_engine_examples}")
