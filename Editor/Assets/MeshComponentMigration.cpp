#include <Assets/MeshComponentMigration.hpp>
#include <Mesh/Primitive.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/PrefabMerge.hpp>
#include <Serialization/PrefabCapture.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <cmath>
#include <stdexcept>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

ResourceId Derived(const Json& id, UInt8 tag)
{
	auto value = Take(ResourceId::Parse(ToUrsaString(id.get<std::string>())));
	value.bytes[0] ^= 0xa7;
	value.bytes[1] ^= tag;
	value.bytes[15] ^= 0x5d;
	if (!value)
	{
		throw std::runtime_error("Migration derived an invalid ID");
	}
	return value;
}

Json External(AssetReference reference)
{
	return {{"kind", "asset"}, {"asset", AssetReferenceToJson(reference)}};
}

Json Local(ResourceId id)
{
	return {{"kind", "local"}, {"id", ToStdString(id.ToString())}};
}

Json TintedMaterial(const Json& component, ResourceSession& resources)
{
	const auto& data = component.at("data");
	const auto color = data.at("color");
	if (!color.is_array() || color.size() != 4)
	{
		throw std::runtime_error("Legacy color requires four components");
	}
	Color tint{color[0].get<Float32>(), color[1].get<Float32>(), color[2].get<Float32>(), color[3].get<Float32>()};
	if (!std::isfinite(tint.red) || !std::isfinite(tint.green) || !std::isfinite(tint.blue) ||
	    !std::isfinite(tint.alpha) || tint.red < 0 || tint.green < 0 || tint.blue < 0 || tint.alpha < 0 ||
	    tint.alpha > 1)
	{
		throw std::runtime_error("Legacy color is out of range");
	}
	std::shared_ptr<MaterialInstance> material;
	const auto binding = data.value("material", data.value("materialId", Json(nullptr)));
	if (!binding.is_null())
	{
		const auto reference = Take(AssetReferenceFromJson(binding, "Legacy material"));
		material = Take(resources.LoadMaterial(reference.guid, reference.localFileId))->Clone();
	}
	else
	{
		material = MakeShared<MaterialInstance>(Memory::MemoryTag::Assets, MaterialType::Unlit);
		// Legacy unassigned Box colors were specified in display space.
		tint.red = std::pow(tint.red, 2.2f);
		tint.green = std::pow(tint.green, 2.2f);
		tint.blue = std::pow(tint.blue, 2.2f);
	}
	const auto original = material->Parameters().baseColor;
	Done(material->SetBaseColor({original.red * tint.red, original.green * tint.green, original.blue * tint.blue,
	                             original.alpha * tint.alpha}));
	if (!material->IsShaderBacked())
	{
		Done(material->MigrateLegacyToShader(material->Type() == MaterialType::Unlit ? Shader::BuiltinUnlitShaderId()
		                                                                             : Shader::BuiltinPbrShaderId()));
	}
	ArchiveWriter writer;
	SerializationContext context(&resources);
	writer.SetContext(&context);
	Done(material->Serialize(writer));
	return {{"id", ToStdString(Derived(component["id"], 2).ToString())},
	        {"type", "ursa.Material"},
	        {"version", UInt32{1}},
	        {"data", Take(ArchiveToJson(writer))}};
}

Json ModelMaterial(const Json& reference, ResourceSession& resources)
{
	const auto mesh = Take(AssetReferenceFromJson(reference, "Legacy Mesh"));
	Take(resources.LoadMesh(mesh.guid, mesh.localFileId));
	const auto path = Take(resources.ResolveAsset(mesh.guid, 0));
	const auto bytes = Take(resources.Index().Filesystem().Read(path));
	const auto model = Json::from_cbor(bytes.begin(), bytes.end());
	const auto key = std::to_string(mesh.localFileId);
	if (!model.contains("meshMaterialBindings"))
	{
		throw std::runtime_error("Reimport models with the current importer before Mesh migration");
	}
	const auto binding = model.at("meshMaterialBindings").at(key);
	return binding.is_null() ? Json(nullptr) : External({mesh.guid, binding.get<UInt64>()});
}

void RewriteFieldTypes(Json& value)
{
	if (value.is_object())
	{
		for (auto& item : value.items())
		{
			if ((item.key() == "TypeName" || item.key() == "ElementTypeName") && item.value().is_string())
			{
				std::string text = item.value().get<std::string>();
				const std::string old = "Ursa.BoxRenderComponent";
				const auto position = text.find(old);
				if (position != std::string::npos)
				{
					text.replace(position, old.size(), "Ursa.MeshRendererComponent");
					item.value() = text;
				}
			}
			else if (item.key() == "scriptPayload" && item.value().is_string())
			{
				auto payload = Json::parse(item.value().get<std::string>());
				RewriteFieldTypes(payload);
				item.value() = payload.dump();
			}
			else
			{
				RewriteFieldTypes(item.value());
			}
		}
	}
	else if (value.is_array())
	{
		for (auto& item : value)
		{
			RewriteFieldTypes(item);
		}
	}
}
} // namespace

