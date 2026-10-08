#pragma once

#include <vox/core/error.hpp>

#include <cstdlib>
#include <optional>
#include <utility>

namespace vox {

/// Value-or-error return type (a small subset of C++23 std::expected).
/// Accessing the wrong alternative is a programming error and aborts.
template <class T>
class [[nodiscard]] Result {
public:
    // Implicit on purpose so functions can `return value;` or `return error;`.
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    Result(T value)
        : value_(std::move(value)) {}
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    Result(Error error)
        : error_(std::move(error)) {}

    [[nodiscard]] bool hasValue() const noexcept { return value_.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] T& value() & {
        if (!value_.has_value()) {
            std::abort();
        }
        return *value_;
    }
    [[nodiscard]] const T& value() const& {
        if (!value_.has_value()) {
            std::abort();
        }
        return *value_;
    }
    [[nodiscard]] T&& value() && {
        if (!value_.has_value()) {
            std::abort();
        }
        return std::move(*value_);
    }

    [[nodiscard]] const Error& error() const& {
        if (hasValue()) {
            std::abort();
        }
        return error_;
    }

    [[nodiscard]] T valueOr(T fallback) const& { return value_.has_value() ? *value_ : fallback; }

private:
    std::optional<T> value_;
    Error error_;
};

/// Success-or-error for operations without a value.
template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    Result(Error error)
        : error_(std::move(error))
        , failed_(true) {}

    [[nodiscard]] bool hasValue() const noexcept { return !failed_; }
    explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] const Error& error() const& {
        if (!failed_) {
            std::abort();
        }
        return error_;
    }

private:
    Error error_{ErrorCode::InvalidArgument, {}, {}};
    bool failed_ = false;
};

using Status = Result<void>;

/// Convenience constructor: `return makeError(ErrorCode::FileNotFound, "...");`
[[nodiscard]] inline Error makeError(ErrorCode code, std::string message, std::string detail = {}) {
    return Error{code, std::move(message), std::move(detail)};
}

} // namespace vox
