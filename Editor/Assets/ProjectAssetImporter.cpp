#include <Assets/ProjectAssetImporter.hpp>
#include <Assets/AssetImportRules.hpp>
#include <Core/Profiler.hpp>
#include <Assets/ShaderAssetImporter.hpp>
#include <Assets/TextureImporter.hpp>
#include <Assets/ModelImporter.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <Vfs/BuildResourceManifest.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ResourceFile.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Assets/ScriptBuildService.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>

namespace Ursa::Editor
{
namespace
{
class ImportSourceSnapshot final : public VirtualFileProvider
{
public:
	ImportSourceSnapshot(std::shared_ptr<MemoryFileProvider> contents, String primaryMetadata)
	    : memory(std::move(contents)), metadataPath(std::move(primaryMetadata))
	{
	}

	String Identity() const override
	{
		return "import-source:" + memory->Identity();
	}

	String SourceLabel() const override
	{
		return "import-source";
	}

	Result<bool> Exists(const String& path) const override
	{
		return memory->Exists(path);
	}

	Result<UInt64> Size(const String& path) const override
	{
		return memory->Size(path);
	}

	Result<Vector<UInt8>> ReadRange(const String& path, UInt64 offset, UInt64 size) const override
	{
		return memory->ReadRange(path, offset, size);
	}

	Result<Vector<String>> List() const override
	{
		return memory->List();
	}

	Result<void> CheckWritable(const String& path) const override
	{
		return path == metadataPath ? Result<void>{} : Result<void>::Failure("Import source snapshot is read-only");
	}

	Result<void> WriteAtomic(const String& path, const Vector<UInt8>& bytes, bool createDirectories) override
	{
		auto writable = CheckWritable(path);
		return writable ? memory->WriteAtomic(path, bytes, createDirectories) : writable;
	}

	Result<void> Append(const String&, const Vector<UInt8>&, bool) override
	{
		return Result<void>::Failure("Import source snapshot does not support append");
	}

private:
	std::shared_ptr<MemoryFileProvider> memory;
	String metadataPath;
};

String ClrFieldTypeName(const String& name)
{
	static const Map<String, String> aliases{
	    {"bool", "System.Boolean"}, {"sbyte", "System.SByte"},   {"byte", "System.Byte"},
	    {"short", "System.Int16"},  {"ushort", "System.UInt16"}, {"int", "System.Int32"},
	    {"uint", "System.UInt32"},  {"long", "System.Int64"},    {"ulong", "System.UInt64"},
	    {"float", "System.Single"}, {"double", "System.Double"}, {"string", "System.String"}};
	const auto alias = aliases.find(name);
	return alias == aliases.end() ? name : alias->second;
}

String Extension(const VirtualPath& path)
{
	String extension = PathToUrsaString(PathFromUrsaString(path.relative).extension());
	std::transform(extension.begin(), extension.end(), extension.begin(), [](char value)
	               { return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value; });
	return extension;
}

bool HasUnsignedRange(const nlohmann::json& value, Ursa::UInt64 maximum)
{
	if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<Int64>() >= 0))
	{
		return false;
	}
	return value.get<Ursa::UInt64>() <= maximum;
}

