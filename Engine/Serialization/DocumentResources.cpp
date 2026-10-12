#include <Serialization/DocumentResources.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Core/StringInterop.hpp>

namespace Ursa
{
Result<void> DocumentResources::Merge(const DocumentResources& other)
{
	for (const auto& entry : other.meshes)
	{
		auto found = meshes.find(entry.first);
		if (materials.count(entry.first) || (found != meshes.end() && found->second != entry.second))
		{
			return Result<void>::Failure("Conflicting document Mesh identity " + entry.first);
		}
		meshes[entry.first] = entry.second;
	}
	for (const auto& entry : other.materials)
	{
		auto found = materials.find(entry.first);
		if (meshes.count(entry.first) || (found != materials.end() && found->second != entry.second))
		{
			return Result<void>::Failure("Conflicting document Material identity " + entry.first);
		}
		materials[entry.first] = entry.second;
	}
	return {};
}

namespace
{
Result<String> ReadId(const ArchiveReader& reader, const String& field)
{
	auto value = reader.Object(field);
	auto id = value ? value.value.Text("id") : Result<String>::Failure(value.error);
	if (!id || !ResourceId::Parse(id.value))
	{
		return Result<String>::Failure("Invalid document local resource ID");
	}
	return id;
}
} // namespace

Result<void> DocumentResources::WriteReference(ArchiveWriter& writer, const String& field, ResourceId id)
{
	auto reference = writer.Object(field);
	if (!reference)
	{
		return Result<void>::Failure(reference.error);
	}
	auto result = reference.value.Write("kind", String("local"));
	return result ? reference.value.Write("id", id.ToString()) : result;
}

Result<void> DocumentResources::WriteLocalMesh(ArchiveWriter& writer, const String& field, const MeshPtr& mesh)
{
	const auto id = mesh->LocalIdentity();
	const auto key = id.ToString();
	auto found = meshes.find(key);
	if (materials.count(key) || (found != meshes.end() && found->second != mesh))
	{
		return Result<void>::Failure("Duplicate local resource identity " + key);
	}
	meshes[key] = mesh;
	return WriteReference(writer, field, id);
}

Result<void> DocumentResources::WriteLocalMaterial(ArchiveWriter& writer, const String& field,
                                                   const std::shared_ptr<MaterialInstance>& material)
{
	const auto id = material->LocalIdentity();
	const auto key = id.ToString();
	auto found = materials.find(key);
	if (meshes.count(key) || (found != materials.end() && found->second != material))
	{
		return Result<void>::Failure("Duplicate local resource identity " + key);
	}
	materials[key] = material;
	return WriteReference(writer, field, id);
}

Result<MeshPtr> DocumentResources::ReadLocalMesh(const ArchiveReader& reader, const String& field)
{
	auto id = ReadId(reader, field);
	auto found = id ? meshes.find(id.value) : meshes.end();
	return found != meshes.end()
	           ? Result<MeshPtr>::Success(found->second)
	           : Result<MeshPtr>::Failure(id ? "Missing local Mesh or resource kind mismatch: " + id.value : id.error);
}

Result<std::shared_ptr<MaterialInstance>> DocumentResources::ReadLocalMaterial(const ArchiveReader& reader,
                                                                               const String& field)
{
	auto id = ReadId(reader, field);
	auto found = id ? materials.find(id.value) : materials.end();
	return found != materials.end()
	           ? Result<std::shared_ptr<MaterialInstance>>::Success(found->second)
	           : Result<std::shared_ptr<MaterialInstance>>::Failure(
	                 id ? "Missing local Material or resource kind mismatch: " + id.value : id.error);
}

Result<nlohmann::json> DocumentResources::Encode(SerializationContext& context) const
{
	nlohmann::json resources = nlohmann::json::array();
	const auto append = [&](const String& id, const Serializable& value) -> Result<void>
	{
		ArchiveWriter writer;
		writer.SetContext(&context);
		auto saved = value.Serialize(writer);
		if (!saved)
		{
			return saved;
		}
		auto data = ArchiveToJson(writer);
		if (!data)
		{
			return Result<void>::Failure(data.error);
		}
		resources.push_back({{"id", ToStdString(id)},
		                     {"type", ToStdString(value.SerializationType())},
		                     {"version", value.SerializationVersion()},
		                     {"data", std::move(data.value)}});
		return {};
	};
	for (const auto& entry : meshes)
	{
		auto result = append(entry.first, *entry.second);
		if (!result)
		{
			return Result<nlohmann::json>::Failure(result.error);
		}
	}
	for (const auto& entry : materials)
	{
		auto result = append(entry.first, *entry.second);
		if (!result)
		{
			return Result<nlohmann::json>::Failure(result.error);
		}
	}
	return Result<nlohmann::json>::Success(std::move(resources));
}

Result<void> DocumentResources::Decode(const nlohmann::json& document, SerializationContext& context,
                                       bool regenerateIdentities)
{
	if (!document.contains("localResources"))
	{
		return {};
	}
	if (!document.contains("localResourceVersion") || !document["localResourceVersion"].is_number_unsigned() ||
	    document["localResourceVersion"].get<UInt64>() != 1 || !document["localResources"].is_array() ||
	    document["localResources"].size() > 100000 || document["localResources"].dump().size() > Mesh::maxDataBytes)
	{
		return Result<void>::Failure("Unsupported or oversized document local resource table");
	}
	DocumentResources candidate;
	// Allocate all identities before restoring data; no partial graph is published on failure.
	for (const auto& item : document["localResources"])
	{
		if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || !item.contains("type") ||
		    !item["type"].is_string() || !item.contains("version") || !item["version"].is_number_unsigned() ||
		    !item.contains("data"))
		{
			return Result<void>::Failure("Malformed document local resource entry");
		}
		auto id = ResourceId::Parse(ToUrsaString(item["id"].get<std::string>()));
		if (!id)
		{
			return Result<void>::Failure(id.error);
		}
		const auto key = id.value.ToString();
		if (candidate.meshes.count(key) || candidate.materials.count(key))
		{
			return Result<void>::Failure("Duplicate local resource ID " + key);
		}
		const auto type = item["type"].get<std::string>();
		if (type == "ursa.Mesh" && item["version"].get<UInt64>() == 1)
		{
			auto mesh = Mesh::Create();
			if (!mesh)
			{
				return Result<void>::Failure(mesh.error);
			}
			candidate.meshes[key] = mesh.value;
		}
		else if (type == "ursa.Material" && item["version"].get<UInt64>() == 1)
		{
			candidate.materials[key] = MakeShared<MaterialInstance>(Memory::MemoryTag::Assets, MaterialType::Unlit);
		}
		else
		{
			return Result<void>::Failure("Unsupported local resource type/version " + ToUrsaString(type));
		}
	}
	for (const auto& item : document["localResources"])
	{
		const auto key = ToUrsaString(item["id"].get<std::string>());
		auto reader = ArchiveFromJson(item["data"]);
		if (!reader)
		{
			return Result<void>::Failure(reader.error);
		}
		auto mesh = candidate.meshes.find(key);
		Serializable& value = mesh != candidate.meshes.end() ? static_cast<Serializable&>(*mesh->second)
		                                                     : static_cast<Serializable&>(*candidate.materials.at(key));
		auto loaded = value.Deserialize(reader.value, context);
		if (!loaded)
		{
			return Result<void>::Failure("Cannot restore local resource " + key + ": " + loaded.error);
		}
		const auto id = regenerateIdentities ? ResourceId::Generate() : ResourceId::Parse(key).value;
		if (mesh != candidate.meshes.end())
		{
			loaded = mesh->second->BindLocalIdentity(id);
		}
		else
		{
			loaded = candidate.materials.at(key)->BindLocalIdentity(id);
		}
		if (!loaded)
		{
			return loaded;
		}
	}
	meshes = std::move(candidate.meshes);
	materials = std::move(candidate.materials);
	return {};
}

DocumentResourceScope::DocumentResourceScope(SerializationContext& context, DocumentResources& resources)
    : context(context)
{
	auto old = context.Service<RenderResourceReferences>();
	previous = old ? old.value : nullptr;
	context.Provide<RenderResourceReferences>(resources);
}

DocumentResourceScope::~DocumentResourceScope()
{
	if (previous)
	{
		context.Provide<RenderResourceReferences>(*previous);
	}
	else
	{
		context.RemoveService<RenderResourceReferences>();
	}
}
} // namespace Ursa
