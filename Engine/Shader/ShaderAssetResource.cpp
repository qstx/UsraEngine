#include <Shader/ShaderAssetResource.hpp>

#include <Serialization/JsonArchiveInterop.hpp>
#include <Serialization/JsonInterop.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Vfs/PakArchive.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>

namespace Ursa::Shader
{
namespace
{
constexpr Ursa::Size shaderHeaderBytes = 40;
constexpr Array<UInt8, 8> shaderMagic{'U', 'R', 'S', 'A', 'S', 'H', 'D', 0};

void EncodeUint32(Vector<UInt8>& bytes, Ursa::Size offset, UInt32 value)
{
	for (Ursa::Size index = 0; index < 4; ++index)
	{
		bytes[offset + index] = static_cast<UInt8>(value >> (index * 8));
	}
}

void EncodeUint64(Vector<UInt8>& bytes, Ursa::Size offset, UInt64 value)
{
	for (Ursa::Size index = 0; index < 8; ++index)
	{
		bytes[offset + index] = static_cast<UInt8>(value >> (index * 8));
	}
}

UInt32 DecodeUint32(const Vector<UInt8>& bytes, Ursa::Size offset)
{
	return static_cast<UInt32>(bytes[offset]) | (static_cast<UInt32>(bytes[offset + 1]) << 8) |
	       (static_cast<UInt32>(bytes[offset + 2]) << 16) | (static_cast<UInt32>(bytes[offset + 3]) << 24);
}
} // namespace

Result<void> SaveShaderAssetResource(VirtualFileSystem& filesystem, const VirtualPath& path, const ShaderAsset& shader)
{
	const auto kind = ResourceKindForPath(path);
	if (!shader.id || !kind || kind.value != ResourceKind::Shader)
	{
		return Result<void>::Failure("Shader asset needs a valid resource ID and .ursashaderasset path: " +
		                             path.ToString());
	}
	ArchiveWriter writer;
	auto serialized = shader.Serialize(writer);
	if (!serialized)
	{
		return Result<void>::Failure(path.ToString() + ": " + serialized.error);
	}
	auto document = ArchiveToJson(writer);
	if (!document)
	{
		return Result<void>::Failure(path.ToString() + ": " + document.error);
	}
	document.value["format"] = "UrsaShader";
	document.value["version"] = resourceFormatVersion;
	document.value["id"] = shader.id.ToString();
	try
	{
		const std::string json = document.value.dump(2);
		if (json.size() > maxShaderResourceBytes - shaderHeaderBytes)
		{
			return Result<void>::Failure("Shader resource exceeds size limit at " + path.ToString());
		}
		if (json.size() > std::numeric_limits<UInt64>::max() - shaderHeaderBytes)
		{
			return Result<void>::Failure("Shader resource size overflows its header at " + path.ToString());
		}
		Vector<UInt8> bytes(shaderHeaderBytes, 0);
		std::copy(shaderMagic.begin(), shaderMagic.end(), bytes.begin());
		EncodeUint32(bytes, 8, resourceFormatVersion);
		std::copy(shader.id.bytes.begin(), shader.id.bytes.end(), bytes.begin() + 12);
		EncodeUint64(bytes, 28, static_cast<UInt64>(json.size()));
		const auto* payload = reinterpret_cast<const UInt8*>(json.data());
		EncodeUint32(bytes, 36, PakChecksum(payload, json.size()));
		bytes.insert(bytes.end(), payload, payload + json.size());
		return filesystem.WriteAtomic(path, bytes, true);
	}
	catch (const nlohmann::json::exception& error)
	{
		return Result<void>::Failure("Cannot encode Shader resource " + path.ToString() + ": " + error.what());
	}
}

Result<ShaderAsset> LoadShaderAssetResource(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	auto header = ReadResourceHeader(filesystem, path);
	if (!header || header.value.kind != ResourceKind::Shader)
	{
		return Result<ShaderAsset>::Failure(header ? "Expected a Shader asset at " + path.ToString() : header.error);
	}
	auto bytes = filesystem.Read(path);
	if (!bytes || bytes.value.size() < shaderHeaderBytes)
	{
		return Result<ShaderAsset>::Failure(bytes ? "Truncated Shader asset at " + path.ToString() : bytes.error);
	}
	const UInt32 expectedChecksum = DecodeUint32(bytes.value, 36);
	const UInt32 actualChecksum =
	    PakChecksum(bytes.value.data() + shaderHeaderBytes, bytes.value.size() - shaderHeaderBytes);
	if (expectedChecksum != actualChecksum)
	{
		return Result<ShaderAsset>::Failure("Shader payload checksum mismatch at " + path.ToString());
	}
	const std::string jsonText(bytes.value.begin() + shaderHeaderBytes, bytes.value.end());
	const auto document = nlohmann::json::parse(jsonText, nullptr, false);
	if (document.is_discarded() || !document.is_object() || !document.contains("format") ||
	    document["format"] != "UrsaShader" || !document.contains("version") ||
	    document["version"] != resourceFormatVersion || !document.contains("id") || !document["id"].is_string())
	{
		return Result<ShaderAsset>::Failure("Invalid Shader resource payload at " + path.ToString());
	}
	auto reader = ArchiveFromJson(document);
	if (!reader)
	{
		return Result<ShaderAsset>::Failure(path.ToString() + ": " + reader.error);
	}
	ShaderAsset shader;
	SerializationContext context;
	auto deserialized = shader.Deserialize(reader.value, context);
	if (!deserialized)
	{
		return Result<ShaderAsset>::Failure(path.ToString() + ": " + deserialized.error);
	}
	auto id = ResourceId::Parse(ToUrsaString(document["id"].get<std::string>()));
	if (!id || id.value != shader.id)
	{
		return Result<ShaderAsset>::Failure(path.ToString() + ": Shader resource ID differs from its payload");
	}
	return Result<ShaderAsset>::Success(std::move(shader));
}
} // namespace Ursa::Shader
