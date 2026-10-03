#pragma once

#include <Core/Containers.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace Ursa
{
inline String ToUrsaString(std::string_view text)
{
	return String(text.data(), text.size());
}

inline std::string ToStdString(const String& text)
{
	return std::string(text.data(), text.size());
}

inline String PathToUrsaString(const std::filesystem::path& path)
{
	return ToUrsaString(path.u8string());
}

inline std::filesystem::path PathFromUrsaString(const String& utf8)
{
	return std::filesystem::u8path(ToStdString(utf8));
}
} // namespace Ursa