Result<Json> MeshComponentMigration::Convert(const Json& source, ResourceSession& resources,
                                             const Map<String, Json>& legacyPrefabs)
{
	try
	{
		Json document = source;
		if (!document.is_object() || !document.contains("gameObjects") || !document["gameObjects"].is_array())
		{
			return Result<Json>::Failure("Migration requires a GameObject Scene/Prefab document");
		}
		document["localResourceVersion"] = UInt32{1};
		if (!document.contains("localResources"))
		{
			document["localResources"] = Json::array();
		}
		Json additionalObjects = Json::array();
		for (auto& object : document["gameObjects"])
		{
			Json components = Json::array();
			Size rendererCount = 0;
			for (auto component : object.at("components"))
			{
				const auto type = component.at("type").get<std::string>();
				const bool legacyBox = type == "ursa.BoxRender";
				const bool legacyMesh =
				    (type == "ursa.MeshRenderer" || type == "ursa.SkinnedMeshRenderer") && component.at("version") == 1;
				const bool renderer = legacyBox || type == "ursa.MeshRenderer" || type == "ursa.SkinnedMeshRenderer";
				if (!legacyBox && !legacyMesh)
				{
					components.push_back(std::move(component));
					if (renderer)
					{
						++rendererCount;
					}
					continue;
				}
				Json target = Json::array();
				Json material;
				Json geometry;
				if (legacyBox)
				{
					auto record = TintedMaterial(component, resources);
					material = Local(Take(ResourceId::Parse(ToUrsaString(record["id"].get<std::string>()))));
					document["localResources"].push_back(std::move(record));
					geometry = External({BuiltinCubeMeshId(), 0});
				}
				else
				{
					geometry = External(Take(AssetReferenceFromJson(component["data"]["mesh"], "Legacy Mesh")));
					material = ModelMaterial(component["data"]["mesh"], resources);
				}
				if (type != "ursa.SkinnedMeshRenderer")
				{
					target.push_back({{"id", ToStdString(Derived(component["id"], 1).ToString())},
					                  {"type", "ursa.MeshFilter"},
					                  {"version", UInt32{1}},
					                  {"enabled", true},
					                  {"data", {{"mesh", geometry}, {"instantiated", false}}}});
				}
				component["type"] =
				    type == "ursa.SkinnedMeshRenderer" ? "ursa.SkinnedMeshRenderer" : "ursa.MeshRenderer";
				component["version"] = UInt32{2};
				component["data"] = {{"materials", Json::array({{{"value", material}, {"instantiated", false}}})}};
				if (type == "ursa.SkinnedMeshRenderer")
				{
					component["data"]["mesh"] = geometry;
				}
				target.push_back(component);
				if (rendererCount++ == 0)
				{
					for (auto& item : target)
					{
						components.push_back(std::move(item));
					}
				}
				else
				{
					additionalObjects.push_back(
					    {{"id", ToStdString(Derived(component["id"], 3).ToString())},
					     {"parentId", object["id"]},
					     {"name", object.value("name", std::string("Object")).substr(0, 200) + "/Renderer-" +
					                  component["id"].get<std::string>().substr(0, 8)},
					     {"enabled", true},
					     {"localTransform",
					      {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}},
					     {"components", std::move(target)}});
				}
			}
			object["components"] = std::move(components);
		}
		for (auto& object : additionalObjects)
		{
			document["gameObjects"].push_back(std::move(object));
		}
		if (document.contains("instances"))
		{
			for (auto& instance : document["instances"])
			{
				const auto reference = Take(AssetReferenceFromJson(instance["prefab"], "Prefab migration"));
				auto prefab = legacyPrefabs.find(reference.guid.ToString());
				if (prefab == legacyPrefabs.end())
				{
					continue;
				}
				const auto overrides = instance["overrides"];
				Json effective = prefab->second;
				effective["gameObjects"] =
				    Take(MergePrefabNodes(prefab->second["gameObjects"], overrides, "Legacy Prefab migration"));
				if (overrides.contains("properties"))
				{
					for (auto& object : effective["gameObjects"])
					{
						const auto apply = [&](Json& record)
						{
							const auto id = record["id"].get<std::string>();
							if (!overrides["properties"].contains(id))
							{
								return;
							}
							for (const auto& property : overrides["properties"][id].items())
							{
								const Json::json_pointer pointer(property.key());
								if (!record.contains(pointer))
								{
									throw std::runtime_error("Missing legacy Prefab property target " + property.key());
								}
								record[pointer] = property.value();
							}
						};
						apply(object);
						for (auto& component : object["components"])
						{
							apply(component);
						}
					}
				}
				const auto convertedSource = Take(Convert(prefab->second, resources));
				const auto convertedEffective = Take(Convert(effective, resources));
				instance["overrides"] = Take(CapturePrefabDocumentChanges(convertedSource, convertedEffective));
			}
		}
		RewriteFieldTypes(document);
		return Result<Json>::Success(std::move(document));
	}
	catch (const std::exception& error)
	{
		return Result<Json>::Failure(String("Cannot migrate Mesh components: ") + error.what());
	}
}

