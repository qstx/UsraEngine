#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <Serialization/ResourceSession.hpp>
#include <charconv>
#include <Serialization/JsonArchiveInterop.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/TextureResource.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;

String ResourceCacheKey(ResourceId guid, Ursa::UInt64 localFileId)
{
	return guid.ToString() + ":" + ToUrsaString(std::to_string(localFileId));
}

Result<Json> ReadCborArtifact(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	auto bytes = filesystem.Read(path);
	if (!bytes)
	{
		return Result<Json>::Failure(bytes.error);
	}
	try
	{
		return Result<Json>::Success(Json::from_cbor(bytes.value.begin(), bytes.value.end()));
	}
	catch (const std::exception& error)
	{
		return Result<Json>::Failure("Cannot decode imported sub-asset " + path.ToString() + ": " +
		                             ToUrsaString(error.what()));
	}
}

Result<Vector<Ursa::UInt8>> ReadBinary(const Json& document, const char* field, const VirtualPath& path)
{
	if (!document.contains(field) || !document[field].is_binary())
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Imported mesh has no binary " + ToUrsaString(field) + " data at " +
		                                            path.ToString());
	}
	const auto& binary = document[field].get_binary();
	Vector<Ursa::UInt8> result;
	result.reserve(binary.size());
	for (Ursa::UInt8 byte : binary)
	{
		result.push_back(byte);
	}
	return Result<Vector<Ursa::UInt8>>::Success(std::move(result));
}

Result<Vector<Ursa::Float32>> ReadFloatVector(const Json& value, Ursa::Size count, const String& location)
{
	if (!value.is_array() || value.size() != count)
	{
		return Result<Vector<Ursa::Float32>>::Failure("Invalid vector data at " + location);
	}
	Vector<Ursa::Float32> result;
	result.reserve(count);
	for (const Json& item : value)
	{
		if (!item.is_number())
		{
			return Result<Vector<Ursa::Float32>>::Failure("Nonnumeric vector data at " + location);
		}
		const Ursa::Float32 component = item.get<Ursa::Float32>();
		if (!std::isfinite(component))
		{
			return Result<Vector<Ursa::Float32>>::Failure("Nonfinite vector data at " + location);
		}
		result.push_back(component);
	}
	return Result<Vector<Ursa::Float32>>::Success(std::move(result));
}

Result<LocalTransform> ReadLocalTransform(const Json& value, const String& location)
{
	if (!value.is_object() || !value.contains("position") || !value.contains("rotation") || !value.contains("scale"))
	{
		return Result<LocalTransform>::Failure("Invalid local transform at " + location);
	}
	auto position = ReadFloatVector(value["position"], 3, location);
	auto rotation = ReadFloatVector(value["rotation"], 4, location);
	auto scale = ReadFloatVector(value["scale"], 3, location);
	if (!position || !rotation || !scale)
	{
		return Result<LocalTransform>::Failure(!position ? position.error : !rotation ? rotation.error : scale.error);
	}
	LocalTransform result;
	result.position = {position.value[0], position.value[1], position.value[2]};
	result.rotation = {rotation.value[0], rotation.value[1], rotation.value[2], rotation.value[3]};
	result.scale = {scale.value[0], scale.value[1], scale.value[2]};
	return Result<LocalTransform>::Success(result);
}

Result<Mat4> ReadMatrix(const Json& value, const String& location)
{
	auto elements = ReadFloatVector(value, 16, location);
	if (!elements)
	{
		return Result<Mat4>::Failure(elements.error);
	}
	Mat4 result;
	for (int row = 0; row < 4; ++row)
	{
		for (int column = 0; column < 4; ++column)
		{
			result.At(row, column) = elements.value[static_cast<Ursa::Size>(row * 4 + column)];
		}
	}
	return Result<Mat4>::Success(result);
}

Result<AnimationVectorKey> ReadVectorKey(const Json& value, const String& location)
{
	if (!value.is_array() || value.size() != 2 || !value[0].is_number())
	{
		return Result<AnimationVectorKey>::Failure("Invalid animation vector key at " + location);
	}
	const Ursa::Float64 timeSeconds = value[0].get<Ursa::Float64>();
	auto components = ReadFloatVector(value[1], 3, location);
	if (!std::isfinite(timeSeconds) || timeSeconds < 0.0 || !components)
	{
		return Result<AnimationVectorKey>::Failure("Invalid animation vector key at " + location);
	}
	return Result<AnimationVectorKey>::Success(
	    {timeSeconds, {components.value[0], components.value[1], components.value[2]}});
}

