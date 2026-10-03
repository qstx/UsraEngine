#pragma once

#include <core/Containers.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace ursa {
inline String toUrsaString(std::string_view text) {
    return String(text.data(), text.size());
}

inline std::string toStdString(const String &text) {
    return std::string(text.data(), text.size());
}

inline String pathToUrsaString(const std::filesystem::path &path) {
    return toUrsaString(path.u8string());
}

inline std::filesystem::path pathFromUrsaString(const String &utf8) {
    return std::filesystem::u8path(toStdString(utf8));
}
} // namespace ursa
