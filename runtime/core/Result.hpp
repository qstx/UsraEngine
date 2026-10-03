#pragma once
#include <core/Containers.hpp>
#include <utility>

namespace ursa {
template <class T> struct Result {
    T value{};
    String error;

    explicit operator bool() const {
        return error.empty();
    }

    static Result success(T v) {
        Result r;
        r.value = std::move(v);
        return r;
    }

    static Result failure(String e) {
        Result r;
        r.error = std::move(e);
        return r;
    }
};

template <> struct Result<void> {
    String error;

    explicit operator bool() const {
        return error.empty();
    }

    static Result success() {
        return {};
    }

    static Result failure(String e) {
        return {std::move(e)};
    }
};
} // namespace ursa