Result<AnimationRotationKey> ReadRotationKey(const Json& value, const String& location)
{
	if (!value.is_array() || value.size() != 2 || !value[0].is_number())
	{
		return Result<AnimationRotationKey>::Failure("Invalid animation rotation key at " + location);
	}
	const Ursa::Float64 timeSeconds = value[0].get<Ursa::Float64>();
	auto components = ReadFloatVector(value[1], 4, location);
	if (!std::isfinite(timeSeconds) || timeSeconds < 0.0 || !components)
	{
		return Result<AnimationRotationKey>::Failure("Invalid animation rotation key at " + location);
	}
	return Result<AnimationRotationKey>::Success(
	    {timeSeconds, {components.value[0], components.value[1], components.value[2], components.value[3]}});
}
} // namespace

Result<std::unique_ptr<ResourceSession>> ResourceSession::Open(const VirtualFileSystem& filesystem,
                                                               AssetPathResolver resolver)
{
	Vector<ResourceMount> mounts;
	for (const auto& mount : filesystem.Providers())
	{
		mounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	return Open(mounts, std::move(resolver));
}

Result<std::unique_ptr<ResourceSession>> ResourceSession::Open(const Vector<ResourceMount>& mounts,
                                                               AssetPathResolver resolver)
{
	auto session = std::unique_ptr<ResourceSession>(new ResourceSession);
	session->assetPathResolver = std::move(resolver);
	for (const ResourceMount& mount : mounts)
	{
		auto added = session->index.Mount(mount);
		if (!added)
		{
			return Result<std::unique_ptr<ResourceSession>>::Failure(added.error);
		}
	}
	auto scanned = session->index.Rebuild();
	if (!scanned)
	{
		return Result<std::unique_ptr<ResourceSession>>::Failure(scanned.error);
	}
	return Result<std::unique_ptr<ResourceSession>>::Success(std::move(session));
}

Result<std::unique_ptr<ResourceSession>> ResourceSession::Open(const std::filesystem::path& root, MountAccess access)
{
	Vector<ResourceMount> mounts;
	mounts.push_back(ResourceMount{projectScheme, root, access});
	return Open(mounts);
}

Result<std::unique_ptr<ResourceSession>> ResourceSession::Refreshed() const
{
	return Open(index.Mounts(), assetPathResolver);
}

Result<VirtualPath> ResourceSession::ResolveAssetPath(ResourceId guid, Ursa::UInt64 localFileId) const
{
	if (assetPathResolver)
	{
		auto resolved = assetPathResolver(guid, localFileId);
		if (resolved)
		{
			return resolved;
		}
	}
	return index.ResolveAsset(guid, localFileId);
}

Result<ResourceLocation> ResourceSession::ResolveResource(ResourceId id) const
{
	auto path = ResolveAssetPath(id, 0);
	if (path)
	{
		auto kind = ResourceKindForPath(path.value);
		if (kind)
		{
			return Result<ResourceLocation>::Success({kind.value, std::move(path.value)});
		}
		return Result<ResourceLocation>::Failure(kind.error);
	}
	return index.Resolve(id);
}

Result<MaterialTexturePtr> ResourceSession::LoadTexture(ResourceId id)
{
	const auto key = id.ToString();
	auto cached = textures.find(key);
	if (cached != textures.end())
	{
		return Result<MaterialTexturePtr>::Success(cached->second);
	}
	auto location = ResolveResource(id);
	if (!location)
	{
		return Result<MaterialTexturePtr>::Failure(location.error);
	}
	if (location.value.kind != ResourceKind::Texture)
	{
		return Result<MaterialTexturePtr>::Failure("Resource " + key +
		                                           " is not a texture: " + location.value.path.ToString());
	}

	auto loaded = LoadTextureResource(index.Filesystem(), location.value.path);
	if (!loaded)
	{
		return Result<MaterialTexturePtr>::Failure(loaded.error);
	}
	if (loaded.value.id != id)
	{
		return Result<MaterialTexturePtr>::Failure("Texture ID changed after indexing: " +
		                                           location.value.path.ToString());
	}
	textures.emplace(key, loaded.value.texture);
	URSA_LOG_DEBUG("Engine", "Loaded texture " + key + " from " + location.value.path.ToString());
	return Result<MaterialTexturePtr>::Success(std::move(loaded.value.texture));
}

Result<MaterialTexturePtr> ResourceSession::LoadTexture(ResourceId guid, Ursa::UInt64 localFileId)
{
	if (localFileId == 0)
	{
		return LoadTexture(guid);
	}
	if (!guid)
	{
		return Result<MaterialTexturePtr>::Failure("Texture sub-asset requires a nonzero asset GUID");
	}
	const String key = ResourceCacheKey(guid, localFileId);
	const auto cached = textures.find(key);
	if (cached != textures.end())
	{
		return Result<MaterialTexturePtr>::Success(cached->second);
	}
	auto path = ResolveAssetPath(guid, localFileId);
	if (!path)
	{
		return Result<MaterialTexturePtr>::Failure(path.error);
	}
	auto loaded = LoadTextureResource(index.Filesystem(), path.value);
	if (!loaded)
	{
		return Result<MaterialTexturePtr>::Failure(loaded.error);
	}
	if (loaded.value.id != guid)
	{
		return Result<MaterialTexturePtr>::Failure("Texture sub-asset owner GUID changed at " + path.value.ToString());
	}
	textures.emplace(key, loaded.value.texture);
	return Result<MaterialTexturePtr>::Success(std::move(loaded.value.texture));
}

Result<std::shared_ptr<MaterialInstance>> ResourceSession::LoadMaterial(ResourceId id)
{
	const auto key = id.ToString();
	auto cached = materials.find(key);
	if (cached != materials.end())
	{
		return Result<std::shared_ptr<MaterialInstance>>::Success(cached->second);
	}
	auto location = ResolveResource(id);
	if (!location)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(location.error);
	}
	if (location.value.kind != ResourceKind::Material)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(
		    "Resource " + key + " is not a material: " + location.value.path.ToString());
	}

	auto loaded = LoadMaterialResource(index.Filesystem(), location.value.path,
	                                   [this](ResourceId textureId) { return LoadTexture(textureId); });
	if (!loaded)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(loaded.error);
	}
	if (loaded.value.material->IsShaderBacked())
	{
		auto shader = LoadShader(loaded.value.material->ShaderId());
		if (!shader)
		{
			return Result<std::shared_ptr<MaterialInstance>>::Failure(location.value.path.ToString() + ": " +
			                                                          shader.error);
		}
		auto validated = loaded.value.material->ValidateAgainstShader(*shader.value);
		if (!validated)
		{
			return Result<std::shared_ptr<MaterialInstance>>::Failure(location.value.path.ToString() + ": " +
			                                                          validated.error);
		}
		auto attachedShader = loaded.value.material->SetShaderAsset(shader.value);
		if (!attachedShader)
		{
			return Result<std::shared_ptr<MaterialInstance>>::Failure(location.value.path.ToString() + ": " +
			                                                          attachedShader.error);
		}
	}
	if (loaded.value.id != id)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure("Material ID changed after indexing: " +
		                                                          location.value.path.ToString());
	}
	materials.emplace(key, loaded.value.material);
	URSA_LOG_DEBUG("Engine", "Loaded material " + key + " from " + location.value.path.ToString());
	return Result<std::shared_ptr<MaterialInstance>>::Success(std::move(loaded.value.material));
}

