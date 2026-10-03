#pragma once

#include <core/StringInterop.hpp>
#include <nlohmann/json.hpp>

namespace nlohmann
{
template <> struct adl_serializer<Ursa::String>
{
	static void to_json(json& value, const Ursa::String& text)
	{
		value = Ursa::ToStdString(text);
	}

	static void from_json(const json& value, Ursa::String& text)
	{
		text = Ursa::ToUrsaString(value.get<std::string>());
	}
};

template <class T> struct adl_serializer<Ursa::Vector<T>>
{
	static void to_json(json& value, const Ursa::Vector<T>& items)
	{
		value = json::array();
		for (const auto& item : items)
		{
			value.push_back(item);
		}
	}

	static void from_json(const json& value, Ursa::Vector<T>& items)
	{
		items.clear();
		for (const auto& item : value)
		{
			items.push_back(item.get<T>());
		}
	}
};
} // namespace nlohmann
