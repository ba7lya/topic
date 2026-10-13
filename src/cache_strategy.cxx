// SPDX-License-Identifier: MIT

#include "cache_strategy.hxx"

#include <spdlog/spdlog.h>

namespace ba7lya::topic_engine {

cache_strategy cache_strategy::lru(std::size_t capacity) {
    if (capacity == 0) { return no_cache(); }
#ifdef BA7LYA_TOPIC_ENGINE_NO_LRU_CACHE
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

} // namespace ba7lya::topic_engine
