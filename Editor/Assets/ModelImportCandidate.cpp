#include <Assets/ModelImportCandidate.hpp>
#include <Assets/AssetImportRules.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
#include <Serialization/MeshResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/TextureResource.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <algorithm>
#include <cmath>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

const char* SlotName(MaterialTextureSlot slot)
{
	switch (slot)
	{
		case MaterialTextureSlot::BaseColor:
			return "baseColor";
		case MaterialTextureSlot::MetallicRoughness:
			return "metallicRoughness";
		case MaterialTextureSlot::Normal:
			return "normal";
		case MaterialTextureSlot::Occlusion:
			return "occlusion";
		case MaterialTextureSlot::Emissive:
			return "emissive";
	}
	return nullptr;
}

Vector<UInt8> Cbor(const Json& document)
{
	const auto bytes = Json::to_cbor(document);
	return Vector<UInt8>(bytes.data(), bytes.data() + bytes.size());
}

Json NodeDocument(const ModelCandidateNode& node)
{
	const auto& local = node.transform;
	const auto matrix = local.Matrix();
	Json values = Json::array();
	for (int row = 0; row < 4; ++row)
	{
		for (int column = 0; column < 4; ++column)
		{
			values.push_back(matrix.At(row, column));
		}
	}
	Json result{{"localFileId", node.localFileId},
	            {"parentLocalFileId", node.parentLocalFileId},
	            {"name", ToStdString(node.name)},
	            {"transform", std::move(values)},
	            {"localTransform",
	             {{"position", {local.position.x, local.position.y, local.position.z}},
	              {"rotation", {local.rotation.x, local.rotation.y, local.rotation.z, local.rotation.w}},
	              {"scale", {local.scale.x, local.scale.y, local.scale.z}}}},
	            {"meshLocalFileIds", Json::array()},
	            {"children", Json::array()}};
	for (const auto id : node.meshes)
	{
		result["meshLocalFileIds"].push_back(id);
	}
	for (const auto id : node.children)
	{
		result["children"].push_back(id);
	}
	return result;
}
} // namespace