Result<std::shared_ptr<MaterialInstance>> ResourceSession::LoadMaterial(ResourceId guid, Ursa::UInt64 localFileId)
{
	if (localFileId == 0)
	{
		return LoadMaterial(guid);
	}
	if (!guid)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure("Material sub-asset requires a nonzero asset GUID");
	}
	const String key = ResourceCacheKey(guid, localFileId);
	const auto cached = materials.find(key);
	if (cached != materials.end())
	{
		return Result<std::shared_ptr<MaterialInstance>>::Success(cached->second);
	}
	auto path = ResolveAssetPath(guid, localFileId);
	if (!path)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(path.error);
	}
	auto document = ReadCborArtifact(index.Filesystem(), path.value);
	if (!document)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(document.error);
	}
	auto reader = ArchiveFromJson(document.value);
	if (!reader)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(path.value.ToString() + ": " + reader.error);
	}
	SerializationContext context(this);
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	auto loaded = material->Deserialize(reader.value, context);
	if (!loaded)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(path.value.ToString() + ": " + loaded.error);
	}
	if (material->IsShaderBacked())
	{
		auto shader = LoadShader(material->ShaderId());
		if (!shader)
		{
			return Result<std::shared_ptr<MaterialInstance>>::Failure(path.value.ToString() + ": " + shader.error);
		}
		auto validated = material->ValidateAgainstShader(*shader.value);
		if (!validated)
		{
			return Result<std::shared_ptr<MaterialInstance>>::Failure(path.value.ToString() + ": " + validated.error);
		}
		auto attached = material->SetShaderAsset(shader.value);
		if (!attached)
		{
			return Result<std::shared_ptr<MaterialInstance>>::Failure(path.value.ToString() + ": " + attached.error);
		}
	}
	materials.emplace(key, material);
	return Result<std::shared_ptr<MaterialInstance>>::Success(std::move(material));
}