bool ScriptFieldValueMatches(const nlohmann::json& value, const String& kind, const String& typeName)
{
	const bool referenceKind = kind == "GameObjectReference" || kind == "ComponentReference" ||
	                           kind == "ScriptableObjectReference" || kind == "MeshReference" ||
	                           kind == "MaterialReference" || kind == "TextureReference" || kind == "ShaderReference" ||
	                           kind == "ComputeShaderReference";
	if (value.is_null())
	{
		return kind == "String" || referenceKind;
	}
	if (kind == "Array" || kind == "List")
	{
		if (!value.is_array())
		{
			return false;
		}
		return std::all_of(value.begin(), value.end(),
		                   [&](const nlohmann::json& item) { return ScriptFieldValueMatches(item, typeName, kind); });
	}
	if (kind == "Boolean")
	{
		return value.is_boolean();
	}
	if (kind == "String")
	{
		return value.is_string();
	}
	if (kind == "FloatingPoint")
	{
		return value.is_number() && std::isfinite(value.get<Float64>());
	}
	if (kind == "SignedInteger")
	{
		if (!value.is_number_integer())
		{
			return false;
		}
		const Int64 number = value.get<Int64>();
		if (typeName == "System.SByte")
		{
			return number >= (std::numeric_limits<Int8>::min)() && number <= (std::numeric_limits<Int8>::max)();
		}
		if (typeName == "System.Int16")
		{
			return number >= (std::numeric_limits<Int16>::min)() && number <= (std::numeric_limits<Int16>::max)();
		}
		if (typeName == "System.Int32")
		{
			return number >= (std::numeric_limits<Int32>::min)() && number <= (std::numeric_limits<Int32>::max)();
		}
		return true;
	}
	if (kind == "UnsignedInteger")
	{
		if (typeName == "System.Byte")
		{
			return HasUnsignedRange(value, (std::numeric_limits<UInt8>::max)());
		}
		if (typeName == "System.UInt16")
		{
			return HasUnsignedRange(value, (std::numeric_limits<UInt16>::max)());
		}
		if (typeName == "System.UInt32")
		{
			return HasUnsignedRange(value, (std::numeric_limits<UInt32>::max)());
		}
		return HasUnsignedRange(value, (std::numeric_limits<UInt64>::max)());
	}
	if (kind == "Enumeration")
	{
		return value.is_number_integer() || value.is_number_unsigned();
	}
	if (kind == "Vector2")
	{
		return value.is_array() && value.size() == 2 &&
		       std::all_of(value.begin(), value.end(), [](const auto& item) { return item.is_number(); });
	}
	if (kind == "Vector3")
	{
		return value.is_array() && value.size() == 3 &&
		       std::all_of(value.begin(), value.end(), [](const auto& item) { return item.is_number(); });
	}
	if (kind == "Vector4" || kind == "Quaternion" || kind == "Color")
	{
		return value.is_array() && value.size() == 4 &&
		       std::all_of(value.begin(), value.end(), [](const auto& item) { return item.is_number(); });
	}
	if (kind == "AssetReference")
	{
		if (!value.is_object() || !value.contains("guid") || !value["guid"].is_string() ||
		    !value.contains("localFileId") ||
		    !HasUnsignedRange(value["localFileId"], (std::numeric_limits<UInt64>::max)()))
		{
			return false;
		}
		auto guid = ResourceId::Parse(ToUrsaString(value["guid"].get<std::string>()));
		return guid && bool(guid.value);
	}
	if (referenceKind)
	{
		if (!value.is_object() || !value.contains("$ursaReference") || !value["$ursaReference"].is_object())
		{
			return false;
		}
		const auto& reference = value["$ursaReference"];
		const String expected = kind == "GameObjectReference"         ? "GameObject"
		                        : kind == "ComponentReference"        ? "Component"
		                        : kind == "ScriptableObjectReference" ? "ScriptableObject"
		                        : kind == "MeshReference"             ? "Mesh"
		                        : kind == "MaterialReference"         ? "Material"
		                        : kind == "TextureReference"          ? "Texture"
		                        : kind == "ShaderReference"           ? "Shader"
		                        : kind == "ComputeShaderReference"    ? "ComputeShader"
		                                                              : String{};
		if (expected.empty() || !reference.contains("Kind") || !reference["Kind"].is_string() ||
		    reference["Kind"].get<std::string>() != ToStdString(expected))
		{
			return false;
		}
		if (expected == "GameObject" || expected == "Component")
		{
			return reference.contains("Id") && reference["Id"].is_string() &&
			       reference["Id"].get<std::string>().empty();
		}
		if (!reference.contains("Resource") || !reference["Resource"].is_object())
		{
			return false;
		}
		const nlohmann::json* resource = &reference["Resource"];
		if (resource->contains("kind"))
		{
			if ((expected != "Material" && expected != "Mesh") || resource->at("kind") != "asset" ||
			    !resource->contains("asset") || !resource->at("asset").is_object())
			{
				return false;
			}
			resource = &resource->at("asset");
		}
		if (!resource->contains("guid") || !resource->at("guid").is_string() ||
		    !ResourceId::Parse(ToUrsaString(resource->at("guid").get<std::string>())))
		{
			return false;
		}
		return resource->contains("localFileId") &&
		       HasUnsignedRange(resource->at("localFileId"), (std::numeric_limits<UInt64>::max)());
	}
	return false;
}

String ScriptTypeFieldSchemaFingerprint(const ScriptTypeDescriptor& type)
{
	nlohmann::json fields = nlohmann::json::array();
	for (const ScriptFieldTypeDescriptor& field : type.fields)
	{
		nlohmann::json enumValues = nlohmann::json::array();
		for (const String& value : field.enumValues)
		{
			enumValues.push_back(ToStdString(value));
		}
		nlohmann::json enumNumericValues = nlohmann::json::array();
		for (const String& value : field.enumNumericValuesJson)
		{
			enumNumericValues.push_back(nlohmann::json::parse(ToStdString(value)));
		}
		nlohmann::json formerNames = nlohmann::json::array();
		for (const String& value : field.formerNames)
		{
			formerNames.push_back(ToStdString(value));
		}
		fields.push_back({{"Name", ToStdString(field.name)},
		                  {"Kind", ToStdString(field.kind)},
		                  {"ElementKind", ToStdString(field.elementKind)},
		                  {"TypeName", ToStdString(field.typeName)},
		                  {"ElementTypeName", ToStdString(field.elementTypeName)},
		                  {"DefaultValueJson", ToStdString(field.defaultValueJson)},
		                  {"EnumUnderlyingType", ToStdString(field.enumUnderlyingType)},
		                  {"EnumValues", std::move(enumValues)},
		                  {"EnumNumericValues", std::move(enumNumericValues)},
		                  {"FormerNames", std::move(formerNames)}});
	}
	nlohmann::json baseTypes = nlohmann::json::array();
	for (const String& typeName : type.baseTypeNames)
	{
		baseTypes.push_back(ToStdString(typeName));
	}
	const String encoded = ToUrsaString(nlohmann::json{
	    {"ScriptType", ToStdString(type.scriptType)},
	    {"Version", type.version},
	    {"BaseTypeNames", std::move(baseTypes)},
	    {"Fields", std::move(fields)}}.dump());
	return BuildResourceFingerprint(Vector<UInt8>(encoded.begin(), encoded.end()));
}

