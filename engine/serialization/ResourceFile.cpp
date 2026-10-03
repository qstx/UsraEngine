#include <serialization/JsonInterop.hpp>
#include <serialization/ResourceFile.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

namespace ursa {
namespace {
constexpr size_t textureHeaderBytes = 28;
constexpr Array<uint8_t, 8> textureMagic{'U', 'R', 'S', 'A', 'T', 'E', 'X', 0};

String lowered(const String &text) {
    String result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return result;
}

const char *formatName(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::Material:
        return "UrsaMaterial";
    case ResourceKind::Scene:
        return "UrsaScene";
    case ResourceKind::Prefab:
        return "UrsaPrefab";
    case ResourceKind::Texture:
        return "UrsaTexture";
    }
    return "";
}

uint32_t readUint32(const Array<uint8_t, textureHeaderBytes> &bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

Result<ResourceHeader> finishHeader(ResourceHeader header, const String &where) {
    if (header.version != resourceFormatVersion)
        return Result<ResourceHeader>::failure("Unsupported resource version " +
                                               toUrsaString(std::to_string(header.version)) + " at " + where);
    if (!header.id)
        return Result<ResourceHeader>::failure("Zero resource ID at " + where);
    return Result<ResourceHeader>::success(header);
}

Result<ResourceHeader> parseTextureHeader(const Vector<uint8_t> &bytes, const String &where) {
    if (bytes.size() < textureHeaderBytes)
        return Result<ResourceHeader>::failure("Truncated texture header at " + where);
    Array<uint8_t, textureHeaderBytes> header{};
    std::copy(bytes.begin(), bytes.begin() + textureHeaderBytes, header.begin());
    if (!std::equal(textureMagic.begin(), textureMagic.end(), header.begin()))
        return Result<ResourceHeader>::failure("Invalid texture magic at " + where);
    ResourceHeader resource{ResourceKind::Texture};
    resource.version = readUint32(header, 8);
    std::copy(header.begin() + 12, header.end(), resource.id.bytes.begin());
    return finishHeader(resource, where);
}

Result<ResourceHeader> parseJsonHeader(const Vector<uint8_t> &bytes, const String &where, ResourceKind kind) {
    const std::string content(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    const auto document = nlohmann::json::parse(content, nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return Result<ResourceHeader>::failure("Invalid JSON resource at " + where);
    if (!document.contains("format") || !document["format"].is_string() ||
        document["format"].get<String>() != formatName(kind))
        return Result<ResourceHeader>::failure("Invalid resource format at " + where + " field format");
    if (!document.contains("version") || !document["version"].is_number_unsigned() ||
        document["version"].get<uint64_t>() > std::numeric_limits<uint32_t>::max())
        return Result<ResourceHeader>::failure("Invalid resource version at " + where + " field version");
    ResourceHeader resource{kind};
    resource.version = document["version"].get<uint32_t>();
    if (!document.contains("id") || !document["id"].is_string())
        return Result<ResourceHeader>::failure("Invalid resource ID at " + where + " field id");
    auto parsed = ResourceId::parse(document["id"].get<String>());
    if (!parsed)
        return Result<ResourceHeader>::failure("Invalid resource ID at " + where + ": " + parsed.error);
    resource.id = parsed.value;
    return finishHeader(resource, where);
}

uint64_t sizeLimitFor(ResourceKind kind) {
    return kind == ResourceKind::Texture ? maxTextureResourceBytes : maxJsonResourceBytes;
}

Result<ResourceKind> kindFromExtension(const String &extension, const String &where) {
    const String loweredExtension = lowered(extension);
    if (loweredExtension == ".ursatex")
        return Result<ResourceKind>::success(ResourceKind::Texture);
    if (loweredExtension == ".ursamat")
        return Result<ResourceKind>::success(ResourceKind::Material);
    if (loweredExtension == ".ursascene")
        return Result<ResourceKind>::success(ResourceKind::Scene);
    if (loweredExtension == ".ursaprefab")
        return Result<ResourceKind>::success(ResourceKind::Prefab);
    return Result<ResourceKind>::failure("Unsupported resource file extension: " + where);
}
} // namespace

Result<ResourceKind> resourceKindForPath(const VirtualPath &path) {
    const auto dot = path.relative.rfind('.');
    if (dot == String::npos)
        return Result<ResourceKind>::failure("Unsupported resource file extension: " + path.toString());
    return kindFromExtension(path.relative.substr(dot), path.toString());
}

Result<ResourceHeader> readResourceHeader(const std::filesystem::path &path) {
    const String where = pathToUrsaString(path);
    auto kind = kindFromExtension(pathToUrsaString(path.extension()), where);
    if (!kind)
        return Result<ResourceHeader>::failure(kind.error);

    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
        return Result<ResourceHeader>::failure("Cannot read resource size at " + where + ": " +
                                               toUrsaString(error.message()));
    if (size > sizeLimitFor(kind.value))
        return Result<ResourceHeader>::failure("Resource exceeds size limit at " + where);

    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Result<ResourceHeader>::failure("Cannot open resource at " + where);
    if (kind.value == ResourceKind::Texture) {
        Array<uint8_t, textureHeaderBytes> header{};
        if (size < header.size() || !input.read(reinterpret_cast<char *>(header.data()), header.size()))
            return Result<ResourceHeader>::failure("Truncated texture header at " + where);
        Vector<uint8_t> bytes;
        bytes.reserve(header.size());
        for (uint8_t byte : header)
            bytes.push_back(byte);
        return parseTextureHeader(bytes, where);
    }
    Vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!bytes.empty() &&
        !input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return Result<ResourceHeader>::failure("Truncated JSON resource at " + where);
    return parseJsonHeader(bytes, where, kind.value);
}

Result<ResourceHeader> readResourceHeader(const VirtualFileSystem &filesystem, const VirtualPath &path) {
    const String where = path.toString();
    auto kind = resourceKindForPath(path);
    if (!kind)
        return Result<ResourceHeader>::failure(kind.error);
    auto size = filesystem.size(path);
    if (!size)
        return Result<ResourceHeader>::failure(size.error);
    if (size.value > sizeLimitFor(kind.value))
        return Result<ResourceHeader>::failure("Resource exceeds size limit at " + where);
    if (kind.value == ResourceKind::Texture) {
        auto bytes = filesystem.readPrefix(path, textureHeaderBytes);
        if (!bytes)
            return Result<ResourceHeader>::failure(bytes.error);
        return parseTextureHeader(bytes.value, where);
    }
    auto bytes = filesystem.read(path);
    if (!bytes)
        return Result<ResourceHeader>::failure(bytes.error);
    return parseJsonHeader(bytes.value, where, kind.value);
}
} // namespace ursa
