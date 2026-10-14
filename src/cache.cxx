///
/// @file cache.cxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Implementation of the cache strategy factory and the LRU cache.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#include "cache.hxx"

#include <spdlog/spdlog.h>

namespace ba7lya::topic {

cache_strategy cache_strategy::lru(std::size_t capacity) {
    if (capacity == 0) { return no_cache(); }
#ifdef BA7LYA_TOPIC_NO_LRU_CACHE
    spdlog::warn(
        "LRU cache requested with capacity {}, but support for it is compiled out. Falling back to "
        "no caching.",
        capacity
    );
    return no_cache();
#else
    return cache_strategy { capacity };
#endif
}

cache_strategy cache_strategy::from_capacity(std::size_t capacity) { return lru(capacity); }

} // namespace ba7lya::topic
