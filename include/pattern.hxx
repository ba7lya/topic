///
/// @file pattern.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Topic names: the concrete topic name a filter is matched against, and the match that comes out.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
/// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error.hxx"
#include "filter.hxx"

namespace ba7lya::topic {

/// @brief The outcome of building a topic name.
using topic_path_result = result<std::shared_ptr<const class topic_path>, topic_path_error>;

///
/// @brief A concrete topic name, split into its `/`-delimited levels.
/// @details Construction validates well-formedness and splits once; the full text and the level slices then share this
/// single
/// object, so handing the same `topic_path` to several filters costs nothing. Build it through `parse` and
/// carry it in a `std::shared_ptr`.
///
class topic_path {
public:
    /// @brief Validates and splits `path`.
    /// @details Rejects an empty string and any string containing U+0000.
    /// Every other UTF-8 topic is accepted,
    /// spaces and the discouraged-but-legal U+0001..U+001F range included; the 65535-byte ceiling is a wire-codec
    /// concern and is not enforced here.
    [[nodiscard]]
    static topic_path_result parse(std::string path);

    /// @brief The reservation behind the leading-wildcard exclusion: a topic name whose first level begins with `$`
    /// belongs to the `$SYS`/`$share` space and is never matched by a filter whose first level is a wildcard.
    /// @details The exclusion is first-level-only, so the caller asks about the leading level; a `$` past the
    /// leading character is an ordinary literal and matches like any other.
    [[nodiscard]]
    bool has_reserved_first_level() const noexcept {
        return !segments_.empty() && segments_.front().starts_with('$');
    }

    /// @brief The full topic name.
    [[nodiscard]]
    const std::string& text() const noexcept {
        return path_;
    }

    /// @brief The levels, in order. `a/b/c` yields three; `a//b` yields an empty one in the middle.
    [[nodiscard]]
    const std::vector<std::string_view>& segments() const noexcept {
        return segments_;
    }

    /// @brief The number of levels.
    [[nodiscard]]
    std::size_t segment_count() const noexcept {
        return segments_.size();
    }

private:
    /// @brief Splits `path` on `/`; the caller has already established well-formedness.
    explicit topic_path(std::string path);

    /// @brief The full topic name; owns the storage the views in `segments_` point into.
    std::string path_;
    /// @brief Views into `path_`, one per `/`-delimited level.
    std::vector<std::string_view> segments_;
};

/// @brief The result of matching a `topic_path` against a filter.
/// @details Holds the matched topic name plus the level ranges its wildcards captured, reachable by position (`param`)
/// or by name (`named_param`). A `#` capture spans every remaining level and renders as their `/`-joined text.
/// @note Copying is cheap: a `shared_ptr` bump plus one small vector.
class topic_match {
public:
    /// @brief Records a match. Used by the filter's matcher.
    topic_match(
        std::shared_ptr<const topic_path> topic, //
        std::vector<std::pair<std::size_t, std::size_t>> param_ranges,
        std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> named_param_ranges
    )
        : topic_(std::move(topic))
        , param_ranges_(std::move(param_ranges))
        , named_param_ranges_(std::move(named_param_ranges)) {}

    /// @brief The matched topic name's levels.
    [[nodiscard]]
    const std::vector<std::string_view>& path_segments() const {
        return topic_->segments();
    }

    /// @brief The positional parameter captured at `index`, in filter order; empty when out of range.
    [[nodiscard]]
    std::string_view param(std::size_t index) const;

    /// @brief The value captured by the named parameter `name`, if the filter has one.
    [[nodiscard]]
    std::optional<std::string_view> named_param(std::string_view name) const;

    /// @brief The full matched topic name.
    [[nodiscard]]
    const std::string& topic_name() const {
        return topic_->text();
    }

    /// @brief How many positional parameters were captured.
    [[nodiscard]]
    std::size_t param_count() const noexcept {
        return param_ranges_.size();
    }

    /// @brief Renders as `Match(<topic>)`, plus ` with N params` when any were captured.
    [[nodiscard]]
    std::string to_string() const;

private:
    /// @brief Resolves a span of levels to the text it covers, `path_[start] / ... / path_[end - 1]`.
    /// @details An empty span resolves to the empty string; a single level resolves to that level.
    /// Both fall out of
    /// the arithmetic below, since an empty span starts and ends at the same offset.
    [[nodiscard]]
    std::string_view range_text(std::size_t start, std::size_t end) const;

    /// @brief The matched topic name.
    std::shared_ptr<const topic_path> topic_;
    /// @brief Spans of levels captured by positional wildcards, in filter order.
    std::vector<std::pair<std::size_t, std::size_t>> param_ranges_;
    /// @brief Spans of levels captured by named wildcards, paired with the parameter name.
    std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> named_param_ranges_;
};

} // namespace ba7lya::topic