Result<void> ModelImportCandidate::Validate() const
{
	if (!guid || !mainObjectLocalFileId || !rootNodeLocalFileId || nodes.empty() || meshOrder.size() != meshes.size())
	{
		return Result<void>::Failure("Model candidate has an invalid identity, root or mesh collection");
	}
	Set<UInt64> ids;
	const auto declare = [&](UInt64 id) -> bool
	{
		const auto key = outputKeys.find(id);
		return id && key != outputKeys.end() && StableImportObjectId("ursa.Model", key->second).value == id &&
		       ids.insert(id).second;
	};
	if (!declare(mainObjectLocalFileId))
	{
		return Result<void>::Failure("Model candidate has an invalid main output key");
	}
	Map<UInt64, const ModelCandidateNode*> nodeIndex;
	for (const auto& node : nodes)
	{
		if (!declare(node.localFileId) || !nodeIndex.emplace(node.localFileId, &node).second)
		{
			return Result<void>::Failure("Model candidate has duplicate or invalid node identity");
		}
		const auto& local = node.transform;
		for (const auto value :
		     {local.position.x, local.position.y, local.position.z, local.rotation.x, local.rotation.y,
		      local.rotation.z, local.rotation.w, local.scale.x, local.scale.y, local.scale.z})
		{
			if (!std::isfinite(value))
			{
				return Result<void>::Failure("Model candidate node transform is not finite");
			}
		}
		const auto quaternionLength = local.rotation.x * local.rotation.x + local.rotation.y * local.rotation.y +
		                              local.rotation.z * local.rotation.z + local.rotation.w * local.rotation.w;
		if (!std::isfinite(quaternionLength) || quaternionLength <= 1e-8f)
		{
			return Result<void>::Failure("Model candidate node rotation is invalid");
		}
		for (const auto value : node.transform.Matrix().values)
		{
			if (!std::isfinite(value))
			{
				return Result<void>::Failure("Model candidate node transform is not finite");
			}
		}
	}
	const auto root = nodeIndex.find(rootNodeLocalFileId);
	if (root == nodeIndex.end() || root->second->parentLocalFileId != 0)
	{
		return Result<void>::Failure("Model candidate root is missing or attached to another node");
	}
	Set<UInt64> visited;
	Vector<UInt64> pending{rootNodeLocalFileId};
	while (!pending.empty())
	{
		const auto id = pending.back();
		pending.pop_back();
		if (!visited.insert(id).second)
		{
			return Result<void>::Failure("Model candidate hierarchy contains a cycle or shared child");
		}
		const auto& node = *nodeIndex.at(id);
		for (const auto child : node.children)
		{
			const auto found = nodeIndex.find(child);
			if (found == nodeIndex.end() || found->second->parentLocalFileId != id)
			{
				return Result<void>::Failure("Model candidate hierarchy contains a missing or mismatched child");
			}
			pending.push_back(child);
		}
		for (const auto mesh : node.meshes)
		{
			if (meshes.find(mesh) == meshes.end())
			{
				return Result<void>::Failure("Model candidate node refers to a missing Mesh");
			}
		}
	}
	if (visited.size() != nodes.size())
	{
		return Result<void>::Failure("Model candidate has disconnected nodes");
	}
	for (const auto id : meshOrder)
	{
		const auto mesh = meshes.find(id);
		if (!declare(id) || mesh == meshes.end() || !mesh->second || !mesh->second->IsValid())
		{
			return Result<void>::Failure("Model candidate has invalid or repeated Mesh outputs");
		}
		auto valid = Mesh::Validate(mesh->second->Snapshot()->data);
		if (!valid)
		{
			return valid;
		}
		const auto identity = mesh->second->AssetIdentity();
		if (identity.guid && (identity.guid != guid || identity.localFileId != id))
		{
			return Result<void>::Failure("Model candidate Mesh belongs to another asset");
		}
	}
	for (const auto& binding : meshMaterialBindings)
	{
		if (meshes.find(binding.first) == meshes.end())
		{
			return Result<void>::Failure("Model candidate has a missing Mesh binding");
		}
		for (const auto material : binding.second)
		{
			if (material && materials.find(material) == materials.end())
			{
				return Result<void>::Failure("Model candidate has a missing material slot binding");
			}
		}
	}

	for (const auto& entry : materials)
	{
		if (!declare(entry.first) || entry.second.localFileId != entry.first || !entry.second.material ||
		    !entry.second.material->IsValid())
		{
			return Result<void>::Failure("Model candidate has an invalid material output");
		}
		const auto identity = entry.second.material->AssetIdentity();
		if (identity.guid && (identity.guid != guid || identity.localFileId != entry.first))
		{
			return Result<void>::Failure("Model candidate material belongs to another asset");
		}
		for (const auto& texture : entry.second.textures)
		{
			if (!SlotName(texture.first) || !texture.second.guid ||
			    (texture.second.guid == guid && textures.find(texture.second.localFileId) == textures.end()))
			{
				return Result<void>::Failure("Model candidate material has an invalid texture binding");
			}
		}
	}
	for (const auto& entry : textures)
	{
		if (!declare(entry.first) || !entry.second)
		{
			return Result<void>::Failure("Model candidate has an invalid embedded texture output");
		}
	}
	try
	{
		Set<UInt64> animationIds;
		for (const auto id : animationOrder)
		{
			const auto clip = archives.find(id);
			if (!animationIds.insert(id).second || clip == archives.end() || clip->second.kind != "AnimationClip")
			{
				return Result<void>::Failure("Model candidate has invalid animation references");
			}
		}
		for (const auto& entry : archives)
		{
			if ((entry.second.kind != "Skeleton" && entry.second.kind != "AnimationClip") ||
			    (entry.second.kind == "AnimationClip" && animationIds.find(entry.first) == animationIds.end()) ||
			    !declare(entry.first) || entry.second.document.at("localFileId").get<UInt64>() != entry.first)
			{
				return Result<void>::Failure("Model candidate has an invalid animation or skeleton output");
			}
			const auto& references = entry.second.document.at(entry.second.kind == "Skeleton" ? "joints" : "channels");
			for (const auto& reference : references)
			{
				if (nodeIndex.find(reference.at("nodeLocalFileId").get<UInt64>()) == nodeIndex.end())
				{
					return Result<void>::Failure("Model candidate removed a node used by a skeleton or animation");
				}
			}
		}
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Invalid model auxiliary data: " + ToUrsaString(error.what()));
	}
	return {};
}

