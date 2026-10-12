#include <Scene/MeshFilterComponent.hpp>
#include <Scene/GameObject.hpp>
#include <Serialization/RenderResourceReferences.hpp>

namespace Ursa
{
String MeshFilterComponent::SerializationType() const
{
	return "ursa.MeshFilter";
}

const char* MeshFilterComponent::UniquenessGroup() const
{
	return "ursa.MeshFilter";
}

bool MeshFilterComponent::SupportsEnabled() const
{
	return false;
}

Result<void> MeshFilterComponent::Serialize(ArchiveWriter& writer) const
{
	auto result = WriteMeshBinding(writer, "mesh", mesh);
	return result ? writer.Write("instantiated", instantiated) : result;
}

Result<void> MeshFilterComponent::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	auto loaded = ReadMeshBinding(reader, "mesh", context);
	if (!loaded)
	{
		return Result<void>::Failure(loaded.error);
	}
	auto flag = reader.Contains("instantiated") ? reader.Boolean("instantiated") : Result<bool>::Success(false);
	if (!flag || (flag.value && (!loaded.value || loaded.value->AssetIdentity().guid)))
	{
		return Result<void>::Failure("Invalid MeshFilter instance marker");
	}
	auto result = SetSharedMesh(std::move(loaded.value));
	if (result)
	{
		instantiated = flag.value;
	}
	return result;
}

MeshPtr MeshFilterComponent::SharedMesh() const
{
	return mesh;
}

Result<void> MeshFilterComponent::SetSharedMesh(MeshPtr value)
{
	if (value && !value->IsValid())
	{
		return Result<void>::Failure("MeshFilter cannot bind a destroyed Mesh");
	}
	mesh = std::move(value);
	instantiated = false;
	return {};
}

Result<void> MeshFilterComponent::SetMesh(MeshPtr value)
{
	auto result = SetSharedMesh(std::move(value));
	if (result)
	{
		instantiated = mesh && !mesh->AssetIdentity().guid;
	}
	return result;
}

Result<MeshPtr> MeshFilterComponent::Mesh()
{
	auto owner = GetGameObject();
	if (owner && owner->IsAssetTemplate())
	{
		return Result<MeshPtr>::Failure("Prefab asset MeshFilter cannot instantiate Mesh; use SharedMesh");
	}
	if (mesh && !mesh->IsValid())
	{
		return Result<MeshPtr>::Failure("MeshFilter Mesh has been destroyed");
	}
	if (!instantiated)
	{
		auto instance = mesh ? mesh->Clone() : Ursa::Mesh::Create();
		if (!instance)
		{
			return instance;
		}
		mesh = std::move(instance.value);
		instantiated = true;
	}
	return Result<MeshPtr>::Success(mesh);
}
} // namespace Ursa
