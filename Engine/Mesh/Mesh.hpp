#pragma once

#include <Mesh/MeshData.hpp>
#include <Serialization/Serializable.hpp>
#include <atomic>
#include <functional>
#include <mutex>

namespace Ursa
{
class Mesh final : public Serializable
{
public:
	static constexpr UInt64 maxDataBytes = 256ull * 1024 * 1024;
	static constexpr Size maxSubMeshes = 4096;

	Mesh();

	Mesh(const Mesh&) = delete;

	Mesh& operator=(const Mesh&) = delete;

	static Result<std::shared_ptr<Mesh>> Create(MeshData data = {});

	static Result<void> Validate(const MeshData& data);

	String SerializationType() const override;

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	UInt64 Identity() const;

	UInt64 Version() const;

	bool IsValid() const;

	std::shared_ptr<const MeshSnapshot> Snapshot() const;

	Result<MeshData> GetData() const;

	Result<void> SetData(MeshData data);

	Result<void> SetVertices(Vector<Vec3> values);

	Result<void> SetNormals(Vector<Vec3> values);

	Result<void> SetUv0(Vector<Vec2> values);

	Result<void> SetColors(Vector<Color> values);

	Result<void> SetTangents(Vector<Vec4> values);

	Result<void> SetIndices(Vector<UInt32> values, Vector<SubMesh> subMeshes = {});

	Result<void> SetSubMeshCount(Size count);

	Result<void> SetSubMesh(Size index, SubMesh value);

	Result<Vector<UInt32>> GetTriangles(Size subMesh) const;

	Result<void> SetTriangles(Size subMesh, Vector<UInt32> values);

	Result<void> RecalculateNormals();

	Result<void> RecalculateBounds();

	Result<void> Clear();

	Result<std::shared_ptr<Mesh>> Clone() const;

	Result<void> Destroy();

	ResourceId LocalIdentity() const
	{
		return localIdentity;
	}

	Result<void> BindLocalIdentity(ResourceId id)
	{
		if (!id)
		{
			return Result<void>::Failure("Local resource identity cannot be zero");
		}
		localIdentity = id;
		return {};
	}

	AssetReference AssetIdentity() const;

	Result<void> BindAssetIdentity(AssetReference reference);

private:
	friend class ResourceSession;
	Result<void> Update(const std::function<Result<void>(MeshData&)>& edit);

	Result<void> Publish(MeshData data);

	mutable std::mutex mutex;
	UInt64 identity = 0;
	AssetReference assetIdentity;
	ResourceId localIdentity = ResourceId::Generate();
	std::atomic_bool valid{true};
	std::shared_ptr<const MeshSnapshot> snapshot;
};

using MeshPtr = std::shared_ptr<Mesh>;
} // namespace Ursa
