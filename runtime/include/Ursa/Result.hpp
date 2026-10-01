#pragma once
#include <string>
#include <utility>

namespace ursa {
template <class T> struct Result {
    T value{};
    std::string error;

    explicit operator bool() const {
        return error.empty();
    }

    static Result success(T v) {
        Result r;
        r.value = std::move(v);
        return r;
    }

    static Result failure(std::string e) {
        Result r;
        r.error = std::move(e);
        return r;
    }
};

template <> struct Result<void> {
    std::string error;

    explicit operator bool() const {
        return error.empty();
    }

    static Result success() {
        return {};
    }

    static Result failure(std::string e) {
        return {std::move(e)};
    }
};
} // namespace ursa