Result<Vector<ImportedArtifact>> ModelImportCandidate::Encode() const
{
	auto valid = Validate();
	if (!valid)
	{
		return Result<Vector<ImportedArtifact>>::Failure(valid.error);
	}
	try
	{
		Vector<ImportedArtifact> outputs;
		Json model{{"format", "UrsaModel"},
		           {"version", 2u},
		           {"guid", ToStdString(guid.ToString())},
		           {"rootNodeLocalFileId", rootNodeLocalFileId},
		           {"nodes", Json::array()},
		           {"meshLocalFileIds", Json::array()},
		           {"materialLocalFileIds", Json::array()},
		           {"skeletonLocalFileIds", Json::array()},
		           {"animationClipLocalFileIds", Json::array()},
		           {"materials", Json::array()},
		           {"meshMaterialBindings", Json::object()},
		           {"dependencies", Json::array()},
		           {"diagnostics", Json::array()}};
		const auto append = [&](UInt64 id, const String& kind, Vector<UInt8> bytes)
		{ outputs.push_back({id, kind, std::move(bytes), outputKeys.at(id)}); };
		for (const auto& node : nodes)
		{
			auto document = NodeDocument(node);
			append(node.localFileId, "ModelNode", Cbor(document));
			model["nodes"].push_back(std::move(document));
		}
		for (const auto id : meshOrder)
		{
			auto encoded = EncodeMeshResource(guid, *meshes.at(id), id);
			if (!encoded)
			{
				return Result<Vector<ImportedArtifact>>::Failure(encoded.error);
			}
			append(id, "Mesh", std::move(encoded.value));
			model["meshLocalFileIds"].push_back(id);
		}
		VirtualFileSystem staging;
		auto mounted =
		    staging.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite);
		if (!mounted)
		{
			return Result<Vector<ImportedArtifact>>::Failure(mounted.error);
		}
		for (const auto& entry : materials)
		{
			ArchiveWriter writer;
			auto serialized = entry.second.material->Serialize(writer);
			auto document = serialized ? ArchiveToJson(writer) : Result<Json>::Failure(serialized.error);
			if (!document)
			{
				return Result<Vector<ImportedArtifact>>::Failure(document.error);
			}
			document.value["textures"] = Json::object();
			for (const auto& texture : entry.second.textures)
			{
				document.value["textures"][SlotName(texture.first)] = AssetReferenceToJson(texture.second);
			}
			auto material = entry.second.material->Clone();
			if (!material->IsShaderBacked())
			{
				auto migrated = material->MigrateLegacyToShader(material->Type() == MaterialType::Unlit
				                                                    ? Shader::BuiltinUnlitShaderId()
				                                                    : Shader::BuiltinPbrShaderId());
				if (!migrated)
				{
					return Result<Vector<ImportedArtifact>>::Failure(migrated.error);
				}
			}
			const Array<String, 5> textureProperties =
			    material->Type() == MaterialType::Unlit
			        ? Array<String, 5>{"_MainTex", {}, {}, {}, {}}
			        : Array<String, 5>{"_BaseMap", "_MetallicRoughnessMap", "_NormalMap", "_OcclusionMap",
			                           "_EmissiveMap"};
			for (const auto& binding : entry.second.textures)
			{
				auto bound = material->SetTextureReference(binding.first, binding.second, {});
				if (!bound)
				{
					return Result<Vector<ImportedArtifact>>::Failure(bound.error);
				}
				const auto& property = textureProperties[Size(binding.first)];
				if (property.empty())
				{
					continue;
				}
				auto value = MaterialPropertyValue::Texture2D(binding.second.guid, {});
				value.textureLocalFileId = binding.second.localFileId;
				auto assigned = material->SetPropertyValue(property, *material->Shader(), std::move(value));
				if (!assigned)
				{
					return Result<Vector<ImportedArtifact>>::Failure(assigned.error);
				}
			}
			const VirtualPath materialPath{projectScheme, "Material.ursamat"};
			auto saved = SaveMaterialResource(staging, materialPath, guid, *material, {});
			auto bytes = saved ? staging.Read(materialPath) : Result<Vector<UInt8>>::Failure(saved.error);
			if (!bytes)
			{
				return Result<Vector<ImportedArtifact>>::Failure(bytes.error);
			}
			append(entry.first, "Material", std::move(bytes.value));
			model["materialLocalFileIds"].push_back(entry.first);
			model["materials"].push_back({{"localFileId", entry.first},
			                              {"kind", "Material"},
			                              {"name", ToStdString(entry.second.name)},
			                              {"material", std::move(document.value)}});
		}
		for (const auto& binding : meshMaterialBindings)
		{
			auto slots = Json::array();
			for (const auto material : binding.second)
			{
				slots.push_back(material);
			}
			model["meshMaterialBindings"][std::to_string(binding.first)] = std::move(slots);
		}
		const auto path = VirtualPath::Parse("prj:Embedded.ursatex").value;
		for (const auto& entry : textures)
		{
			auto saved = SaveTextureResource(staging, path, guid, *entry.second);
			auto bytes = saved ? staging.Read(path) : Result<Vector<UInt8>>::Failure(saved.error);
			if (!bytes)
			{
				return Result<Vector<ImportedArtifact>>::Failure(bytes.error);
			}
			append(entry.first, "Texture", std::move(bytes.value));
		}
		for (const auto& entry : archives)
		{
			append(entry.first, entry.second.kind, Cbor(entry.second.document));
			if (entry.second.kind == "Skeleton")
			{
				model["skeletonLocalFileIds"].push_back(entry.first);
			}
		}
		for (const auto id : animationOrder)
		{
			model["animationClipLocalFileIds"].push_back(id);
		}
		for (const auto& pathValue : dependencies)
		{
			model["dependencies"].push_back(ToStdString(pathValue.ToString()));
		}
		for (const auto& diagnostic : diagnostics)
		{
			model["diagnostics"].push_back(ToStdString(diagnostic));
		}
		outputs.insert(outputs.begin(),
		               {mainObjectLocalFileId, "Model", Cbor(model), outputKeys.at(mainObjectLocalFileId)});
		return Result<Vector<ImportedArtifact>>::Success(std::move(outputs));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<ImportedArtifact>>::Failure("Cannot encode model candidates: " +
		                                                 ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Editor
