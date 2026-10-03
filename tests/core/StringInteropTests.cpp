#include <core/StringInterop.hpp>

#include <string>

int main() {
    const std::string ascii("a\0b", 3);
    if (ursa::toStdString(ursa::toUrsaString(ascii)) != ascii)
        return 1;

    const std::string utf8 = "\xE6\xB5\x8B\xE8\xAF\x95.ursatex";
    const ursa::String engineText = ursa::toUrsaString(utf8);
    if (ursa::toStdString(engineText) != utf8)
        return 2;

    const auto path = ursa::pathFromUrsaString(engineText);
    if (ursa::toStdString(ursa::pathToUrsaString(path)) != utf8)
        return 3;
}