Result<AssetImportResult> ImportScriptableAsset(AssetDatabase& database, const VirtualPath& source,
                                                const Vector<ScriptTypeRecord>& types, bool force)
{
	auto metadata = database.Metadata(source);
	if (!metadata || metadata.value.folder)
	{
		return Result<AssetImportResult>::Failure(metadata ? "Cannot import a ScriptableAsset folder" : metadata.error);
	}
	auto asset = LoadScriptableAssetSource(database.Filesystem(), source, metadata.value.guid);
	if (!asset)
	{
		return Result<AssetImportResult>::Failure(asset.error);
	}
	const auto type = std::find_if(types.begin(), types.end(), [&](const ScriptTypeRecord& record)
	                               { return record.scriptType == asset.value.TypeId(); });
	if (type == types.end() || type->kind != ScriptTypeKind::ScriptableObject ||
	    type->group != ScriptAssemblyGroup::Game || type->version != asset.value.TypeVersion() ||
	    ToStdString(type->sourceGuid.ToString()) != asset.value.Payload().value("SourceGuid", std::string{}) ||
	    type->localFileId != asset.value.Payload().value("LocalFileId", UInt64{0}))
	{
		return Result<AssetImportResult>::Failure(
		    "ScriptableAsset type/version does not match the current Game type catalog: " + asset.value.TypeId());
	}
	for (const auto& descriptor : type->fields)
	{
		if (descriptor.kind == "Unsupported" ||
		    ((descriptor.kind == "Array" || descriptor.kind == "List") && descriptor.elementKind == "Unsupported"))
		{
			return Result<AssetImportResult>::Failure(
			    "ScriptableAsset type has an unsupported field: " + type->clrTypeName + "." + descriptor.name);
		}
	}
	const auto& payload = asset.value.Payload();
	if (!payload.contains("FieldTypes") || !payload["FieldTypes"].is_object())
	{
		return Result<AssetImportResult>::Failure("ScriptableAsset is missing its versioned field type schema: " +
		                                          source.ToString());
	}
	for (const auto& descriptor : type->fields)
	{
		String fieldName = descriptor.name;
		if (!payload["Fields"].contains(ToStdString(fieldName)))
		{
			const auto alias =
			    std::find_if(descriptor.formerNames.begin(), descriptor.formerNames.end(),
			                 [&](const String& name) { return payload["Fields"].contains(ToStdString(name)); });
			if (alias == descriptor.formerNames.end())
			{
				continue;
			}
			fieldName = *alias;
		}
		const auto fieldType = payload["FieldTypes"].find(ToStdString(fieldName));
		const bool collection = descriptor.kind == "Array" || descriptor.kind == "List";
		const String valueKind = collection ? descriptor.elementKind : descriptor.kind;
		const bool reference = valueKind == "GameObjectReference" || valueKind == "ComponentReference" ||
		                       valueKind == "ScriptableObjectReference" || valueKind == "MaterialReference" ||
		                       valueKind == "TextureReference" || valueKind == "MeshReference" ||
		                       valueKind == "ShaderReference" || valueKind == "ComputeShaderReference";
		if (fieldType == payload["FieldTypes"].end() || !fieldType->is_object() ||
		    fieldType->value("Kind", std::string{}) != ToStdString(descriptor.kind) ||
		    fieldType->value("ElementKind", std::string{}) != ToStdString(descriptor.elementKind) ||
		    (!reference && ClrFieldTypeName(ToUrsaString(
		                       fieldType->value(collection ? "ElementTypeName" : "TypeName", std::string{}))) !=
		                       ClrFieldTypeName(collection ? descriptor.elementTypeName : descriptor.typeName)))
		{
			return Result<AssetImportResult>::Failure("ScriptableAsset field schema does not match its current type: " +
			                                          source.ToString() + "." + descriptor.name);
		}
		const nlohmann::json& fieldValue = payload["Fields"].at(ToStdString(fieldName));
		const String payloadType = ClrFieldTypeName(
		    ToUrsaString(fieldType->value(collection ? "ElementTypeName" : "TypeName", std::string{})));
		const auto validValue = [&](const nlohmann::json& value)
		{ return ScriptFieldValueMatches(value, valueKind, payloadType); };
		const bool valuesMatch =
		    collection ? fieldValue.is_null() ||
		                     (fieldValue.is_array() && std::all_of(fieldValue.begin(), fieldValue.end(), validValue))
		               : validValue(fieldValue);
		if (!valuesMatch)
		{
			return Result<AssetImportResult>::Failure("ScriptableAsset field value does not match its declared type: " +
			                                          source.ToString() + "." + descriptor.name);
		}
		if (valueKind == "Enumeration")
		{
			const auto declared = [&](const nlohmann::json& value)
			{
				return std::any_of(descriptor.enumNumericValuesJson.begin(), descriptor.enumNumericValuesJson.end(),
				                   [&](const String& numeric)
				                   { return nlohmann::json::parse(ToStdString(numeric)) == value; });
			};
			const bool declaredValues =
			    collection ? fieldValue.is_null() || std::all_of(fieldValue.begin(), fieldValue.end(), declared)
			               : declared(fieldValue);
			if (!declaredValues)
			{
				return Result<AssetImportResult>::Failure("ScriptableAsset enum value is not declared by its type: " +
				                                          source.ToString() + "." + descriptor.name);
			}
		}
	}
	auto references = ScriptableAssetDependencies(asset.value);
	if (!references)
	{
		return Result<AssetImportResult>::Failure(references.error);
	}
	Vector<VirtualPath> dependencies;
	if (type->sourcePath.scheme != "framework")
	{
		dependencies.push_back(type->sourcePath);
		auto typeMetadata = VirtualPath::Parse(type->sourcePath.ToString() + ".meta");
		if (typeMetadata && typeMetadata.value != source)
		{
			auto exists = database.Filesystem().Exists(typeMetadata.value);
			if (exists && exists.value)
			{
				dependencies.push_back(typeMetadata.value);
			}
		}
	}
	for (AssetReference reference : references.value)
	{
		if (reference.guid == metadata.value.guid)
		{
			continue;
		}
		// Import dependencies point to source files. Local file IDs are resolved after import by the runtime catalog.
		auto path = database.SourcePath(reference.guid);
		if (!path)
		{
			return Result<AssetImportResult>::Failure("Missing ScriptableAsset dependency from " + source.ToString() +
			                                          ": " + path.error);
		}
		if (std::find(dependencies.begin(), dependencies.end(), path.value) == dependencies.end())
		{
			dependencies.push_back(std::move(path.value));
		}
	}
	const nlohmann::json settings{{"typeId", ToStdString(type->scriptType)},
	                              {"typeVersion", type->version},
	                              {"sourceTypeGuid", ToStdString(type->sourceGuid.ToString())},
	                              {"sourceTypeLocalFileId", type->localFileId},
	                              {"typeFieldSchemaFingerprint", ToStdString(ScriptTypeFieldSchemaFingerprint(*type))}};
	auto cached =
	    database.CheckImport(source, "ursa.ScriptableAsset", 1, "scriptable-asset-v1", settings, dependencies);
	if (!cached)
	{
		return Result<AssetImportResult>::Failure(cached.error);
	}
	if (!force && cached.value.upToDate && cached.value.mainObjectLocalFileId == asset.value.LocalFileId())
	{
		return Result<AssetImportResult>::Success({metadata.value.guid, cached.value.artifacts, dependencies, true});
	}
	auto artifactBytes = EncodeScriptableAssetArtifact(asset.value);
	if (!artifactBytes)
	{
		return Result<AssetImportResult>::Failure(artifactBytes.error);
	}
	ImportedArtifact artifact{asset.value.LocalFileId(), "ScriptableAsset", std::move(artifactBytes.value)};
	auto published = database.PublishImport(source, "ursa.ScriptableAsset", 1, "scriptable-asset-v1", settings,
	                                        dependencies, {std::move(artifact)}, asset.value.LocalFileId());
	if (!published)
	{
		return Result<AssetImportResult>::Failure(published.error);
	}
	auto result =
	    database.CheckImport(source, "ursa.ScriptableAsset", 1, "scriptable-asset-v1", settings, dependencies);
	return result
	           ? Result<AssetImportResult>::Success({metadata.value.guid, result.value.artifacts, dependencies, false})
	           : Result<AssetImportResult>::Failure(result.error);
}