Result<ModelMeshPtr> ResourceSession::LoadMesh(ResourceId modelGuid, Ursa::UInt64 localFileId)
{
	if (!modelGuid || localFileId == 0)
	{
		return Result<ModelMeshPtr>::Failure("Model mesh requires a nonzero model GUID and local file ID");
	}
	const String key = ResourceCacheKey(modelGuid, localFileId);
	const auto cached = meshes.find(key);
	if (cached != meshes.end())
	{
		return Result<ModelMeshPtr>::Success(cached->second);
	}
	auto path = ResolveAssetPath(modelGuid, localFileId);
	if (!path)
	{
		return Result<ModelMeshPtr>::Failure(path.error);
	}
	auto document = ReadCborArtifact(index.Filesystem(), path.value);
	if (!document)
	{
		return Result<ModelMeshPtr>::Failure(document.error);
	}
	const Ursa::UInt64 storedId = document.value.value("localFileId", Ursa::UInt64{0});
	const Ursa::UInt64 vertexStride = document.value.value("vertexStride", Ursa::UInt64{0});
	const Ursa::UInt64 vertexCount = document.value.value("vertexCount", Ursa::UInt64{0});
	const Ursa::UInt64 indexCount = document.value.value("indexCount", Ursa::UInt64{0});
	if (document.value.value("kind", std::string{}) != "Mesh" || storedId != localFileId || vertexStride != 60 ||
	    vertexCount == 0 || vertexCount > std::numeric_limits<Ursa::UInt32>::max() || indexCount == 0 ||
	    indexCount > std::numeric_limits<Ursa::UInt32>::max())
	{
		return Result<ModelMeshPtr>::Failure("Imported model mesh has an invalid descriptor at " +
		                                     path.value.ToString());
	}
	auto vertices = ReadBinary(document.value, "vertices", path.value);
	auto indexBytes = ReadBinary(document.value, "indices", path.value);
	if (!vertices || !indexBytes || vertices.value.size() != vertexStride * vertexCount ||
	    indexBytes.value.size() != indexCount * sizeof(Ursa::UInt32))
	{
		return Result<ModelMeshPtr>::Failure(
		    !vertices     ? vertices.error
		    : !indexBytes ? indexBytes.error
		                  : "Imported mesh byte counts do not match its descriptor at " + path.value.ToString());
	}
	auto mesh = std::make_shared<ModelMesh>();
	mesh->modelGuid = modelGuid;
	mesh->localFileId = localFileId;
	mesh->vertexStride = static_cast<Ursa::UInt32>(vertexStride);
	mesh->vertexBytes = std::move(vertices.value);
	mesh->indices.reserve(static_cast<Ursa::Size>(indexCount));
	for (Ursa::Size offset = 0; offset < indexBytes.value.size(); offset += sizeof(Ursa::UInt32))
	{
		Ursa::UInt32 vertexIndex = 0;
		for (Ursa::UInt32 byte = 0; byte < sizeof(Ursa::UInt32); ++byte)
		{
			vertexIndex |= static_cast<Ursa::UInt32>(indexBytes.value[offset + byte]) << (byte * 8);
		}
		if (vertexIndex >= vertexCount)
		{
			return Result<ModelMeshPtr>::Failure("Imported mesh index is outside its vertex range at " +
			                                     path.value.ToString());
		}
		mesh->indices.push_back(vertexIndex);
	}
	if (document.value.contains("materialLocalFileId") && document.value["materialLocalFileId"].is_number_unsigned())
	{
		const Ursa::UInt64 materialLocalFileId = document.value["materialLocalFileId"].get<Ursa::UInt64>();
		if (materialLocalFileId != 0)
		{
			auto material = LoadMaterial(modelGuid, materialLocalFileId);
			if (!material)
			{
				return Result<ModelMeshPtr>::Failure(path.value.ToString() + ": " + material.error);
			}
			mesh->material = std::move(material.value);
		}
	}
	if (document.value.contains("skeletonLocalFileId"))
	{
		if (!document.value["skeletonLocalFileId"].is_number_unsigned())
		{
			return Result<ModelMeshPtr>::Failure("Imported skinned mesh has an invalid skeleton reference at " +
			                                     path.value.ToString());
		}
		mesh->skeletonLocalFileId = document.value["skeletonLocalFileId"].get<Ursa::UInt64>();
		auto skeleton = LoadSkeleton(modelGuid, mesh->skeletonLocalFileId);
		if (!skeleton)
		{
			return Result<ModelMeshPtr>::Failure(path.value.ToString() + ": " + skeleton.error);
		}
		mesh->skeleton = std::move(skeleton.value);
		if (!document.value.contains("influenceCount") || !document.value["influenceCount"].is_number_unsigned() ||
		    document.value["influenceCount"].get<Ursa::UInt32>() == 0 ||
		    document.value["influenceCount"].get<Ursa::UInt32>() > 4 ||
		    document.value.value("skinInfluenceStride", Ursa::UInt64{0}) != 24)
		{
			return Result<ModelMeshPtr>::Failure("Imported skinned mesh has an invalid influence layout at " +
			                                     path.value.ToString());
		}
		auto influenceBytes = ReadBinary(document.value, "skinInfluences", path.value);
		if (!influenceBytes || influenceBytes.value.size() != vertexCount * 24)
		{
			return Result<ModelMeshPtr>::Failure(influenceBytes ? "Imported skin influence byte count is invalid at " +
			                                                          path.value.ToString()
			                                                    : influenceBytes.error);
		}
		mesh->skinInfluences.resize(static_cast<Ursa::Size>(vertexCount));
		for (Ursa::Size vertex = 0; vertex < mesh->skinInfluences.size(); ++vertex)
		{
			const Ursa::Size offset = vertex * 24;
			Ursa::Float32 totalWeight = 0.0f;
			for (Ursa::Size influence = 0; influence < 4; ++influence)
			{
				Ursa::UInt16 jointIndex =
				    static_cast<Ursa::UInt16>(influenceBytes.value[offset + influence * 2]) |
				    static_cast<Ursa::UInt16>(influenceBytes.value[offset + influence * 2 + 1] << 8);
				Ursa::UInt32 bits = 0;
				for (Ursa::UInt32 byte = 0; byte < 4; ++byte)
				{
					bits |= static_cast<Ursa::UInt32>(influenceBytes.value[offset + 8 + influence * 4 + byte])
					        << (byte * 8);
				}
				Ursa::Float32 weight = 0.0f;
				std::memcpy(&weight, &bits, sizeof(weight));
				if (!std::isfinite(weight) || weight < 0.0f ||
				    (weight > 0.0f && jointIndex >= mesh->skeleton->joints.size()))
				{
					return Result<ModelMeshPtr>::Failure("Imported skin influence is invalid at " +
					                                     path.value.ToString());
				}
				mesh->skinInfluences[vertex].jointIndices[influence] = jointIndex;
				mesh->skinInfluences[vertex].weights[influence] = weight;
				totalWeight += weight;
			}
			if (!std::isfinite(totalWeight) || (totalWeight != 0.0f && std::abs(totalWeight - 1.0f) > 0.001f))
			{
				return Result<ModelMeshPtr>::Failure("Imported skin weights are not normalized at " +
				                                     path.value.ToString());
			}
		}
	}
	ModelMeshPtr immutableMesh = std::move(mesh);
	meshes.emplace(key, immutableMesh);
	return Result<ModelMeshPtr>::Success(std::move(immutableMesh));
}

