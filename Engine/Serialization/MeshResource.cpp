#include <Serialization/MeshResource.hpp>
#include <Core/Memory/Memory.hpp>
#include <algorithm>

namespace Ursa
{
Result<Vector<UInt8>> EncodeMeshResource(ResourceId id, const Mesh& mesh, UInt64 localFileId, bool binary)
{
	if (!id)
	{
		return Result<Vector<UInt8>>::Failure("Mesh resource requires a nonzero ID");
	}
	ArchiveWriter writer;
	auto result = writer.Write("format", String("UrsaMesh"));
	if (result)
	{
		result = writer.Write("version", UInt64(resourceFormatVersion));
	}
	if (result)
	{
		result = writer.Write("id", id.ToString());
	}
	if (result)
	{
		result = writer.Write("localFileId", localFileId);
	}
	auto data = writer.Object("data");
	if (!result || !data)
	{
		return Result<Vector<UInt8>>::Failure(result ? data.error : result.error);
	}
	result = mesh.Serialize(data.value);
	if (!result)
	{
		return Result<Vector<UInt8>>::Failure(result.error);
	}
	auto bytes = binary ? BinaryArchive::Encode(writer) : JsonArchive::Encode(writer);
	if (!bytes)
	{
		return Result<Vector<UInt8>>::Failure(bytes.error);
	}
	if (bytes.value.size() > maxMeshResourceBytes - 40)
	{
		return Result<Vector<UInt8>>::Failure("Mesh resource exceeds encoded size limit");
	}
	if (binary)
	{
		Vector<UInt8> envelope(40, 0);
		const Array<UInt8, 8> magic{'U', 'R', 'S', 'A', 'M', 'S', 'H', 0};
		std::copy(magic.begin(), magic.end(), envelope.begin());
		envelope[8] = resourceFormatVersion;
		std::copy(id.bytes.begin(), id.bytes.end(), envelope.begin() + 12);
		for (Size i = 0; i < 8; ++i)
		{
			envelope[28 + i] = UInt8(UInt64(bytes.value.size()) >> (8 * i));
		}
		envelope.insert(envelope.end(), bytes.value.begin(), bytes.value.end());
		return Result<Vector<UInt8>>::Success(std::move(envelope));
	}
	return bytes;
}

Result<void> SaveMeshResource(VirtualFileSystem& files, const VirtualPath& path, ResourceId id, const Mesh& mesh,
                              bool binary)
{
	auto kind = ResourceKindForPath(path);
	if (!kind || kind.value != ResourceKind::Mesh)
	{
		return Result<void>::Failure("Mesh resource requires .ursamesh path");
	}
	auto encoded = EncodeMeshResource(id, mesh, 0, binary);
	return encoded ? files.WriteAtomic(path, encoded.value) : Result<void>::Failure(encoded.error);
}

Result<LoadedMeshResource> LoadMeshResource(const VirtualFileSystem& files, const VirtualPath& path,
                                            SerializationContext& context)
{
	auto header = ReadResourceHeader(files, path);
	if (!header || header.value.kind != ResourceKind::Mesh)
	{
		return Result<LoadedMeshResource>::Failure(header ? "Expected Mesh resource at " + path.ToString()
		                                                  : header.error);
	}
	auto bytes = files.Read(path);
	if (!bytes)
	{
		return Result<LoadedMeshResource>::Failure(bytes.error);
	}
	const bool binary = bytes.value.size() >= 40 && bytes.value[0] == 'U';
	if (binary)
	{
		bytes.value.erase(bytes.value.begin(), bytes.value.begin() + 40);
	}
	auto reader = binary ? BinaryArchive::Decode(bytes.value, maxMeshResourceBytes)
	                     : JsonArchive::Decode(bytes.value, maxMeshResourceBytes);
	if (!reader)
	{
		return Result<LoadedMeshResource>::Failure(reader.error);
	}
	auto format = reader.value.Text("format");
	auto version = reader.value.Unsigned("version");
	auto id = reader.value.Text("id");
	auto data = reader.value.Object("data");
	auto localFileId = reader.value.Unsigned("localFileId");
	if (!format || format.value != "UrsaMesh" || !version || version.value != resourceFormatVersion || !id ||
	    id.value != header.value.id.ToString() || !data || !localFileId)
	{
		return Result<LoadedMeshResource>::Failure("Mesh envelope and payload metadata disagree at " + path.ToString());
	}
	auto mesh = Mesh::Create();
	if (!mesh)
	{
		return Result<LoadedMeshResource>::Failure(mesh.error);
	}
	auto loaded = mesh.value->Deserialize(data.value, context);
	if (!loaded)
	{
		return Result<LoadedMeshResource>::Failure(loaded.error + " at " + path.ToString());
	}
	loaded = mesh.value->BindAssetIdentity({header.value.id, localFileId.value});
	return loaded ? Result<LoadedMeshResource>::Success({header.value.id, localFileId.value, std::move(mesh.value)})
	              : Result<LoadedMeshResource>::Failure(loaded.error);
}
} // namespace Ursa