Result<AssetImportResult> DefaultImport(AssetDatabase& database, const VirtualPath& source, bool force)
{
	auto metadata = database.Metadata(source);
	if (!metadata)
	{
		return Result<AssetImportResult>::Failure(metadata.error);
	}
	String kind = "Default";
	auto resourceKind = ResourceKindForPath(source);
	if (resourceKind)
	{
		auto header = ReadResourceHeader(database.Filesystem(), source);
		if (!header)
		{
			return Result<AssetImportResult>::Failure(header.error);
		}
		const char* names[]{"Texture", "Material", "Scene", "Prefab", "Shader", "Mesh"};
		kind = names[static_cast<int>(resourceKind.value)];
	}
	const nlohmann::json settings = metadata.value.importSettings;
	const String target =
	    kind == "Material" ? "native-v1/material-migration-v" + ToUrsaString(std::to_string(materialMigrationVersion))
	                       : "native-v1";
	auto cached = database.CheckImport(source, "ursa.Default", 1, target, settings, {});
	if (!cached)
	{
		return Result<AssetImportResult>::Failure(cached.error);
	}
	if (!force && cached.value.upToDate)
	{
		return Result<AssetImportResult>::Success({metadata.value.guid, cached.value.artifacts, {}, true});
	}
	auto bytes = database.Filesystem().Read(source);
	if (!bytes)
	{
		return Result<AssetImportResult>::Failure(bytes.error);
	}
	auto published = database.PublishImport(source, "ursa.Default", 1, target, settings, {}, {{0, kind, bytes.value}});
	if (!published)
	{
		return Result<AssetImportResult>::Failure(published.error);
	}
	auto status = database.CheckImport(source, "ursa.Default", 1, target, settings, {});
	return status ? Result<AssetImportResult>::Success({metadata.value.guid, status.value.artifacts, {}, false})
	              : Result<AssetImportResult>::Failure(status.error);
}
} // namespace

