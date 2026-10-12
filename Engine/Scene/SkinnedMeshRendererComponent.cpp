#include <Scene/SkinnedMeshRendererComponent.hpp>
#include <Serialization/RenderResourceReferences.hpp>

namespace Ursa
{
String SkinnedMeshRendererComponent::SerializationType() const
{
	return "ursa.SkinnedMeshRenderer";
}

UInt32 SkinnedMeshRendererComponent::SerializationVersion() const
{
	return 2;
}

MeshPtr SkinnedMeshRendererComponent::SharedMesh() const
{
	return mesh;
}

Result<void> SkinnedMeshRendererComponent::SetSharedMesh(MeshPtr value)
{
	if (value)
	{
		const auto data = value->Snapshot();
		if (!data || !data->data.skeleton || data->data.skinInfluences.size() != data->data.positions.size())
		{
			return Result<void>::Failure("SkinnedMeshRenderer requires a valid skinned Mesh");
		}
	}
	mesh = std::move(value);
	return {};
}

Result<void> SkinnedMeshRendererComponent::Serialize(ArchiveWriter& writer) const
{
	auto result = WriteMeshBinding(writer, "mesh", mesh);
	return result ? RendererComponent::Serialize(writer) : result;
}

Result<void> SkinnedMeshRendererComponent::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	auto loaded = ReadMeshBinding(reader, "mesh", context);
	if (!loaded)
	{
		return Result<void>::Failure(loaded.error);
	}
	if (loaded.value)
	{
		const auto snapshot = loaded.value->Snapshot();
		if (!snapshot || !snapshot->data.skeleton ||
		    snapshot->data.skinInfluences.size() != snapshot->data.positions.size())
		{
			return Result<void>::Failure("SkinnedMeshRenderer references an incompatible Mesh");
		}
	}
	auto result = RendererComponent::Deserialize(reader, context);
	if (result)
	{
		mesh = std::move(loaded.value);
	}
	return result;
}
} // namespace Ursa
