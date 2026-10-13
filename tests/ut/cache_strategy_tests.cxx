// SPDX-License-Identifier: MIT
//
// `cache_strategy`: the capacity-is-the-state model, and the fallback path when the LRU cache is
// compiled out.

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>

#include "cache_strategy.hxx"

namespace ba7lya::topic_engine {
namespace {

TEST(cache_strategy_tests, new_with_capacity) {
#ifdef BA7LYA_TOPIC_ENGINE_NO_LRU_CACHE
    const cache_strategy strategy = cache_strategy::from_capacity(100);
    EXPECT_FALSE(strategy.is_lru());
    EXPECT_EQ(strategy.capacity(), std::nullopt);
#else
    const cache_strategy strategy = cache_strategy::from_capacity(100);
    EXPECT_TRUE(strategy.is_lru());
    EXPECT_EQ(strategy.capacity(), std::optional<std::size_t> { 100 });
#endif
}

TEST(cache_strategy_tests, new_with_zero_capacity) {
    const cache_strategy strategy = cache_strategy::from_capacity(0);
    EXPECT_FALSE(strategy.is_lru());
    EXPECT_EQ(strategy.capacity(), std::nullopt);
}

TEST(cache_strategy_tests, default_is_no_cache) {
    const cache_strategy strategy = cache_strategy::no_cache();
    EXPECT_FALSE(strategy.is_lru());
    EXPECT_EQ(strategy.capacity(), std::nullopt);
}

TEST(cache_strategy_tests, capacity_of_lru) {
#ifdef BA7LYA_TOPIC_ENGINE_NO_LRU_CACHE
    EXPECT_EQ(cache_strategy::lru(50).capacity(), std::nullopt);
#else
    EXPECT_EQ(cache_strategy::lru(50).capacity(), std::optional<std::size_t> { 50 });
    EXPECT_TRUE(cache_strategy::lru(50).is_lru());
#endif
}

TEST(cache_strategy_tests, lru_with_zero_capacity_is_no_cache) {
    const cache_strategy strategy = cache_strategy::lru(0);
    EXPECT_FALSE(strategy.is_lru());
    EXPECT_EQ(strategy.capacity(), std::nullopt);
}

TEST(cache_strategy_tests, copy_and_equality) {
    const cache_strategy original = cache_strategy::from_capacity(16);
    const cache_strategy copy = original;
    EXPECT_EQ(original, copy);
    EXPECT_EQ(copy, cache_strategy::from_capacity(16));
    EXPECT_NE(cache_strategy::from_capacity(16), cache_strategy::no_cache());
}

} // namespace
} // namespace ba7lya::topic_engine
