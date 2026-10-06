#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Serialization/JsonInterop.hpp>
#include <Serialization/ResourceFile.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

namespace Ursa
{
namespace
{
constexpr Ursa::Size textureHeaderBytes = 28;
constexpr Array<Ursa::UInt8, 8> textureMagic{'U', 'R', 'S', 'A', 'T', 'E', 'X', 0};

String Lowered(const String& text)
{
	String result = text;
	std::transform(result.begin(), result.end(), result.begin(),
	               [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	return result;
}

const char* FormatName(ResourceKind kind)
{
	switch (kind)
	{
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

Ursa::UInt32 ReadUint32(const Array<Ursa::UInt8, textureHeaderBytes>& bytes, Ursa::Size offset)
{
	return static_cast<Ursa::UInt32>(bytes[offset]) | (static_cast<Ursa::UInt32>(bytes[offset + 1]) << 8) |
	       (static_cast<Ursa::UInt32>(bytes[offset + 2]) << 16) | (static_cast<Ursa::UInt32>(bytes[offset + 3]) << 24);
}

Result<ResourceHeader> FinishHeader(ResourceHeader header, const String& where)
{
	if (header.version != resourceFormatVersion)
	{
		return Result<ResourceHeader>::Failure("Unsupported resource version " +
		                                       ToUrsaString(std::to_string(header.version)) + " at " + where);
	}
	if (!header.id)
	{
		return Result<ResourceHeader>::Failure("Zero resource ID at " + where);
	}
	return Result<ResourceHeader>::Success(header);
}

Result<ResourceHeader> ParseTextureHeader(const Vector<Ursa::UInt8>& bytes, const String& where)
{
	if (bytes.size() < textureHeaderBytes)
	{
		return Result<ResourceHeader>::Failure("Truncated texture header at " + where);
	}
	Array<Ursa::UInt8, textureHeaderBytes> header{};
	std::copy(bytes.begin(), bytes.begin() + textureHeaderBytes, header.begin());
	if (!std::equal(textureMagic.begin(), textureMagic.end(), header.begin()))
	{
		return Result<ResourceHeader>::Failure("Invalid texture magic at " + where);
	}
	ResourceHeader resource{ResourceKind::Texture};
	resource.version = ReadUint32(header, 8);
	std::copy(header.begin() + 12, header.end(), resource.id.bytes.begin());
	return FinishHeader(resource, where);
}

Result<ResourceHeader> ParseJsonHeader(const Vector<Ursa::UInt8>& bytes, const String& where, ResourceKind kind)
{
	const std::string content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	const auto document = nlohmann::json::parse(content, nullptr, false);
	if (document.is_discarded() || !document.is_object())
	{
		return Result<ResourceHeader>::Failure("Invalid JSON resource at " + where);
	}
	if (!document.contains("format") || !document["format"].is_string() ||
	    document["format"].get<String>() != FormatName(kind))
	{
		return Result<ResourceHeader>::Failure("Invalid resource format at " + where + " field format");
	}
	if (!document.contains("version") || !document["version"].is_number_unsigned() ||
	    document["version"].get<Ursa::UInt64>() > std::numeric_limits<Ursa::UInt32>::max())
	{
		return Result<ResourceHeader>::Failure("Invalid resource version at " + where + " field version");
	}
	ResourceHeader resource{kind};
	resource.version = document["version"].get<Ursa::UInt32>();
	if (!document.contains("id") || !document["id"].is_string())
	{
		return Result<ResourceHeader>::Failure("Invalid resource ID at " + where + " field id");
	}
	auto parsed = ResourceId::Parse(document["id"].get<String>());
	if (!parsed)
	{
		return Result<ResourceHeader>::Failure("Invalid resource ID at " + where + ": " + parsed.error);
	}
	resource.id = parsed.value;
	return FinishHeader(resource, where);
}

Ursa::UInt64 SizeLimitFor(ResourceKind kind)
{
	return kind == ResourceKind::Texture ? maxTextureResourceBytes : maxJsonResourceBytes;
}

Result<ResourceKind> KindFromExtension(const String& extension, const String& where)
{
	const String loweredExtension = Lowered(extension);
	if (loweredExtension == ".ursatex")
	{
		return Result<ResourceKind>::Success(ResourceKind::Texture);
	}
	if (loweredExtension == ".ursamat")
	{
		return Result<ResourceKind>::Success(ResourceKind::Material);
	}
	if (loweredExtension == ".ursascene")
	{
		return Result<ResourceKind>::Success(ResourceKind::Scene);
	}
	if (loweredExtension == ".ursaprefab")
	{
		return Result<ResourceKind>::Success(ResourceKind::Prefab);
	}
	return Result<ResourceKind>::Failure("Unsupported resource file extension: " + where);
}
} // namespace

Result<ResourceKind> ResourceKindForPath(const VirtualPath& path)
{
	const auto dot = path.relative.rfind('.');
	if (dot == String::npos)
	{
		return Result<ResourceKind>::Failure("Unsupported resource file extension: " + path.ToString());
	}
	return KindFromExtension(path.relative.substr(dot), path.ToString());
}

Result<ResourceHeader> ReadResourceHeader(const std::filesystem::path& path)
{
	const String where = PathToUrsaString(path);
	auto kind = KindFromExtension(PathToUrsaString(path.extension()), where);
	if (!kind)
	{
		return Result<ResourceHeader>::Failure(kind.error);
	}

	std::error_code error;
	const uintmax_t size = std::filesystem::file_size(path, error);
	if (error)
	{
		return Result<ResourceHeader>::Failure("Cannot read resource size at " + where + ": " +
		                                       ToUrsaString(error.message()));
	}
	if (size > SizeLimitFor(kind.value))
	{
		return Result<ResourceHeader>::Failure("Resource exceeds size limit at " + where);
	}

	std::ifstream input(path, std::ios::binary);
	if (!input)
	{
		return Result<ResourceHeader>::Failure("Cannot open resource at " + where);
	}
	if (kind.value == ResourceKind::Texture)
	{
		Array<Ursa::UInt8, textureHeaderBytes> header{};
		if (size < header.size() || !input.read(reinterpret_cast<char*>(header.data()), header.size()))
		{
			return Result<ResourceHeader>::Failure("Truncated texture header at " + where);
		}
		Vector<Ursa::UInt8> bytes;
		bytes.reserve(header.size());
		for (Ursa::UInt8 byte : header)
		{
			bytes.push_back(byte);
		}
		return ParseTextureHeader(bytes, where);
	}
	Vector<Ursa::UInt8> bytes(static_cast<Ursa::Size>(size));
	if (!bytes.empty() &&
	    !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
	{
		return Result<ResourceHeader>::Failure("Truncated JSON resource at " + where);
	}
	return ParseJsonHeader(bytes, where, kind.value);
}

Result<ResourceHeader> ReadResourceHeader(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	const String where = path.ToString();
	auto kind = ResourceKindForPath(path);
	if (!kind)
	{
		return Result<ResourceHeader>::Failure(kind.error);
	}
	auto size = filesystem.Size(path);
	if (!size)
	{
		return Result<ResourceHeader>::Failure(size.error);
	}
	if (size.value > SizeLimitFor(kind.value))
	{
		return Result<ResourceHeader>::Failure("Resource exceeds size limit at " + where);
	}
	if (kind.value == ResourceKind::Texture)
	{
		auto bytes = filesystem.ReadPrefix(path, textureHeaderBytes);
		if (!bytes)
		{
			return Result<ResourceHeader>::Failure(bytes.error);
		}
		return ParseTextureHeader(bytes.value, where);
	}
	auto bytes = filesystem.Read(path);
	if (!bytes)
	{
		return Result<ResourceHeader>::Failure(bytes.error);
	}
	return ParseJsonHeader(bytes.value, where, kind.value);
}
} // namespace Ursa
