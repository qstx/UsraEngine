#include <core/StringInterop.hpp>

#include <string>

int main()
{
	const std::string ascii("a\0b", 3);
	if (Ursa::ToStdString(Ursa::ToUrsaString(ascii)) != ascii)
	{
		return 1;
	}

	const std::string utf8 = "\xE6\xB5\x8B\xE8\xAF\x95.ursatex";
	const Ursa::String engineText = Ursa::ToUrsaString(utf8);
	if (Ursa::ToStdString(engineText) != utf8)
	{
		return 2;
	}

	const auto path = Ursa::PathFromUrsaString(engineText);
	if (Ursa::ToStdString(Ursa::PathToUrsaString(path)) != utf8)
	{
		return 3;
	}
}
