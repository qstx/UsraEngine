#include <vfs/VirtualPath.hpp>

namespace ursa {
namespace {
bool schemeStart(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

bool schemeBody(char character) {
    return schemeStart(character) || (character >= '0' && character <= '9') || character == '_';
}
} // namespace

Result<VirtualPath> VirtualPath::parse(const String &text) {
    const auto colon = text.find(':');
    if (colon == String::npos)
        return Result<VirtualPath>::failure(
            "Resource path needs a scheme, such as ursa:shaders/renderer3d/cube.vert: " + text);
    if (colon == 0)
        return Result<VirtualPath>::failure("Resource path has an empty scheme: " + text);

    String scheme = text.substr(0, colon);
    if (scheme.size() == 1)
        return Result<VirtualPath>::failure("Resource path scheme looks like a host drive letter: " + text);
    if (!schemeStart(scheme.front()))
        return Result<VirtualPath>::failure("Resource path scheme must start with a letter: " + text);
    for (size_t index = 0; index < scheme.size(); ++index) {
        const char character = scheme[index];
        if (!schemeBody(character))
            return Result<VirtualPath>::failure("Resource path scheme has an invalid character: " + text);
        if (character >= 'A' && character <= 'Z')
            scheme[index] = static_cast<char>(character - 'A' + 'a');
    }

    String rest = text.substr(colon + 1);
    for (char &character : rest)
        if (character == '\\')
            character = '/';
    if (rest.empty())
        return Result<VirtualPath>::failure("Resource path needs a relative path after the scheme: " + text);
    if (rest.front() == '/')
        return Result<VirtualPath>::failure("Resource path must be relative to its scheme root: " + text);

    String normalized;
    size_t cursor = 0;
    while (cursor < rest.size()) {
        const auto slash = rest.find('/', cursor);
        const size_t end = slash == String::npos ? rest.size() : slash;
        const String segment = rest.substr(cursor, end - cursor);
        if (!segment.empty() && segment != ".") {
            if (segment == "..")
                return Result<VirtualPath>::failure("Resource path escapes its scheme root: " + text);
            if (segment.find(':') != String::npos)
                return Result<VirtualPath>::failure("Resource path segment has an invalid character: " + text);
            if (!normalized.empty())
                normalized.push_back('/');
            normalized += segment;
        }
        if (slash == String::npos)
            break;
        cursor = slash + 1;
    }
    if (normalized.empty())
        return Result<VirtualPath>::failure("Resource path needs a relative path after the scheme: " + text);

    VirtualPath path;
    path.scheme = std::move(scheme);
    path.relative = std::move(normalized);
    return Result<VirtualPath>::success(std::move(path));
}

String VirtualPath::toString() const {
    return scheme + ":" + relative;
}

String VirtualPath::fileName() const {
    const auto slash = relative.rfind('/');
    return slash == String::npos ? relative : relative.substr(slash + 1);
}

bool operator==(const VirtualPath &left, const VirtualPath &right) {
    return left.scheme == right.scheme && left.relative == right.relative;
}

bool operator!=(const VirtualPath &left, const VirtualPath &right) {
    return !(left == right);
}
} // namespace ursa