Result<std::shared_ptr<const ModelSkeleton>> ResourceSession::LoadSkeleton(ResourceId modelGuid,
                                                                           Ursa::UInt64 localFileId)
{
	if (!modelGuid || localFileId == 0)
	{
		return Result<std::shared_ptr<const ModelSkeleton>>::Failure(
		    "Model skeleton requires a nonzero model GUID and local file ID");
	}
	const String key = ResourceCacheKey(modelGuid, localFileId);
	const auto cached = skeletons.find(key);
	if (cached != skeletons.end())
	{
		return Result<std::shared_ptr<const ModelSkeleton>>::Success(cached->second);
	}
	auto path = ResolveAssetPath(modelGuid, localFileId);
	if (!path)
	{
		return Result<std::shared_ptr<const ModelSkeleton>>::Failure(path.error);
	}
	auto document = ReadCborArtifact(index.Filesystem(), path.value);
	if (!document)
	{
		return Result<std::shared_ptr<const ModelSkeleton>>::Failure(document.error);
	}
	if (document.value.value("format", std::string{}) != "UrsaSkeleton" ||
	    document.value.value("version", Ursa::UInt32{0}) != 1 ||
	    document.value.value("localFileId", Ursa::UInt64{0}) != localFileId || !document.value.contains("joints") ||
	    !document.value["joints"].is_array() || document.value["joints"].empty())
	{
		return Result<std::shared_ptr<const ModelSkeleton>>::Failure("Invalid model skeleton artifact at " +
		                                                             path.value.ToString());
	}
	auto skeleton = std::make_shared<ModelSkeleton>();
	skeleton->modelGuid = modelGuid;
	skeleton->localFileId = localFileId;
	for (Ursa::Size jointIndex = 0; jointIndex < document.value["joints"].size(); ++jointIndex)
	{
		const Json& jointDocument = document.value["joints"][jointIndex];
		if (!jointDocument.is_object() || jointDocument.value("index", Ursa::UInt32{UINT32_MAX}) != jointIndex ||
		    !jointDocument.contains("nodeLocalFileId") || !jointDocument["nodeLocalFileId"].is_number_unsigned() ||
		    !jointDocument.contains("name") || !jointDocument["name"].is_string() ||
		    !jointDocument.contains("bindLocalTransform") || !jointDocument.contains("inverseBindMatrix"))
		{
			return Result<std::shared_ptr<const ModelSkeleton>>::Failure("Invalid model skeleton joint at " +
			                                                             path.value.ToString());
		}
		SkeletonJoint joint;
		joint.nodeLocalFileId = jointDocument["nodeLocalFileId"].get<Ursa::UInt64>();
		joint.name = ToUrsaString(jointDocument["name"].get<std::string>());
		if (jointDocument["parentJointIndex"].is_number_unsigned())
		{
			const Ursa::UInt64 parent = jointDocument["parentJointIndex"].get<Ursa::UInt64>();
			if (parent >= jointIndex)
			{
				return Result<std::shared_ptr<const ModelSkeleton>>::Failure(
				    "Model skeleton parent order is invalid at " + path.value.ToString());
			}
			joint.parentJointIndex = static_cast<Ursa::Int32>(parent);
		}
		else if (!jointDocument["parentJointIndex"].is_null())
		{
			return Result<std::shared_ptr<const ModelSkeleton>>::Failure(
			    "Model skeleton parent reference is invalid at " + path.value.ToString());
		}
		auto localTransform = ReadLocalTransform(jointDocument["bindLocalTransform"], path.value.ToString());
		auto inverseBind = ReadMatrix(jointDocument["inverseBindMatrix"], path.value.ToString());
		if (!localTransform || !inverseBind)
		{
			return Result<std::shared_ptr<const ModelSkeleton>>::Failure(!localTransform ? localTransform.error
			                                                                             : inverseBind.error);
		}
		joint.bindLocalTransform = localTransform.value;
		joint.inverseBindMatrix = inverseBind.value;
		skeleton->joints.push_back(std::move(joint));
	}
	std::shared_ptr<const ModelSkeleton> immutableSkeleton = std::move(skeleton);
	skeletons.emplace(key, immutableSkeleton);
	return Result<std::shared_ptr<const ModelSkeleton>>::Success(std::move(immutableSkeleton));
}

