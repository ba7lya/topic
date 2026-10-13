// SPDX-License-Identifier: MIT
//
// Value-based fallible result, the C++ stand-in for the Rust `Result<T, E>` the engine was
// ported from.

#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace ba7lya::topic_engine {

/// Which of the two states a `result` currently holds.
enum class result_status : std::uint8_t { OK, ERROR };

/// A value or an error, never both.
///
/// `std::expected` would be the natural spelling, but it is missing from MSVC 14.29 (VS2019),
/// which the project's CI matrix still builds with, so the two-state result is written here
/// instead of relying on the C++23 library type.
///
/// Constructing from `T` produces a value and constructing from `E` produces an error, so a
/// function body can simply `return some_value;` or `return some_error;`.
template<typename T, typename E>
class result {
public:
    /// Wraps `value` as a success.
    result(T value)
        : storage_(std::in_place_index<0>, std::move(value)) {}

    /// Wraps `error` as a failure.
    result(E error)
        : storage_(std::in_place_index<1>, std::move(error)) {}

    /// Wraps one alternative of a variant-typed `E` as a failure.
    ///
    /// Without this a caller would have to spell the variant type at every return site — the
    /// ported code returns the concrete error struct, as the Rust original does.
    template<typename G>
        requires(
            !std::same_as<std::remove_cvref_t<G>, T> && !std::same_as<std::remove_cvref_t<G>, result>
            && std::constructible_from<E, G>
        )
    result(G&& error)
        : storage_(std::in_place_index<1>, E { std::forward<G>(error) }) {}

    /// Returns `true` when this result holds a value.
    [[nodiscard]]
    bool has_value() const noexcept {
        return storage_.index() == 0;
    }

    /// Returns `true` when this result holds a value.
    [[nodiscard]]
    explicit operator bool() const noexcept {
        return has_value();
    }

    /// Returns the held value; undefined when this result holds an error.
    [[nodiscard]]
    T& value() & noexcept {
        return std::get<0>(storage_);
    }

    /// Returns the held value; undefined when this result holds an error.
    [[nodiscard]]
    const T& value() const& noexcept {
        return std::get<0>(storage_);
    }

    /// Returns the held value, moving it out; undefined when this result holds an error.
    [[nodiscard]]
    T&& value() && noexcept {
        return std::move(std::get<0>(storage_));
    }

    /// Returns the held error; undefined when this result holds a value.
    [[nodiscard]]
    E& error() & noexcept {
        return std::get<1>(storage_);
    }

    /// Returns the held error; undefined when this result holds a value.
    [[nodiscard]]
    const E& error() const& noexcept {
        return std::get<1>(storage_);
    }

    /// Returns the held value; undefined when this result holds an error.
    [[nodiscard]]
    T& operator*() noexcept {
        return value();
    }

    /// Returns the held value; undefined when this result holds an error.
    [[nodiscard]]
    const T& operator*() const noexcept {
        return value();
    }

    /// Returns the held value; undefined when this result holds an error.
    [[nodiscard]]
    T* operator->() noexcept {
        return &value();
    }

    /// Returns the held value; undefined when this result holds an error.
    [[nodiscard]]
    const T* operator->() const noexcept {
        return &value();
    }

private:
    std::variant<T, E> storage_;
};

} // namespace ba7lya::topic_engine
