// SPDX-License-Identifier: MIT
//
// One error type covering every topic operation.
//
// The per-module error types stay where they are; this is the aggregate the public API hands back
// when a caller does not care which stage failed.

#pragma once

#include <string>
#include <variant>

#include "result.hxx"
#include "topic_match.hxx"
#include "topic_pattern_item.hxx"
#include "topic_pattern_path.hxx"

#ifndef BA7LYA_TOPIC_ENGINE_NO_ROUTER
#include "topic_matcher.hxx"
#include "topic_router.hxx"
#endif

namespace ba7lya::topic_engine {

/// A topic pattern failed to parse or validate.
struct pattern_stage_error {
    /// What the pattern parser objected to.
    topic_pattern_error cause;

    /// Renders as `"Topic pattern error: <cause>"`.
    [[nodiscard]]
    std::string message() const;
};

/// A topic failed to format for publishing.
struct format_stage_error {
    /// What the formatter objected to.
    topic_format_failure cause;

    /// Renders as `"Topic format error: <cause>"`.
    [[nodiscard]]
    std::string message() const;
};

#ifndef BA7LYA_TOPIC_ENGINE_NO_ROUTER
/// The topic matcher failed.
struct matcher_stage_error {
    /// What the matcher objected to.
    topic_matcher_error cause;

    /// Renders as `"Topic matcher error: <cause>"`.
    [[nodiscard]]
    std::string message() const;
};

/// The topic router failed.
struct router_stage_error {
    /// What the router objected to.
    topic_router_error cause;

    /// Renders as `"Topic router error: <cause>"`.
    [[nodiscard]]
    std::string message() const;
};
#endif

/// Everything that can go wrong in a topic operation, whichever stage it failed at.
#ifdef BA7LYA_TOPIC_ENGINE_NO_ROUTER
using topic_error = std::variant<pattern_stage_error, format_stage_error>;
#else
using topic_error = std::variant<pattern_stage_error, format_stage_error, matcher_stage_error, router_stage_error>;
#endif

/// A topic operation's outcome.
template<typename T>
using topic_result = result<T, topic_error>;

/// Renders an error the way the Rust original's `Display` impls do.
[[nodiscard]]
std::string describe(const topic_error& error);

// Each aggregate alternative is constructible from the per-module error it wraps, so a caller can
// return one without naming the wrapper. Written by hand because a variadic using-declaration does
// not exist.

/// Wraps a pattern error.
[[nodiscard]]
inline topic_error make_topic_error(const topic_pattern_error& cause) {
    return pattern_stage_error { cause };
}

/// Wraps a format error.
[[nodiscard]]
inline topic_error make_topic_error(const topic_format_failure& cause) {
    return format_stage_error { cause };
}

#ifndef BA7LYA_TOPIC_ENGINE_NO_ROUTER
/// Wraps a matcher error.
[[nodiscard]]
inline topic_error make_topic_error(const topic_matcher_error& cause) {
    return matcher_stage_error { cause };
}

/// Wraps a router error.
[[nodiscard]]
inline topic_error make_topic_error(const topic_router_error& cause) {
    return router_stage_error { cause };
}
#endif

} // namespace ba7lya::topic_engine