ProjectAssetImporter::ProjectAssetImporter()
{
	auto initial = std::make_shared<Registry>();
	auto& importers = initial->importers;
	importers[".ursaasset"] = [](AssetDatabase& database, const VirtualPath& source, bool force)
	{ return ImportScriptableAsset(database, source, {}, force); };
	const ImportFunction shader = [](AssetDatabase& database, const VirtualPath& source, bool force)
	{
		auto imported = ShaderAssetImporter::Import(database, source, DefaultShaderCompilerTools(), force);
		if (!imported)
		{
			return Result<AssetImportResult>::Failure(imported.error);
		}
		auto metadata = database.Metadata(source);
		if (!metadata)
		{
			return Result<AssetImportResult>::Failure(metadata.error);
		}
		ImportedArtifactLocation artifact{0, "Shader", imported.value.artifactPath};
		return Result<AssetImportResult>::Success(
		    {metadata.value.guid, {artifact}, imported.value.dependencies, imported.value.cacheHit});
	};
	importers[".ursashader"] = shader;
	importers[".compute"] = shader;
	const ImportFunction texture = [](AssetDatabase& database, const VirtualPath& source, bool force)
	{
		auto settings = TextureImporter::Settings(database, source);
		if (!settings)
		{
			return Result<AssetImportResult>::Failure(settings.error);
		}
		auto imported = TextureImporter::Import(database, source, settings.value, "texture-rgba8", force);
		if (!imported)
		{
			return Result<AssetImportResult>::Failure(imported.error);
		}
		ImportedArtifactLocation artifact{0, "Texture", imported.value.artifactPath};
		return Result<AssetImportResult>::Success({imported.value.guid, {artifact}, {}, imported.value.cacheHit});
	};
	for (const String extension : {String(".png"), String(".jpg"), String(".jpeg")})
	{
		importers[extension] = texture;
	}
	const ImportFunction model = [](AssetDatabase& database, const VirtualPath& source, bool force)
	{
		auto settings = ModelImporter::Settings(database, source);
		if (!settings)
		{
			return Result<AssetImportResult>::Failure(settings.error);
		}
		auto imported = ModelImporter::Import(database, source, settings.value, force);
		return imported ? Result<AssetImportResult>::Success({imported.value.guid, imported.value.subAssets,
		                                                      imported.value.dependencies, imported.value.cacheHit,
		                                                      imported.value.diagnostics})
		                : Result<AssetImportResult>::Failure(imported.error);
	};
	for (const auto& extension : ModelImporter::SupportedExtensions())
	{
		importers[extension] = model;
	}
	initial->nativeImporters = importers;
	registrations = std::move(initial);
}

Result<void> ProjectAssetImporter::ReplaceManagedRules(const ProjectAssetImporter& prepared, UInt64 token)
{
	const auto next = std::atomic_load(&prepared.registrations);
	for (;;)
	{
		auto previous = std::atomic_load(&registrations);
		auto updated = std::make_shared<Registry>(*previous);
		for (const auto& old : updated->scriptedIdentities)
		{
			updated->importers.erase(old.first);
		}
		for (const auto& extension : updated->processedExtensions)
		{
			updated->importers[extension] = updated->nativeImporters.at(extension);
		}
		for (const auto& entry : next->scriptedIdentities)
		{
			if (updated->importers.count(entry.first))
			{
				return Result<void>::Failure("Managed rule conflicts with an existing project importer: " +
				                             entry.first);
			}
			updated->importers.emplace(entry.first, next->importers.at(entry.first));
		}
		for (const auto& extension : next->processedExtensions)
		{
			updated->importers[extension] = next->importers.at(extension);
		}
		updated->scriptedIdentities = next->scriptedIdentities;
		updated->scriptedVersions = next->scriptedVersions;
		updated->processedExtensions = next->processedExtensions;
		updated->importers[".ursaasset"] = next->importers.at(".ursaasset");
		updated->managedRulesToken = token;
		updated->generation = previous->generation + 1;
		if (!updated->generation)
		{
			return Result<void>::Failure("Import rule generation exhausted");
		}
		std::shared_ptr<const Registry> replacement = std::move(updated);
		if (std::atomic_compare_exchange_weak(&registrations, &previous, replacement))
		{
			return {};
		}
	}
}

Result<void> ProjectAssetImporter::RegisterScriptableAssets(Vector<Ursa::ScriptTypeDescriptor> types)
{
	const ImportFunction import =
	    [types = std::move(types)](AssetDatabase& database, const VirtualPath& source, bool force)
	{ return ImportScriptableAsset(database, source, types, force); };
	for (;;)
	{
		auto previous = std::atomic_load(&registrations);
		auto updated = std::make_shared<Registry>(*previous);
		updated->importers[".ursaasset"] = import;
		++updated->generation;
		std::shared_ptr<const Registry> replacement = std::move(updated);
		if (std::atomic_compare_exchange_weak(&registrations, &previous, replacement))
		{
			return {};
		}
	}
}

Result<void> ProjectAssetImporter::Register(const String& extension, ImportFunction importer)
{
	return Register(extension, std::move(importer), {});
}

Result<void> ProjectAssetImporter::RegisterScripted(const String& extension, const String& identity, UInt32 version,
                                                    ImportFunction importer)
{
	const auto parsed = ImportScriptType::FromIdentity(identity);
	if (!parsed || !version)
	{
		return Result<void>::Failure(parsed ? "ScriptedImporter version must be nonzero" : parsed.error);
	}
	return Register(extension, std::move(importer), identity, version);
}