Result<std::shared_ptr<const AnimationClip>> ResourceSession::LoadAnimationClip(ResourceId modelGuid,
                                                                                Ursa::UInt64 localFileId)
{
	if (!modelGuid || localFileId == 0)
	{
		return Result<std::shared_ptr<const AnimationClip>>::Failure(
		    "Animation clip requires a nonzero model GUID and local file ID");
	}
	const String key = ResourceCacheKey(modelGuid, localFileId);
	const auto cached = animationClips.find(key);
	if (cached != animationClips.end())
	{
		return Result<std::shared_ptr<const AnimationClip>>::Success(cached->second);
	}
	auto path = ResolveAssetPath(modelGuid, localFileId);
	if (!path)
	{
		return Result<std::shared_ptr<const AnimationClip>>::Failure(path.error);
	}
	auto document = ReadCborArtifact(index.Filesystem(), path.value);
	if (!document)
	{
		return Result<std::shared_ptr<const AnimationClip>>::Failure(document.error);
	}
	if (document.value.value("format", std::string{}) != "UrsaAnimationClip" ||
	    document.value.value("version", Ursa::UInt32{0}) != 1 ||
	    document.value.value("localFileId", Ursa::UInt64{0}) != localFileId || !document.value.contains("name") ||
	    !document.value["name"].is_string() || !document.value.contains("durationSeconds") ||
	    !document.value["durationSeconds"].is_number() || !document.value.contains("channels") ||
	    !document.value["channels"].is_array())
	{
		return Result<std::shared_ptr<const AnimationClip>>::Failure("Invalid animation clip artifact at " +
		                                                             path.value.ToString());
	}
	const Ursa::Float64 duration = document.value["durationSeconds"].get<Ursa::Float64>();
	if (!std::isfinite(duration) || duration < 0.0)
	{
		return Result<std::shared_ptr<const AnimationClip>>::Failure("Animation clip has an invalid duration at " +
		                                                             path.value.ToString());
	}
	auto clip = std::make_shared<AnimationClip>();
	clip->modelGuid = modelGuid;
	clip->localFileId = localFileId;
	clip->name = ToUrsaString(document.value["name"].get<std::string>());
	clip->durationSeconds = duration;
	for (const Json& channelDocument : document.value["channels"])
	{
		if (!channelDocument.is_object() || !channelDocument.contains("nodeLocalFileId") ||
		    !channelDocument["nodeLocalFileId"].is_number_unsigned())
		{
			return Result<std::shared_ptr<const AnimationClip>>::Failure("Invalid animation channel at " +
			                                                             path.value.ToString());
		}
		AnimationNodeChannel channel;
		channel.nodeLocalFileId = channelDocument["nodeLocalFileId"].get<Ursa::UInt64>();
		for (const char* field : {"positionKeys", "scaleKeys"})
		{
			if (!channelDocument.contains(field) || !channelDocument[field].is_array())
			{
				return Result<std::shared_ptr<const AnimationClip>>::Failure("Invalid animation key array at " +
				                                                             path.value.ToString());
			}
			for (const Json& keyDocument : channelDocument[field])
			{
				auto vectorKey = ReadVectorKey(keyDocument, path.value.ToString());
				if (!vectorKey)
				{
					return Result<std::shared_ptr<const AnimationClip>>::Failure(vectorKey.error);
				}
				(field[0] == 'p' ? channel.positions : channel.scales).push_back(std::move(vectorKey.value));
			}
		}
		if (!channelDocument.contains("rotationKeys") || !channelDocument["rotationKeys"].is_array())
		{
			return Result<std::shared_ptr<const AnimationClip>>::Failure("Invalid animation rotation keys at " +
			                                                             path.value.ToString());
		}
		for (const Json& keyDocument : channelDocument["rotationKeys"])
		{
			auto rotationKey = ReadRotationKey(keyDocument, path.value.ToString());
			if (!rotationKey)
			{
				return Result<std::shared_ptr<const AnimationClip>>::Failure(rotationKey.error);
			}
			channel.rotations.push_back(std::move(rotationKey.value));
		}
		clip->channels.push_back(std::move(channel));
	}
	std::shared_ptr<const AnimationClip> immutableClip = std::move(clip);
	animationClips.emplace(key, immutableClip);
	return Result<std::shared_ptr<const AnimationClip>>::Success(std::move(immutableClip));
}

