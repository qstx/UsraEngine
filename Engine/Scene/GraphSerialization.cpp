#include <Scene/Scene.hpp>
#include <Serialization/ArchiveFields.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/JsonArchiveInterop.hpp>

namespace Ursa
{
Result<void> GameObject::Serialize(ArchiveWriter& writer) const
{
	auto objectName = Name();
	auto transform = GetLocalTransform();
	auto active = IsEnabled();
	if (!objectName || !transform || !active)
	{
		return Result<void>::Failure("Invalid GameObject for serialization");
	}
	auto result = writer.Write("id", Id().ToString());
	if (result)
	{
		result = writer.Write("name", objectName.value);
	}
	if (result)
	{
		result = writer.Write("enabled", active.value);
	}
	auto local = writer.Object("localTransform");
	if (!result || !local)
	{
		return Result<void>::Failure(!result ? result.error : local.error);
	}
	const auto& value = transform.value;
	result = WriteNumbers(local.value, "position", {value.position.x, value.position.y, value.position.z});
	if (result)
	{
		result = WriteNumbers(local.value, "rotation",
		                      {value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w});
	}
	if (result)
	{
		result = WriteNumbers(local.value, "scale", {value.scale.x, value.scale.y, value.scale.z});
	}
	return result;
}

Result<void> GameObject::Deserialize(const ArchiveReader& reader, SerializationContext&)
{
	if (scene || !alive)
	{
		return Result<void>::Failure("GameObject must be detached to deserialize");
	}
	auto idText = reader.Text("id");
	auto objectName = reader.Text("name");
	auto active = reader.Boolean("enabled");
	auto local = reader.Object("localTransform");
	if (!idText || !objectName || !active || !local || objectName.value.size() > 256 ||
	    objectName.value.find('\0') != String::npos)
	{
		return Result<void>::Failure("Invalid GameObject archive fields");
	}
	auto loadedId = ResourceId::Parse(idText.value);
	auto position = ReadNumbers(local.value, "position", 3);
	auto rotation = ReadNumbers(local.value, "rotation", 4);
	auto scale = ReadNumbers(local.value, "scale", 3);
	if (!loadedId || !position || !rotation || !scale)
	{
		return Result<void>::Failure("Invalid GameObject transform or ID");
	}
	LocalTransform transform;
	transform.position = {position.value[0], position.value[1], position.value[2]};
	transform.rotation = {rotation.value[0], rotation.value[1], rotation.value[2], rotation.value[3]};
	transform.scale = {scale.value[0], scale.value[1], scale.value[2]};
	auto applied = SetLocalTransform(transform);
	if (!applied)
	{
		return applied;
	}
	id = loadedId.value;
	name = std::move(objectName.value);
	enabled = active.value;
	return {};
}

Result<void> Scene::Serialize(ArchiveWriter& writer) const
{
	if (!writer.Context())
	{
		return Result<void>::Failure("Scene serialization needs a context");
	}
	auto registry = writer.Context()->Service<ComponentRegistry>();
	if (!registry)
	{
		return Result<void>::Failure(registry.error);
	}
	auto resolver = [&](const std::shared_ptr<MaterialInstance>& material)
	{ return writer.Context()->Identify(*material); };
	auto document = EncodeSceneDocument("archive.ursascene", ResourceId::Generate(), *this, *registry.value, resolver);
	return document ? ArchiveWriteJson(writer, document.value) : Result<void>::Failure(document.error);
}

Result<void> Scene::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	if (inPhase)
	{
		return Result<void>::Failure("Cannot deserialize Scene during update");
	}
	auto* loadContext = dynamic_cast<ComponentLoadContext*>(&context);
	auto registry = context.Service<ComponentRegistry>();
	auto document = ArchiveToJson(reader);
	if (!loadContext || !registry || !document)
	{
		return Result<void>::Failure("Scene archive needs a registry and component load context");
	}
	auto loaded = DecodeSceneDocument("archive.ursascene", document.value, *registry.value, *loadContext);
	if (!loaded)
	{
		return Result<void>::Failure(loaded.error);
	}
	Clear();
	auto& candidate = *loaded.value.scene;
	objects.swap(candidate.objects);
	rootObjects.swap(candidate.rootObjects);
	freeObjectIndices.swap(candidate.freeObjectIndices);
	components.swap(candidate.components);
	freeComponentIndices.swap(candidate.freeComponentIndices);
	activeCamera = candidate.activeCamera;
	ambientColor = candidate.ambientColor;
	ambientIntensity = candidate.ambientIntensity;
	for (auto& slot : objects)
	{
		if (slot.object)
		{
			slot.object->scene = this;
		}
	}
	for (auto& slot : components)
	{
		if (slot.object)
		{
			slot.object->scene = this;
		}
	}
	return {};
}

Result<void> PrefabAsset::Serialize(ArchiveWriter& writer) const
{
	if (!writer.Context())
	{
		return Result<void>::Failure("Prefab serialization needs a context");
	}
	auto registry = writer.Context()->Service<ComponentRegistry>();
	if (!registry)
	{
		return Result<void>::Failure(registry.error);
	}
	auto resolver = [&](const std::shared_ptr<MaterialInstance>& material)
	{ return writer.Context()->Identify(*material); };
	auto document = EncodePrefabAssetDocument("archive.ursaprefab", *this, *registry.value, resolver);
	return document ? ArchiveWriteJson(writer, document.value) : Result<void>::Failure(document.error);
}

Result<void> PrefabAsset::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	auto* loadContext = dynamic_cast<ComponentLoadContext*>(&context);
	auto registry = context.Service<ComponentRegistry>();
	auto document = ArchiveToJson(reader);
	if (!loadContext || !registry || !document)
	{
		return Result<void>::Failure("Prefab archive needs a registry and component load context");
	}
	auto loaded = DecodePrefabAssetDocument("archive.ursaprefab", document.value, *registry.value, *loadContext);
	if (!loaded)
	{
		return Result<void>::Failure(loaded.error);
	}
	id = loaded.value.id;
	root = std::move(loaded.value.root);
	return {};
}
} // namespace Ursa
