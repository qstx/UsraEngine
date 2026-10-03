#include <core/StringInterop.hpp>
#include <serialization/TextureResource.hpp>
#include <serialization/AtomicFile.hpp>
#include <serialization/ResourceFile.hpp>
#include <array>
#include <fstream>
#include <limits>
#include <vector>

namespace ursa {
namespace {
constexpr size_t textureHeaderBytes = 40;

void encodeUint32(Array<uint8_t, textureHeaderBytes> &bytes, size_t offset, uint32_t value) {
    for (size_t index = 0; index < 4; ++index)
        bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

uint32_t decodeUint32(const Array<uint8_t, textureHeaderBytes> &bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}
} // namespace

Result<void> saveTextureResource(const std::filesystem::path &path, ResourceId id, const MaterialTexture &texture) {
    if (!id)
        return Result<void>::failure("Cannot save texture with a zero resource ID at " + pathToUrsaString(path));
    if (path.extension() != ".ursatex")
        return Result<void>::failure("Texture resource needs .ursatex extension: " + pathToUrsaString(path));
    if (texture.pixels().size() > maxTextureResourceBytes - textureHeaderBytes)
        return Result<void>::failure("Texture resource exceeds size limit at " + pathToUrsaString(path));
    Array<uint8_t, textureHeaderBytes> header{};
    constexpr Array<uint8_t, 8> magic{'U', 'R', 'S', 'A', 'T', 'E', 'X', 0};
    std::copy(magic.begin(), magic.end(), header.begin());
    encodeUint32(header, 8, resourceFormatVersion);
    std::copy(id.bytes.begin(), id.bytes.end(), header.begin() + 12);
    encodeUint32(header, 28, texture.width());
    encodeUint32(header, 32, texture.height());
    header[36] = texture.colorSpace() == TextureColorSpace::Srgb ? 1 : 0;
    return writeResourceAtomically(path, [&](std::ostream &output) {
        output.write(reinterpret_cast<const char *>(header.data()), header.size());
        output.write(reinterpret_cast<const char *>(texture.pixels().data()),
                     static_cast<std::streamsize>(texture.pixels().size()));
        return output ? Result<void>{}
                      : Result<void>::failure("Cannot write texture resource " + pathToUrsaString(path));
    });
}

Result<LoadedTextureResource> loadTextureResource(const std::filesystem::path &path) {
    auto resource = readResourceHeader(path);
    if (!resource)
        return Result<LoadedTextureResource>::failure(resource.error);
    if (resource.value.kind != ResourceKind::Texture)
        return Result<LoadedTextureResource>::failure("Expected a texture resource at " + pathToUrsaString(path));

    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size < textureHeaderBytes)
        return Result<LoadedTextureResource>::failure("Truncated texture resource at " + pathToUrsaString(path));
    std::ifstream input(path, std::ios::binary);
    Array<uint8_t, textureHeaderBytes> header{};
    if (!input.read(reinterpret_cast<char *>(header.data()), header.size()))
        return Result<LoadedTextureResource>::failure("Truncated texture header at " + pathToUrsaString(path));
    const uint32_t width = decodeUint32(header, 28);
    const uint32_t height = decodeUint32(header, 32);
    if (!width || !height || static_cast<uint64_t>(width) * height > (maxTextureResourceBytes - textureHeaderBytes) / 4)
        return Result<LoadedTextureResource>::failure("Invalid texture dimensions at " + pathToUrsaString(path));
    const uint64_t expected = textureHeaderBytes + static_cast<uint64_t>(width) * height * 4;
    if (size != expected)
        return Result<LoadedTextureResource>::failure("Texture pixel byte count does not match dimensions at " +
                                                      pathToUrsaString(path));
    if (header[36] > 1 || header[37] || header[38] || header[39])
        return Result<LoadedTextureResource>::failure("Invalid texture color space or reserved bytes at " +
                                                      pathToUrsaString(path));
    Vector<uint8_t> pixels(static_cast<size_t>(expected - textureHeaderBytes));
    if (!input.read(reinterpret_cast<char *>(pixels.data()), static_cast<std::streamsize>(pixels.size())))
        return Result<LoadedTextureResource>::failure("Truncated texture pixel data at " + pathToUrsaString(path));
    auto texture = MaterialTexture::create(
        width, height, header[36] ? TextureColorSpace::Srgb : TextureColorSpace::Linear, std::move(pixels));
    if (!texture)
        return Result<LoadedTextureResource>::failure("Invalid texture resource at " + pathToUrsaString(path) + ": " +
                                                      texture.error);
    return Result<LoadedTextureResource>::success({resource.value.id, std::move(texture.value)});
}
} // namespace ursa