Result<void> ProjectAssetImporter::Register(const String& extension, ImportFunction importer,
                                            const String& scriptedIdentity, UInt32 version)
{
	if (extension.empty() || extension.front() != '.' || !importer)
	{
		return Result<void>::Failure("Invalid or duplicate asset importer extension: " + extension);
	}
	for (;;)
	{
		auto previous = std::atomic_load(&registrations);
		auto updated = std::make_shared<Registry>(*previous);
		if (!updated->importers.emplace(extension, importer).second)
		{
			return Result<void>::Failure("Duplicate asset importer extension: " + extension);
		}
		++updated->generation;
		if (!scriptedIdentity.empty())
		{
			updated->scriptedIdentities.emplace(extension, scriptedIdentity);
			updated->scriptedVersions.emplace(extension, version);
		}
		std::shared_ptr<const Registry> replacement = std::move(updated);
		if (std::atomic_compare_exchange_weak(&registrations, &previous, replacement))
		{
			return {};
		}
	}
}

Result<void> ProjectAssetImporter::RegisterNativeProcessing(const String& identity, const Vector<String>& extensions,
                                                            ImportFunction importer)
{
	if (!importer || extensions.empty())
	{
		return Result<void>::Failure("Native processing requires extensions and a callback");
	}
	const auto native = AssetImportRules::Create();
	if (!native)
	{
		return Result<void>::Failure(native.error);
	}
	for (;;)
	{
		auto previous = std::atomic_load(&registrations);
		auto updated = std::make_shared<Registry>(*previous);
		for (const auto& extension : extensions)
		{
			const auto selected = native.value.FindImporter(extension);
			if (!selected || selected.value->identity != identity || !updated->importers.count(extension) ||
			    !updated->processedExtensions.insert(extension).second)
			{
				return Result<void>::Failure("Invalid or duplicate native processing registration: " + extension);
			}
			updated->importers[extension] = importer;
		}
		++updated->generation;
		std::shared_ptr<const Registry> replacement = std::move(updated);
		if (std::atomic_compare_exchange_weak(&registrations, &previous, replacement))
		{
			return {};
		}
	}
}

Result<AssetImportResult> ProjectAssetImporter::LoadCurrent(AssetDatabase& database, const VirtualPath& source)
{
	auto status = database.CurrentImport(source);
	if (!status || !status.value.upToDate)
	{
		return Result<AssetImportResult>::Failure(status ? "Asset import is pending: " + source.ToString()
		                                                 : status.error);
	}
	auto metadata = database.Metadata(source);
	return metadata ? Result<AssetImportResult>::Success({metadata.value.guid,
	                                                      status.value.artifacts,
	                                                      status.value.dependencies,
	                                                      true,
	                                                      {},
	                                                      status.value.mainObjectLocalFileId})
	                : Result<AssetImportResult>::Failure(metadata.error);
}