Result<std::shared_ptr<const Shader::ShaderAsset>> ResourceSession::LoadShader(ResourceId id)
{
	Set<String> active;
	return LoadShaderInternal(id, active);
}

Result<std::shared_ptr<const Shader::ShaderAsset>> ResourceSession::LoadShaderInternal(ResourceId id,
                                                                                       Set<String>& active)
{
	const auto key = id.ToString();
	if (active.find(key) != active.end())
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure("Shader resource reference cycle at " + key);
	}
	auto cached = shaders.find(key);
	if (cached != shaders.end())
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(cached->second);
	}
	auto location = ResolveResource(id);
	if (!location)
	{
		if (id == Shader::BuiltinUnlitShaderId())
		{
			auto builtin = std::make_shared<const Shader::ShaderAsset>(Shader::CreateBuiltinUnlitShaderAsset());
			shaders.emplace(key, builtin);
			return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(std::move(builtin));
		}
		if (id == Shader::BuiltinPbrShaderId())
		{
			auto builtin = std::make_shared<const Shader::ShaderAsset>(Shader::CreateBuiltinPbrShaderAsset());
			shaders.emplace(key, builtin);
			return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(std::move(builtin));
		}
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure(location.error);
	}
	if (location.value.kind != ResourceKind::Shader)
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure(
		    "Resource " + key + " is not a Shader asset: " + location.value.path.ToString());
	}
	auto loaded = Shader::LoadShaderAssetResource(index.Filesystem(), location.value.path);
	if (!loaded)
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure(loaded.error);
	}
	if (loaded.value.id != id)
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure("Shader ID changed after indexing: " +
		                                                                   location.value.path.ToString());
	}
	active.insert(key);
	auto mutableShader = std::make_shared<Shader::ShaderAsset>(std::move(loaded.value));
	for (const Shader::ShaderReference& reference : mutableShader->references)
	{
		auto referencedShader = LoadShaderInternal(reference.id, active);
		if (!referencedShader)
		{
			active.erase(key);
			return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure(location.value.path.ToString() + ": " +
			                                                                   referencedShader.error);
		}
		mutableShader->resolvedReferences.push_back({reference, std::move(referencedShader.value)});
	}
	active.erase(key);
	std::shared_ptr<const Shader::ShaderAsset> shader = std::move(mutableShader);
	shaders.emplace(key, shader);
	URSA_LOG_DEBUG("Engine", "Loaded Shader " + key + " from " + location.value.path.ToString());
	return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(std::move(shader));
}

