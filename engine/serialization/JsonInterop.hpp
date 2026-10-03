#pragma once

#include <core/StringInterop.hpp>
#include <nlohmann/json.hpp>

namespace nlohmann {
template <> struct adl_serializer<ursa::String> {
    static void to_json(json &value, const ursa::String &text) {
        value = ursa::toStdString(text);
    }

    static void from_json(const json &value, ursa::String &text) {
        text = ursa::toUrsaString(value.get<std::string>());
    }
};

template <class T> struct adl_serializer<ursa::Vector<T>> {
    static void to_json(json &value, const ursa::Vector<T> &items) {
        value = json::array();
        for (const auto &item : items)
            value.push_back(item);
    }

    static void from_json(const json &value, ursa::Vector<T> &items) {
        items.clear();
        for (const auto &item : value)
            items.push_back(item.get<T>());
    }
};
} // namespace nlohmann
