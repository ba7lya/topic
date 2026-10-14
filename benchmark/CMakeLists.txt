# SPDX-License-Identifier: MIT

# google benchmark, one executable per measured area. The `benchmark::Benchmark` main is pulled in
# from the library's own target rather than written by hand.

find_package(benchmark CONFIG REQUIRED)

add_executable(topic_benchmark_pattern pattern_benchmark.cxx)
target_link_libraries(topic_benchmark_pattern PRIVATE ba7lya::topic benchmark::benchmark)

add_executable(topic_benchmark_matching matching_benchmark.cxx)
target_link_libraries(topic_benchmark_matching PRIVATE ba7lya::topic benchmark::benchmark)

# The router and the trie are gated behind the feature, so their benchmark follows the same switch.
if(${PROJECT_NAME}_ROUTER)
    add_executable(topic_benchmark_router router_benchmark.cxx)
    target_link_libraries(topic_benchmark_router PRIVATE ba7lya::topic benchmark::benchmark)
endif()