Result<ResourceId> ResourceSession::IdentifyMaterial(const std::shared_ptr<MaterialInstance>& material) const
{
	for (const auto& entry : materials)
	{
		if (entry.second == material)
		{
			return ResourceId::Parse(entry.first);
		}
	}
	return Result<ResourceId>::Failure("Material object is not in this resource session");
}

Result<AssetReference>
ResourceSession::IdentifyMaterialReference(const std::shared_ptr<MaterialInstance>& material) const
{
	for (const auto& entry : materials)
	{
		if (entry.second != material)
		{
			continue;
		}
		const Size separator = entry.first.find(':');
		const String guidText = separator == String::npos ? entry.first : entry.first.substr(0, separator);
		auto guid = ResourceId::Parse(guidText);
		if (!guid)
		{
			return Result<AssetReference>::Failure(guid.error);
		}
		UInt64 localFileId = 0;
		if (separator != String::npos)
		{
			const String localIdText = entry.first.substr(separator + 1);
			const auto result =
			    std::from_chars(localIdText.data(), localIdText.data() + localIdText.size(), localFileId);
			if (result.ec != std::errc{} || result.ptr != localIdText.data() + localIdText.size())
			{
				return Result<AssetReference>::Failure("Material cache key has an invalid local file ID");
			}
		}
		return Result<AssetReference>::Success({guid.value, localFileId});
	}
	return Result<AssetReference>::Failure("Material object is not in this resource session");
}
} // namespace Ursa
