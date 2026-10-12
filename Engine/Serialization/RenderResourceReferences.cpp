#include <Serialization/RenderResourceReferences.hpp>
#include <Serialization/ResourceSession.hpp>

namespace Ursa
{
namespace
{
Result<void> WriteExternal(ArchiveWriter& writer, const String& field, AssetReference reference)
{
	auto object = writer.Object(field);
	if (!object)
	{
		return Result<void>::Failure(object.error);
	}
	auto result = object.value.Write("kind", String("asset"));
	return result ? WriteAssetReference(object.value, "asset", reference) : result;
}

Result<AssetReference> ReadExternal(const ArchiveReader& reader, const String& field)
{
	auto object = reader.Object(field);
	if (!object)
	{
		return Result<AssetReference>::Failure(object.error);
	}
	auto kind = object.value.Text("kind");
	if (!kind || kind.value != "asset")
	{
		return Result<AssetReference>::Failure("Resource binding is not an external asset");
	}
	return ReadAssetReference(object.value, "asset");
}

bool Local(const ArchiveReader& reader, const String& field)
{
	auto object = reader.Object(field);
	auto kind = object ? object.value.Text("kind") : Result<String>::Failure("Invalid resource reference");
	return kind && kind.value == "local";
}
} // namespace

Result<void> WriteMeshBinding(ArchiveWriter& writer, const String& field, const MeshPtr& mesh)
{
	if (!mesh)
	{
		return writer.Null(field);
	}
	if (!mesh->IsValid())
	{
		return Result<void>::Failure("Cannot save destroyed Mesh binding");
	}
	const auto reference = mesh->AssetIdentity();
	if (reference.guid)
	{
		return WriteExternal(writer, field, reference);
	}
	auto service = writer.Context() ? writer.Context()->Service<RenderResourceReferences>()
	                                : Result<RenderResourceReferences*>::Failure("No document resource context");
	return service ? service.value->WriteLocalMesh(writer, field, mesh) : Result<void>::Failure(service.error);
}

Result<MeshPtr> ReadMeshBinding(const ArchiveReader& reader, const String& field, SerializationContext& context)
{
	if (!reader.Contains(field))
	{
		return Result<MeshPtr>::Failure("Mesh binding field is missing");
	}
	if (reader.IsNull(field))
	{
		return Result<MeshPtr>::Success(nullptr);
	}
	if (Local(reader, field))
	{
		auto service = context.Service<RenderResourceReferences>();
		return service ? service.value->ReadLocalMesh(reader, field) : Result<MeshPtr>::Failure(service.error);
	}
	auto reference = ReadExternal(reader, field);
	if (!reference || !context.ResourceLoader())
	{
		return Result<MeshPtr>::Failure(reference ? "Mesh asset binding requires a ResourceSession" : reference.error);
	}
	return context.ResourceLoader()->LoadMesh(reference.value.guid, reference.value.localFileId);
}

Result<void> WriteMaterialBinding(ArchiveWriter& writer, const String& field,
                                  const std::shared_ptr<MaterialInstance>& material)
{
	if (!material)
	{
		return writer.Null(field);
	}
	if (!material->IsValid())
	{
		return Result<void>::Failure("Cannot save destroyed material binding");
	}
	auto reference = material->AssetIdentity();
	if (!reference.guid && writer.Context())
	{
		auto identified = writer.Context()->Identify(*material);
		if (identified)
		{
			reference = {identified.value, 0};
		}
	}
	if (reference.guid)
	{
		return WriteExternal(writer, field, reference);
	}
	auto service = writer.Context() ? writer.Context()->Service<RenderResourceReferences>()
	                                : Result<RenderResourceReferences*>::Failure("No document resource context");
	return service ? service.value->WriteLocalMaterial(writer, field, material) : Result<void>::Failure(service.error);
}

Result<std::shared_ptr<MaterialInstance>> ReadMaterialBinding(const ArchiveReader& reader, const String& field,
                                                              SerializationContext& context)
{
	if (!reader.Contains(field))
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure("Material binding field is missing");
	}
	if (reader.IsNull(field))
	{
		return Result<std::shared_ptr<MaterialInstance>>::Success(nullptr);
	}
	if (Local(reader, field))
	{
		auto service = context.Service<RenderResourceReferences>();
		return service ? service.value->ReadLocalMaterial(reader, field)
		               : Result<std::shared_ptr<MaterialInstance>>::Failure(service.error);
	}
	auto reference = ReadExternal(reader, field);
	if (!reference || !context.ResourceLoader())
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(
		    reference ? "Material asset binding requires a ResourceSession" : reference.error);
	}
	return context.ResourceLoader()->LoadMaterial(reference.value.guid, reference.value.localFileId);
}
} // namespace Ursa
