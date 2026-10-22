#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Core/StringInterop.hpp>

namespace Ursa
{
namespace
{
constexpr UInt64 maxAssetBytes = 16ull * 1024 * 1024;
constexpr Size maxGraphAssets = 4096;

Result<void> CheckEnvelope(const ArchiveReader& reader)
{
	auto format = reader.Text("format");
	auto version = reader.Unsigned("version");
	return format && format.value == "UrsaScriptableAsset" && version && version.value == scriptableAssetFileVersion
	           ? Result<void>{}
	           : Result<void>::Failure("Unsupported or malformed ScriptableAsset file envelope");
}

Result<ArchiveWriter> Envelope(const ScriptableAsset& asset)
{
	ArchiveWriter writer;
	auto result = asset.Serialize(writer);
	if (result)
	{
		result = writer.Write("format", String("UrsaScriptableAsset"));
	}
	if (result)
	{
		result = writer.Write("version", UInt64(scriptableAssetFileVersion));
	}
	return result ? Result<ArchiveWriter>::Success(std::move(writer)) : Result<ArchiveWriter>::Failure(result.error);
}

Result<ScriptableAsset> Decode(const ArchiveReader& reader)
{
	auto valid = CheckEnvelope(reader);
	if (!valid)
	{
		return Result<ScriptableAsset>::Failure(valid.error);
	}
	SerializationContext context;
	ScriptableAsset asset;
	auto loaded = asset.Deserialize(reader, context);
	return loaded ? Result<ScriptableAsset>::Success(std::move(asset)) : Result<ScriptableAsset>::Failure(loaded.error);
}

Result<void> References(const nlohmann::json& value, Vector<AssetReference>& output)
{
	try
	{
		if (value.is_object() && value.contains("$ursaReference"))
		{
			const auto& reference = value.at("$ursaReference");
			if (reference.at("Kind") == "ScriptableObject")
			{
				auto parsed = AssetReferenceFromJson(reference.at("Resource"), "ScriptableObject field");
				if (!parsed || !parsed.value.guid || !parsed.value.localFileId)
				{
					return Result<void>::Failure(parsed ? "ScriptableObject dependency identity is empty"
					                                    : parsed.error);
				}
				output.push_back(parsed.value);
			}
		}
		if (value.is_structured())
		{
			for (const auto& child : value)
			{
				auto valid = References(child, output);
				if (!valid)
				{
					return valid;
				}
			}
		}
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(String("Invalid ScriptableObject dependency: ") + error.what());
	}
}

Result<void> Dependencies(const nlohmann::json& value, Vector<AssetReference>& output)
{
	try
	{
		if (value.is_object() && value.contains("$ursaReference"))
		{
			const auto& reference = value.at("$ursaReference");
			const auto kind = reference.at("Kind").get<std::string>();
			if (kind == "ScriptableObject" || kind == "Shader" || kind == "ComputeShader" || kind == "Texture" ||
			    kind == "Material" || kind == "Mesh")
			{
				const auto& resource = reference.at("Resource");
				const auto& identity = resource.is_object() && resource.value("kind", std::string{}) == "asset"
				                           ? resource.at("asset")
				                           : resource;
				auto parsed = AssetReferenceFromJson(identity, "ScriptableAsset field");
				if (!parsed || !parsed.value.guid || (kind == "ScriptableObject" && !parsed.value.localFileId))
				{
					return Result<void>::Failure(parsed ? "ScriptableAsset dependency identity is empty"
					                                    : parsed.error);
				}
				output.push_back(parsed.value);
			}
		}
		if (value.is_structured())
		{
			for (const auto& child : value)
			{
				auto valid = Dependencies(child, output);
				if (!valid)
				{
					return valid;
				}
			}
		}
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(String("Invalid ScriptableAsset dependency: ") + error.what());
	}
}
} // namespace

Result<Vector<AssetReference>> ScriptableAssetDependencies(const ScriptableAsset& asset)
{
	Vector<AssetReference> dependencies;
	auto valid = Dependencies(asset.Payload(), dependencies);
	return valid ? Result<Vector<AssetReference>>::Success(std::move(dependencies))
	             : Result<Vector<AssetReference>>::Failure(valid.error);
}

Result<void> SaveScriptableAssetSource(VirtualFileSystem& files, const VirtualPath& path, const ScriptableAsset& asset)
{
	auto writer = Envelope(asset);
	if (!writer)
	{
		return Result<void>::Failure(writer.error);
	}
	auto json = ArchiveToJson(writer.value);
	if (!json)
	{
		return Result<void>::Failure(json.error);
	}
	json.value.erase("sourceGuid");
	return WriteJsonResource(files, path, json.value);
}

Result<ScriptableAsset> LoadScriptableAssetSource(const VirtualFileSystem& files, const VirtualPath& path,
                                                  ResourceId metadataGuid)
{
	auto size = files.Size(path);
	if (!metadataGuid || !size || size.value > maxAssetBytes)
	{
		return Result<ScriptableAsset>::Failure("ScriptableAsset source GUID or file size is invalid at " +
		                                        path.ToString());
	}
	auto bytes = files.Read(path);
	if (!bytes)
	{
		return Result<ScriptableAsset>::Failure(bytes.error);
	}
	try
	{
		auto json = nlohmann::json::parse(bytes.value.begin(), bytes.value.end());
		if (!json.is_object() || json.contains("sourceGuid") || json.contains("id"))
		{
			return Result<ScriptableAsset>::Failure("ScriptableAsset source identity must come from its .meta");
		}
		json["sourceGuid"] = ToStdString(metadataGuid.ToString());
		auto reader = ArchiveFromJson(json);
		return reader ? Decode(reader.value) : Result<ScriptableAsset>::Failure(reader.error);
	}
	catch (const std::exception& error)
	{
		return Result<ScriptableAsset>::Failure("Invalid ScriptableAsset source " + path.ToString() + ": " +
		                                        error.what());
	}
}

Result<ScriptableAsset> CreateScriptableAssetWithDefaults(ResourceId metadataGuid, const ScriptTypeDescriptor& type)
{
	if (!metadataGuid || !type.sourceGuid || type.localFileId == 0 || type.scriptType.empty() || type.version == 0 ||
	    type.kind != ScriptTypeKind::ScriptableObject || type.group != ScriptAssemblyGroup::Game)
	{
		return Result<ScriptableAsset>::Failure("Cannot create a ScriptableAsset from an invalid catalog type");
	}
	nlohmann::json fields = nlohmann::json::object();
	nlohmann::json fieldTypes = nlohmann::json::object();
	try
	{
		for (const ScriptFieldTypeDescriptor& field : type.fields)
		{
			if (field.name.empty() || field.kind.empty() || field.kind == "Unsupported" ||
			    ((field.kind == "Array" || field.kind == "List") && field.elementKind == "Unsupported") ||
			    fields.contains(ToStdString(field.name)))
			{
				return Result<ScriptableAsset>::Failure(
				    "ScriptableAsset type has an invalid serialized field: " + type.clrTypeName + "." + field.name);
			}
			fields[ToStdString(field.name)] = nlohmann::json::parse(ToStdString(field.defaultValueJson));
			fieldTypes[ToStdString(field.name)] = {{"Kind", ToStdString(field.kind)},
			                                       {"ElementKind", ToStdString(field.elementKind)},
			                                       {"TypeName", ToStdString(field.typeName)},
			                                       {"ElementTypeName", ToStdString(field.elementTypeName)}};
			if (field.kind == "Enumeration" || field.elementKind == "Enumeration")
			{
				auto& schema = fieldTypes[ToStdString(field.name)];
				schema["EnumValues"] = nlohmann::json::array();
				schema["EnumNumericValues"] = nlohmann::json::array();
				schema["EnumUnderlyingType"] = ToStdString(field.enumUnderlyingType);
				for (const auto& name : field.enumValues)
				{
					schema["EnumValues"].push_back(ToStdString(name));
				}
				for (const auto& value : field.enumNumericValuesJson)
				{
					schema["EnumNumericValues"].push_back(nlohmann::json::parse(ToStdString(value)));
				}
			}
		}
	}
	catch (const std::exception& error)
	{
		return Result<ScriptableAsset>::Failure("ScriptableAsset type has invalid default fields: " +
		                                        ToUrsaString(error.what()));
	}
	nlohmann::json payload = {{"FormatVersion", 1},
	                          {"ScriptType", ToStdString(type.scriptType)},
	                          {"Version", type.version},
	                          {"SourceGuid", ToStdString(type.sourceGuid.ToString())},
	                          {"LocalFileId", type.localFileId},
	                          {"Fields", std::move(fields)},
	                          {"FieldTypes", std::move(fieldTypes)}};
	ScriptableAsset asset;
	auto described =
	    asset.SetDescription(metadataGuid, type.localFileId, type.scriptType, type.version, std::move(payload));
	return described ? Result<ScriptableAsset>::Success(std::move(asset))
	                 : Result<ScriptableAsset>::Failure(described.error);
}

Result<Vector<UInt8>> EncodeScriptableAssetArtifact(const ScriptableAsset& asset)
{
	auto writer = Envelope(asset);
	return writer ? BinaryArchive::Encode(writer.value) : Result<Vector<UInt8>>::Failure(writer.error);
}

Result<ScriptableAsset> DecodeScriptableAssetArtifact(const Vector<UInt8>& bytes)
{
	auto reader = BinaryArchive::Decode(bytes, maxAssetBytes);
	return reader ? Decode(reader.value) : Result<ScriptableAsset>::Failure(reader.error);
}

Result<ScriptableAssetGraph> LoadScriptableAssetGraph(ResourceSession& resources, AssetReference root)
{
	ScriptableAssetGraph graph{root, {}};
	Set<String> visited;
	UInt64 bytesRead = 0;
	Vector<AssetReference> pending;
	const auto enqueue = [&](AssetReference reference) -> Result<void>
	{
		if (!reference.guid || (!reference.localFileId && (!pending.empty() || reference.guid != root.guid)))
		{
			return Result<void>::Failure("ScriptableAsset root/dependency identity is empty");
		}
		const String key = reference.guid.ToString() + ":" + ToUrsaString(std::to_string(reference.localFileId));
		if (!visited.insert(key).second)
		{
			return {};
		}
		if (visited.size() > maxGraphAssets)
		{
			return Result<void>::Failure("ScriptableAsset graph exceeds its asset budget");
		}
		pending.push_back(reference);
		return {};
	};
	auto valid = enqueue(root);
	if (!valid)
	{
		return Result<ScriptableAssetGraph>::Failure(valid.error);
	}
	for (Size index = 0; index < pending.size(); ++index)
	{
		const auto reference = pending[index];
		auto path = resources.ResolveAsset(reference.guid, reference.localFileId);
		if (!path)
		{
			return Result<ScriptableAssetGraph>::Failure(path.error);
		}
		const auto& files = resources.Index().Filesystem();
		auto size = files.Size(path.value);
		if (!size || size.value > maxAssetBytes || size.value > 64ull * 1024 * 1024 - bytesRead)
		{
			return Result<ScriptableAssetGraph>::Failure("ScriptableAsset graph exceeds its byte budget");
		}
		bytesRead += size.value;
		auto bytes = files.Read(path.value);
		if (!bytes)
		{
			return Result<ScriptableAssetGraph>::Failure(bytes.error);
		}
		auto asset = DecodeScriptableAssetArtifact(bytes.value);
		if (!asset)
		{
			return Result<ScriptableAssetGraph>::Failure(asset.error + " at " + path.value.ToString());
		}
		if (asset.value.SourceGuid() != reference.guid ||
		    (reference.localFileId && asset.value.LocalFileId() != reference.localFileId))
		{
			return Result<ScriptableAssetGraph>::Failure(
			    "ScriptableAsset artifact identity does not match its catalog reference");
		}
		if (!reference.localFileId)
		{
			const AssetReference canonical{reference.guid, asset.value.LocalFileId()};
			visited.erase(reference.guid.ToString() + ":0");
			const String key = canonical.guid.ToString() + ":" + ToUrsaString(std::to_string(canonical.localFileId));
			if (!visited.insert(key).second)
			{
				continue;
			}
			if (graph.assets.empty())
			{
				graph.root = canonical;
			}
		}
		Vector<AssetReference> dependencies;
		valid = References(asset.value.Payload(), dependencies);
		if (!valid)
		{
			return Result<ScriptableAssetGraph>::Failure(valid.error);
		}
		graph.assets.push_back(std::move(asset.value));
		for (auto dependency : dependencies)
		{
			valid = enqueue(dependency);
			if (!valid)
			{
				return Result<ScriptableAssetGraph>::Failure(valid.error);
			}
		}
	}
	return Result<ScriptableAssetGraph>::Success(std::move(graph));
}
} // namespace Ursa
