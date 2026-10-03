#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace ursa {
struct ResourceId {
    Array<uint8_t, 16> bytes{};

    explicit operator bool() const;

    String toString() const;

    static ResourceId generate();

    static Result<ResourceId> parse(const String &text);
};

bool operator==(const ResourceId &left, const ResourceId &right);

bool operator!=(const ResourceId &left, const ResourceId &right);
} // namespace ursa