Result<MeshMigrationReport> MeshComponentMigration::Migrate(VirtualFileSystem& files, ResourceSession& resources,
                                                            bool dryRun)
{
	try
	{
		struct Rewrite
		{
			VirtualPath path;
			Vector<UInt8> before;
			Vector<UInt8> after;
		};

		Vector<Rewrite> rewrites;
		Map<String, Json> documents;
		Map<String, Json> prefabs;
		const auto entries = Take(files.List(projectScheme));
		for (const auto& entry : entries)
		{
			auto kind = ResourceKindForPath(entry.path);
			if (!kind || (kind.value != ResourceKind::Scene && kind.value != ResourceKind::Prefab))
			{
				continue;
			}
			auto document = Take(ReadJsonResource(files, entry.path, kind.value));
			if (kind.value == ResourceKind::Prefab)
			{
				prefabs[ToUrsaString(document["id"].get<std::string>())] = document;
			}
			documents[entry.path.ToString()] = std::move(document);
		}
		auto provider = std::make_shared<MemoryFileProvider>();
		VirtualFileSystem staging;
		Done(staging.MountProvider(projectScheme, provider, MountAccess::ReadWrite));
		for (const auto& entry : documents)
		{
			auto converted = Take(Convert(entry.second, resources, prefabs));
			if (converted == entry.second)
			{
				continue;
			}
			const auto path = Take(VirtualPath::Parse(entry.first));
			Done(files.CheckWritable(path));
			const auto text = converted.dump(2);
			Vector<UInt8> bytes(text.data(), text.data() + text.size());
			Done(staging.WriteAtomic(path, bytes));
			rewrites.push_back({path, Take(files.Read(path)), std::move(bytes)});
		}
		for (const auto& mount : files.Providers())
		{
			Done(staging.MountProvider(mount.scheme, mount.provider, mount.access));
		}
		auto resolver = [&](ResourceId id, UInt64 localId) -> Result<VirtualPath>
		{
			if (!localId)
			{
				for (const auto& document : documents)
				{
					if (document.second["id"] == ToStdString(id.ToString()))
					{
						return VirtualPath::Parse(document.first);
					}
				}
			}
			return resources.ResolveAsset(id, localId);
		};
		auto candidate = Take(ResourceSession::Open(staging, resolver));
		ComponentRegistry registry;
		Done(RegisterBuiltinComponents(registry));
		for (const auto& rewrite : rewrites)
		{
			ComponentLoadContext context(*candidate);
			const auto kind = Take(ResourceKindForPath(rewrite.path));
			if (kind == ResourceKind::Scene)
			{
				Take(LoadLinkedSceneResource(staging, rewrite.path, registry, context));
			}
			else
			{
				Take(LoadPrefabAssetResource(staging, rewrite.path, registry, context));
			}
		}
		MeshMigrationReport report{UInt32(rewrites.size()), {}};
		if (dryRun || rewrites.empty())
		{
			return Result<MeshMigrationReport>::Success(std::move(report));
		}
		report.backupRoot = "lib:MeshMigration/" + ResourceId::Generate().ToString();
		for (const auto& rewrite : rewrites)
		{
			Done(files.WriteAtomic(Take(VirtualPath::Parse(report.backupRoot + "/" + rewrite.path.relative)),
			                       rewrite.before));
		}
		Size written = 0;
		for (const auto& rewrite : rewrites)
		{
			auto result = files.WriteAtomic(rewrite.path, rewrite.after);
			if (!result)
			{
				for (Size i = 0; i < written; ++i)
				{
					files.WriteAtomic(rewrites[i].path, rewrites[i].before);
				}
				return Result<MeshMigrationReport>::Failure(result.error +
				                                            "; previous files restored, backup: " + report.backupRoot);
			}
			++written;
		}
		return Result<MeshMigrationReport>::Success(std::move(report));
	}
	catch (const std::exception& error)
	{
		return Result<MeshMigrationReport>::Failure(String("Cannot migrate project Mesh documents: ") + error.what());
	}
}
} // namespace Ursa::Editor