Result<AssetImportResult> ProjectAssetImporter::Import(AssetDatabase& database, const VirtualPath& source,
                                                       bool force) const
{
	URSA_PROFILE_DIAGNOSTIC_SCOPE("Asset.Import");
	URSA_PROFILE_OPERATION("Asset.Import", source.ToString().c_str(), "begin");

	struct ActiveImport
	{
		const ProjectAssetImporter* service;
		const VirtualFileSystem* files;
		VirtualPath path;
	};

	static thread_local Vector<ActiveImport> activeImports;
	if (std::any_of(activeImports.begin(), activeImports.end(), [&](const auto& item)
	                { return item.service == this && item.files == &database.Filesystem() && item.path == source; }))
	{
		String chain;
		for (const auto& item : activeImports)
		{
			chain += item.path.ToString() + " -> ";
		}
		return Result<AssetImportResult>::Failure("Cyclic artifact dependency: " + chain + source.ToString());
	}
	activeImports.push_back({this, &database.Filesystem(), source});

	struct PopImport
	{
		Vector<ActiveImport>& values;

		~PopImport()
		{
			values.pop_back();
		}
	} pop{activeImports};

	const auto snapshot = std::atomic_load(&registrations);
	const auto import = [&]() -> Result<AssetImportResult>
	{
		const auto& importers = snapshot->importers;
		auto metadata = database.Metadata(source);
		if (!metadata || metadata.value.folder)
		{
			return Result<AssetImportResult>::Failure(metadata ? "Cannot import a folder" : metadata.error);
		}
		const auto extension = Extension(source);
		if (metadata.value.importer.find("script:") == 0)
		{
			const auto required = snapshot->scriptedIdentities.find(extension);
			if (required == snapshot->scriptedIdentities.end() || required->second != metadata.value.importer)
			{
				return Result<AssetImportResult>::Failure("Required ScriptedImporter is missing for " +
				                                          source.ToString() + ": " + metadata.value.importer);
			}
		}
		const auto selected = importers.find(extension);
		const auto scripted = snapshot->scriptedIdentities.find(extension);
		if (scripted != snapshot->scriptedIdentities.end() && database.Filesystem().CheckWritable(source))
		{
			auto saved = database.SetImporterSettings(
			    source, scripted->second, snapshot->scriptedVersions.at(extension), metadata.value.importSettings);
			if (!saved)
			{
				return Result<AssetImportResult>::Failure(saved.error);
			}
		}
		if (database.Filesystem().CheckWritable(source))
		{
			if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
			{
				auto settings = TextureImporter::Settings(database, source);
				if (!settings)
				{
					return Result<AssetImportResult>::Failure(settings.error);
				}
				if (metadata.value.importer != "ursa.Texture" || metadata.value.importerVersion != 1 ||
				    metadata.value.importSettings != settings.value.ToJson())
				{
					auto saved = TextureImporter::SetSettings(database, source, settings.value);
					if (!saved)
					{
						return Result<AssetImportResult>::Failure(saved.error);
					}
				}
			}
			else if (ModelImporter::IsSupportedSource(source))
			{
				auto settings = ModelImporter::Settings(database, source);
				if (!settings)
				{
					return Result<AssetImportResult>::Failure(settings.error);
				}
				if (metadata.value.importer != "ursa.Model" ||
				    metadata.value.importerVersion != ModelImporter::Version ||
				    metadata.value.importSettings != settings.value.ToJson())
				{
					auto saved = ModelImporter::SetSettings(database, source, settings.value);
					if (!saved)
					{
						return Result<AssetImportResult>::Failure(saved.error);
					}
				}
			}
		}
		metadata = database.Metadata(source);
		if (!metadata)
		{
			return Result<AssetImportResult>::Failure(metadata.error);
		}
		VirtualFileSystem frozen;
		auto candidateLibrary = std::make_shared<MemoryFileProvider>();
		auto stagedLibrary = frozen.MountProvider(editorLibraryScheme, candidateLibrary, MountAccess::ReadWrite);
		if (!stagedLibrary)
		{
			return Result<AssetImportResult>::Failure(stagedLibrary.error);
		}
		Vector<RuntimeContentInput> inputs;
		for (const auto& scheme : database.Filesystem().Schemes())
		{
			if (scheme != source.scheme)
			{
				for (const auto& provider : database.Filesystem().Providers())
				{
					if (provider.scheme == scheme)
					{
						auto mounted = frozen.MountProvider(scheme, provider.provider,
						                                    scheme == editorLibraryScheme ? MountAccess::ReadOnly
						                                                                  : provider.access);
						if (!mounted)
						{
							return Result<AssetImportResult>::Failure(mounted.error);
						}
					}
				}
				continue;
			}
			auto memory = std::make_shared<MemoryFileProvider>();
			auto entries = database.Filesystem().List(scheme);
			if (!entries)
			{
				return Result<AssetImportResult>::Failure(entries.error);
			}
			for (const auto& entry : entries.value)
			{
				if (selected == importers.end() ||
				    ((extension == ".png" || extension == ".jpg" || extension == ".jpeg") &&
				     !snapshot->processedExtensions.count(extension)))
				{
					bool needed = entry.path == source || entry.path.ToString() == source.ToString() + ".meta";
					for (auto slash = source.relative.find('/'); slash != String::npos;
					     slash = source.relative.find('/', slash + 1))
					{
						needed = needed || entry.path.relative == source.relative.substr(0, slash) + ".meta";
					}
					if (!needed)
					{
						continue;
					}
				}
				auto bytes = database.Filesystem().Read(entry.path);
				if (!bytes)
				{
					return Result<AssetImportResult>::Failure(bytes.error);
				}
				inputs.push_back(
				    {entry.path, BuildResourceFingerprint(bytes.value), static_cast<UInt64>(bytes.value.size())});
				memory->WriteAtomic(entry.path.relative, bytes.value, true);
			}
			auto snapshotProvider = std::make_shared<ImportSourceSnapshot>(memory, source.relative + ".meta");
			auto mounted = frozen.MountProvider(scheme, snapshotProvider,
			                                    database.Filesystem().CheckWritable(source) ? MountAccess::ReadWrite
			                                                                                : MountAccess::ReadOnly);
			if (!mounted)
			{
				return Result<AssetImportResult>::Failure(mounted.error);
			}
		}
		auto isolated = AssetDatabase::Open(frozen, database.SourceScheme());
		if (!isolated)
		{
			return Result<AssetImportResult>::Failure(isolated.error);
		}
		auto refreshed = isolated.value->Refresh();
		if (!refreshed)
		{
			return Result<AssetImportResult>::Failure(refreshed.error);
		}
		isolated.value->BeginCandidateCapture();
		auto result = selected == importers.end() ? DefaultImport(*isolated.value, source, force)
		                                          : selected->second(*isolated.value, source, force);
		if (!result)
		{
			return result;
		}
		auto candidate = isolated.value->CurrentImport(source);
		auto candidateMetadata = isolated.value->Metadata(source);
		if (!candidate || !candidate.value.upToDate || !candidateMetadata ||
		    candidateMetadata.value.guid != metadata.value.guid || result.value.guid != metadata.value.guid)
		{
			return Result<AssetImportResult>::Failure("Importer did not produce a current candidate for " +
			                                          source.ToString());
		}
		for (const auto& input : inputs)
		{
			auto current = CaptureRuntimeContentInput(database.Filesystem(), input.path);
			if (!current || current.value.fingerprint != input.fingerprint)
			{
				return Result<AssetImportResult>::Failure("Asset inputs changed during import: " +
				                                          input.path.ToString());
			}
		}
		refreshed = database.ReloadImports();
		if (!refreshed)
		{
			return Result<AssetImportResult>::Failure(refreshed.error);
		}
		if (candidateMetadata.value.importer != metadata.value.importer ||
		    candidateMetadata.value.importerVersion != metadata.value.importerVersion ||
		    candidateMetadata.value.importSettings != metadata.value.importSettings)
		{
			auto saved = database.SetImporterSettings(source, candidateMetadata.value.importer,
			                                          candidateMetadata.value.importerVersion,
			                                          candidateMetadata.value.importSettings);
			if (!saved)
			{
				return Result<AssetImportResult>::Failure(saved.error);
			}
		}
		auto currentInputs =
		    database.CheckImport(source, candidate.value.importer, candidate.value.importerVersion,
		                         candidate.value.target, candidate.value.settings, candidate.value.dependencies);
		if (!currentInputs || currentInputs.value.fingerprint != candidate.value.fingerprint)
		{
			return Result<AssetImportResult>::Failure("Asset inputs changed before candidate publication: " +
			                                          source.ToString());
		}
		if (std::atomic_load(&registrations) != snapshot)
		{
			return Result<AssetImportResult>::Failure("Import rules changed before publication: " + source.ToString());
		}
		for (const auto& entry : candidate.value.settings.value("artifacts", nlohmann::json::array()))
		{
			const auto reference = AssetReferenceFromJson(entry.at("reference"), "candidate artifact dependency");
			if (!reference)
			{
				return Result<AssetImportResult>::Failure(reference.error);
			}
			Result<ImportedArtifactLocation> artifact =
			    database.ResolveImportedArtifact(reference.value.guid, reference.value.localFileId);
			if (!database.Contains(reference.value.guid))
			{
				for (const auto& scheme : database.Filesystem().Schemes())
				{
					if (scheme == database.SourceScheme() || scheme == editorLibraryScheme ||
					    scheme == userDataScheme || scheme == projectSettingsScheme || scheme == "importframework" ||
					    scheme.find("framework") == 0)
					{
						continue;
					}
					auto other = AssetDatabase::Open(database.Filesystem(), scheme);
					if (!other)
					{
						return Result<AssetImportResult>::Failure(other.error);
					}
					const auto refreshedOther = other.value->Refresh();
					if (!refreshedOther)
					{
						return Result<AssetImportResult>::Failure(refreshedOther.error);
					}
					if (other.value->Contains(reference.value.guid))
					{
						artifact =
						    other.value->ResolveImportedArtifact(reference.value.guid, reference.value.localFileId);
						break;
					}
				}
			}
			if (!artifact ||
			    artifact.value.contentFingerprint != ToUrsaString(entry.at("fingerprint").get<std::string>()))
			{
				return Result<AssetImportResult>::Failure("Artifact dependency changed before publication: " +
				                                          source.ToString());
			}
		}
		auto published = database.PublishCandidates(*isolated.value);
		if (!published)
		{
			return Result<AssetImportResult>::Failure(published.error);
		}
		auto current = database.CurrentImport(source);
		if (!current || !current.value.upToDate)
		{
			return Result<AssetImportResult>::Failure(current ? "Published import is not current" : current.error);
		}
		result.value.artifacts = current.value.artifacts;
		result.value.dependencies = current.value.dependencies;
		result.value.mainObjectLocalFileId = current.value.mainObjectLocalFileId;
		return result;
	};
	auto result = import();
	for (Size attempt = 0; !result && !result.value.pendingArtifacts.empty() && attempt < 64; ++attempt)
	{
		if (std::atomic_load(&registrations) != snapshot)
		{
			return Result<AssetImportResult>::Failure("Import rules changed during dependency scheduling: " +
			                                          source.ToString());
		}
		for (const auto& dependency : result.value.pendingArtifacts)
		{
			std::unique_ptr<AssetDatabase> secondary;
			AssetDatabase* owner = &database;
			if (dependency.source.scheme != database.SourceScheme())
			{
				auto opened = AssetDatabase::Open(database.Filesystem(), dependency.source.scheme);
				if (!opened)
				{
					return Result<AssetImportResult>::Failure(opened.error);
				}
				secondary = std::move(opened.value);
				auto refreshed = secondary->Refresh();
				if (!refreshed)
				{
					return Result<AssetImportResult>::Failure(refreshed.error);
				}
				owner = secondary.get();
			}
			if (!dependency.initialSettings.empty())
			{
				const auto metadata = owner->Metadata(dependency.source);
				if (!metadata)
				{
					return Result<AssetImportResult>::Failure(metadata.error);
				}
				if (metadata.value.importer.empty() && owner->Filesystem().CheckWritable(dependency.source))
				{
					const auto settings = TextureImportSettings::FromJson(dependency.initialSettings);
					if (!settings)
					{
						return Result<AssetImportResult>::Failure(settings.error);
					}
					const auto saved = TextureImporter::SetSettings(*owner, dependency.source, settings.value);
					if (!saved)
					{
						return Result<AssetImportResult>::Failure(saved.error);
					}
				}
			}
			const auto prepared = Import(*owner, dependency.source, false);
			if (!prepared)
			{
				return Result<AssetImportResult>::Failure("Artifact dependency from " + source.ToString() + " to " +
				                                          dependency.source.ToString() + ": " + prepared.error);
			}
		}
		result = import();
	}
	if (result)
	{
		URSA_PROFILE_OPERATION("Asset.Import", source.ToString().c_str(),
		                       result.value.cacheHit ? "cache-hit" : "complete");
	}
	else
	{
		URSA_PROFILE_OPERATION("Asset.Import.failed", source.ToString().c_str(), result.error.c_str());
	}
	return result;
}
} // namespace Ursa::Editor
