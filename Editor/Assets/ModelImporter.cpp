#include <Assets/ModelImporter.hpp>

#include <Assets/TextureImporter.hpp>
#include <Core/StringInterop.hpp>
#include <Material/Material.hpp>
#include <Serialization/AssetReference.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
#include <Serialization/TextureResource.hpp>
#include <Serialization/MeshResource.hpp>
#include <Animation/AnimationAssets.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <assimp/Importer.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

constexpr Ursa::UInt32 modelImporterVersion = 5;
const String modelImporterId = "ursa.Model";

String Lowered(String value)
{
	for (char& character : value)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character - 'A' + 'a');
		}
	}
	return value;
}

bool IsSupportedModel(const VirtualPath& path)
{
	const auto dot = path.relative.rfind('.');
	if (dot == String::npos)
	{
		return false;
	}
	const String extension = Lowered(path.relative.substr(dot));
	return extension == ".fbx" || extension == ".obj";
}

Ursa::UInt64 StableLocalFileId(const String& key)
{
	Ursa::UInt64 value = 14695981039346656037ull;
	for (unsigned char character : key)
	{
		value = (value ^ character) * 1099511628211ull;
	}
	return value == 0 ? 1 : value;
}

void AppendUint32(Vector<Ursa::UInt8>& output, Ursa::UInt32 value)
{
	for (Ursa::UInt32 byte = 0; byte < 4; ++byte)
	{
		output.push_back(static_cast<Ursa::UInt8>(value >> (byte * 8)));
	}
}

Vector<Ursa::UInt8> EncodeJson(const Json& value)
{
	const std::vector<Ursa::UInt8> encoded = Json::to_cbor(value);
	Vector<Ursa::UInt8> result;
	result.reserve(encoded.size());
	for (Ursa::UInt8 byte : encoded)
	{
		result.push_back(byte);
	}
	return result;
}

Json Vector3Json(const aiVector3D& value)
{
	return Json::array({static_cast<Ursa::Float32>(value.x), static_cast<Ursa::Float32>(value.y),
	                    static_cast<Ursa::Float32>(value.z)});
}

Json MatrixJson(const aiMatrix4x4& value)
{
	return Json::array({static_cast<Ursa::Float32>(value.a1), static_cast<Ursa::Float32>(value.a2),
	                    static_cast<Ursa::Float32>(value.a3), static_cast<Ursa::Float32>(value.a4),
	                    static_cast<Ursa::Float32>(value.b1), static_cast<Ursa::Float32>(value.b2),
	                    static_cast<Ursa::Float32>(value.b3), static_cast<Ursa::Float32>(value.b4),
	                    static_cast<Ursa::Float32>(value.c1), static_cast<Ursa::Float32>(value.c2),
	                    static_cast<Ursa::Float32>(value.c3), static_cast<Ursa::Float32>(value.c4),
	                    static_cast<Ursa::Float32>(value.d1), static_cast<Ursa::Float32>(value.d2),
	                    static_cast<Ursa::Float32>(value.d3), static_cast<Ursa::Float32>(value.d4)});
}

class VfsStream final : public Assimp::IOStream
{
public:
	explicit VfsStream(Vector<Ursa::UInt8> content) : bytes(std::move(content))
	{
	}

	size_t Read(void* buffer, size_t size, size_t count) override
	{
		if (size == 0 || count == 0 || count > (std::numeric_limits<size_t>::max)() / size)
		{
			return 0;
		}
		const size_t requested = size * count;
		const size_t available = bytes.size() - offset;
		const size_t copied = (std::min)(requested, available);
		const size_t completeItems = copied / size;
		const size_t completeBytes = completeItems * size;
		if (completeBytes > 0)
		{
			std::memcpy(buffer, bytes.data() + offset, completeBytes);
			offset += completeBytes;
		}
		return completeItems;
	}

	size_t Write(const void*, size_t, size_t) override
	{
		return 0;
	}

	aiReturn Seek(size_t position, aiOrigin origin) override
	{
		std::int64_t base = 0;
		if (origin == aiOrigin_CUR)
		{
			base = static_cast<std::int64_t>(offset);
		}
		else if (origin == aiOrigin_END)
		{
			base = static_cast<std::int64_t>(bytes.size());
		}
		const std::int64_t target = base + static_cast<std::int64_t>(position);
		if (target < 0 || static_cast<size_t>(target) > bytes.size())
		{
			return aiReturn_FAILURE;
		}
		offset = static_cast<size_t>(target);
		return aiReturn_SUCCESS;
	}

	size_t Tell() const override
	{
		return offset;
	}

	size_t FileSize() const override
	{
		return bytes.size();
	}

	void Flush() override
	{
	}

private:
	Vector<Ursa::UInt8> bytes;
	size_t offset = 0;
};

class VfsIOSystem final : public Assimp::IOSystem
{
public:
	VfsIOSystem(const VirtualFileSystem& selectedFilesystem, VirtualPath selectedSource)
	    : filesystem(selectedFilesystem), source(std::move(selectedSource))
	{
		const auto slash = source.relative.rfind('/');
		sourceDirectory = slash == String::npos ? String{} : source.relative.substr(0, slash);
	}

	bool Exists(const char* file) const override
	{
		auto path = ResolvePath(file);
		if (!path)
		{
			return false;
		}
		auto exists = filesystem.Exists(path.value);
		return exists && exists.value;
	}

	char getOsSeparator() const override
	{
		return '/';
	}

	Assimp::IOStream* Open(const char* file, const char* mode = "rb") override
	{
		if (!file || !mode || std::strchr(mode, 'w') || std::strchr(mode, 'a'))
		{
			return nullptr;
		}
		auto path = ResolvePath(file);
		if (!path)
		{
			lastError = path.error;
			return nullptr;
		}
		auto bytes = filesystem.Read(path.value);
		if (!bytes)
		{
			lastError = bytes.error;
			return nullptr;
		}
		if (path.value != source)
		{
			dependencies.emplace(path.value.ToString(), path.value);
		}
		return new VfsStream(std::move(bytes.value));
	}

	void Close(Assimp::IOStream* stream) override
	{
		delete stream;
	}

	Result<VirtualPath> ResolvePath(const char* requestedPath) const
	{
		if (!requestedPath || !*requestedPath)
		{
			return Result<VirtualPath>::Failure("Assimp requested an empty virtual path");
		}
		String requested = ToUrsaString(requestedPath);
		for (char& character : requested)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}
		if (requested.front() == '/' || requested.find(':') != String::npos)
		{
			return Result<VirtualPath>::Failure("Assimp path is absolute and cannot be resolved through VFS: " +
			                                    requested);
		}
		String candidate;
		if (requested == source.relative ||
		    (!sourceDirectory.empty() && requested.compare(0, sourceDirectory.size() + 1, sourceDirectory + "/") == 0))
		{
			candidate = requested;
		}
		else
		{
			String base = ToUrsaString(CurrentDirectory());
			if (base.empty())
			{
				base = sourceDirectory;
			}
			candidate = base.empty() ? requested : base + "/" + requested;
		}
		Vector<String> segments;
		Ursa::Size cursor = 0;
		while (cursor < candidate.size())
		{
			const auto slash = candidate.find('/', cursor);
			const Ursa::Size end = slash == String::npos ? candidate.size() : slash;
			const String segment = candidate.substr(cursor, end - cursor);
			if (!segment.empty() && segment != ".")
			{
				if (segment == "..")
				{
					if (segments.empty())
					{
						return Result<VirtualPath>::Failure("Assimp dependency escapes the VFS root: " + requested);
					}
					segments.pop_back();
				}
				else
				{
					segments.push_back(segment);
				}
			}
			if (slash == String::npos)
			{
				break;
			}
			cursor = slash + 1;
		}
		String normalized;
		for (const String& segment : segments)
		{
			if (!normalized.empty())
			{
				normalized.push_back('/');
			}
			normalized += segment;
		}
		if (normalized.empty())
		{
			return Result<VirtualPath>::Failure("Assimp resolved an empty path for " + requested);
		}
		return VirtualPath::Parse(source.scheme + ":" + normalized);
	}

	Vector<VirtualPath> Dependencies() const
	{
		Vector<VirtualPath> result;
		for (const auto& dependency : dependencies)
		{
			result.push_back(dependency.second);
		}
		std::sort(result.begin(), result.end(),
		          [](const VirtualPath& left, const VirtualPath& right) { return left.ToString() < right.ToString(); });
		return result;
	}

	const String& LastError() const
	{
		return lastError;
	}

private:
	const VirtualFileSystem& filesystem;
	VirtualPath source;
	String sourceDirectory;
	Map<String, VirtualPath> dependencies;
	String lastError;
};

Result<Vector<Ursa::UInt8>> EncodeScene(const Json& document)
{
	try
	{
		const auto bytes = Json::to_cbor(document);
		Vector<Ursa::UInt8> result;
		result.reserve(bytes.size());
		for (Ursa::UInt8 byte : bytes)
		{
			result.push_back(byte);
		}
		return Result<Vector<Ursa::UInt8>>::Success(std::move(result));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(ToUrsaString(error.what()));
	}
}

Result<Vector<String>> ReadModelDiagnostics(const VirtualFileSystem& filesystem, const VirtualPath& modelPath)
{
	auto bytes = filesystem.Read(modelPath);
	if (!bytes)
	{
		return Result<Vector<String>>::Failure(bytes.error);
	}
	try
	{
		const Json document = Json::from_cbor(bytes.value.begin(), bytes.value.end());
		Vector<String> diagnostics;
		if (!document.contains("diagnostics") || !document["diagnostics"].is_array())
		{
			return Result<Vector<String>>::Success(std::move(diagnostics));
		}
		for (const Json& diagnostic : document["diagnostics"])
		{
			if (diagnostic.is_string())
			{
				diagnostics.push_back(ToUrsaString(diagnostic.get<std::string>()));
			}
		}
		return Result<Vector<String>>::Success(std::move(diagnostics));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<String>>::Failure("Cannot read model diagnostics from " + modelPath.ToString() + ": " +
		                                       ToUrsaString(error.what()));
	}
}

MeshData GeometryData(const aiMesh& mesh)
{
	MeshData data;
	for (UInt32 i = 0; i < mesh.mNumVertices; ++i)
	{
		data.positions.push_back({mesh.mVertices[i].x, mesh.mVertices[i].y, mesh.mVertices[i].z});
		if (mesh.HasNormals())
		{
			data.normals.push_back({mesh.mNormals[i].x, mesh.mNormals[i].y, mesh.mNormals[i].z});
		}
		if (mesh.HasTextureCoords(0))
		{
			data.uv0.push_back({mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y});
		}
		if (mesh.HasVertexColors(0))
		{
			const auto& c = mesh.mColors[0][i];
			data.colors.push_back({c.r, c.g, c.b, c.a});
		}
		if (mesh.HasTangentsAndBitangents())
		{
			const auto& t = mesh.mTangents[i];
			const auto cross = mesh.mNormals[i] ^ t;
			const Float32 sign = (cross * mesh.mBitangents[i]) < 0 ? -1.0f : 1.0f;
			data.tangents.push_back({t.x, t.y, t.z, sign});
		}
	}
	for (UInt32 i = 0; i < mesh.mNumFaces; ++i)
	{
		const auto& face = mesh.mFaces[i];
		for (UInt32 j = 0; j < face.mNumIndices; ++j)
		{
			data.indices.push_back(face.mIndices[j]);
		}
	}
	return data;
}

String MaterialName(const aiMaterial& material, Ursa::UInt32 index)
{
	aiString name;
	if (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0)
	{
		return ToUrsaString(name.C_Str());
	}
	return "Material" + ToUrsaString(std::to_string(index));
}

bool IsSupportedMaterialProperty(const String& key)
{
	static const Set<String> supported = {"$mat.name",
	                                      "$clr.base",
	                                      "$clr.diffuse",
	                                      "$clr.emissive",
	                                      "$mat.metallicFactor",
	                                      "$mat.roughnessFactor",
	                                      "$mat.opacity",
	                                      "$mat.shininess",
	                                      "$mat.twosided",
	                                      "$mat.shadingm",
	                                      "$tex.file",
	                                      "$tex.uvwsrc",
	                                      "$tex.mapping",
	                                      "$tex.blend",
	                                      "$tex.op",
	                                      "$tex.mapmodeu",
	                                      "$tex.mapmodev",
	                                      "$tex.mappingname",
	                                      "$mat.gltf.alphaMode",
	                                      "$mat.gltf.alphaCutoff",
	                                      "$mat.gltf.pbrMetallicRoughness.baseColorFactor",
	                                      "$mat.gltf.pbrMetallicRoughness.metallicFactor",
	                                      "$mat.gltf.pbrMetallicRoughness.roughnessFactor"};
	return supported.find(key) != supported.end();
}

void AddUnsupportedMaterialDiagnostics(const aiMaterial& material, const String& name, Vector<String>& diagnostics)
{
	for (Ursa::UInt32 index = 0; index < material.mNumProperties; ++index)
	{
		const aiMaterialProperty* property = material.mProperties[index];
		if (!property)
		{
			continue;
		}
		const String key = ToUrsaString(property->mKey.C_Str());
		if (!IsSupportedMaterialProperty(key))
		{
			diagnostics.push_back("Material " + name + ": unsupported source property '" + key + "' was ignored");
		}
	}
}

Result<ResourceId> MaterialTextureId(const aiScene& scene, aiMaterial& material, aiTextureType textureType,
                                     const char* slot, ResourceId modelGuid, AssetDatabase& database,
                                     VfsIOSystem& ioSystem, Vector<VirtualPath>& dependencies,
                                     Vector<ImportedArtifact>& artifacts, Map<String, Ursa::UInt64>& embeddedIds)
{
	if (material.GetTextureCount(textureType) == 0)
	{
		return Result<ResourceId>::Failure("No texture assigned");
	}
	aiString texturePath;
	if (material.GetTexture(textureType, 0, &texturePath) != AI_SUCCESS)
	{
		return Result<ResourceId>::Failure("Cannot read material texture path");
	}
	const String path = ToUrsaString(texturePath.C_Str());
	const aiTexture* embedded = path.empty() ? nullptr : scene.GetEmbeddedTexture(texturePath.C_Str());
	if ((!path.empty() && path.front() == '*') || embedded)
	{
		if (!embedded)
		{
			return Result<ResourceId>::Failure("Missing embedded texture " + path);
		}
		const auto known = embeddedIds.find(path);
		if (known != embeddedIds.end())
		{
			return Result<ResourceId>::Success(modelGuid);
		}
		Ursa::UInt32 width = embedded->mWidth;
		Ursa::UInt32 height = embedded->mHeight;
		Vector<Ursa::UInt8> rgba;
		if (embedded->mHeight == 0)
		{
			if (embedded->mWidth > static_cast<Ursa::UInt32>(std::numeric_limits<int>::max()))
			{
				return Result<ResourceId>::Failure("Embedded texture exceeds decoder size limit: " + path);
			}
			int decodedWidth = 0;
			int decodedHeight = 0;
			int channels = 0;
			std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
			    stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(embedded->pcData),
			                          static_cast<int>(embedded->mWidth), &decodedWidth, &decodedHeight, &channels,
			                          STBI_rgb_alpha),
			    &stbi_image_free);
			if (!decoded || decodedWidth <= 0 || decodedHeight <= 0)
			{
				const char* reason = stbi_failure_reason();
				return Result<ResourceId>::Failure("Cannot decode embedded texture " + path +
				                                   (reason ? ": " + ToUrsaString(reason) : String{}));
			}
			width = static_cast<Ursa::UInt32>(decodedWidth);
			height = static_cast<Ursa::UInt32>(decodedHeight);
			rgba.resize(static_cast<Ursa::Size>(width) * height * 4);
			std::copy(decoded.get(), decoded.get() + rgba.size(), rgba.begin());
		}
		else
		{
			if (static_cast<Ursa::UInt64>(width) * height > 256ull * 1024 * 1024 / 4)
			{
				return Result<ResourceId>::Failure("Embedded texture exceeds size limit: " + path);
			}
			rgba.resize(static_cast<Ursa::Size>(width) * height * 4);
			for (Ursa::Size index = 0; index < static_cast<Ursa::Size>(width) * height; ++index)
			{
				rgba[index * 4] = embedded->pcData[index].r;
				rgba[index * 4 + 1] = embedded->pcData[index].g;
				rgba[index * 4 + 2] = embedded->pcData[index].b;
				rgba[index * 4 + 3] = embedded->pcData[index].a;
			}
		}
		const TextureColorSpace colorSpace = String(slot) == "baseColor" || String(slot) == "emissive"
		                                         ? TextureColorSpace::Srgb
		                                         : TextureColorSpace::Linear;
		auto texture = MaterialTexture::Create(width, height, colorSpace, std::move(rgba));
		if (!texture)
		{
			return Result<ResourceId>::Failure(texture.error);
		}
		VirtualFileSystem staging;
		auto memory = std::make_shared<MemoryFileProvider>();
		auto mounted = staging.MountProvider(projectScheme, memory, MountAccess::ReadWrite);
		auto texturePathInMemory = VirtualPath::Parse("prj:Embedded.ursatex");
		if (!mounted || !texturePathInMemory)
		{
			return Result<ResourceId>::Failure(!mounted ? mounted.error : texturePathInMemory.error);
		}
		auto saved = SaveTextureResource(staging, texturePathInMemory.value, modelGuid, *texture.value);
		if (!saved)
		{
			return Result<ResourceId>::Failure(saved.error);
		}
		auto bytes = staging.Read(texturePathInMemory.value);
		if (!bytes)
		{
			return Result<ResourceId>::Failure(bytes.error);
		}
		const String localKey = String("texture:") + path;
		const Ursa::UInt64 localFileId = StableLocalFileId(localKey);
		embeddedIds.emplace(path, localFileId);
		artifacts.push_back({localFileId, "Texture", std::move(bytes.value)});
		return Result<ResourceId>::Success(modelGuid);
	}

	auto resolved = ioSystem.ResolvePath(path.c_str());
	if (!resolved)
	{
		return Result<ResourceId>::Failure(resolved.error);
	}
	auto metadata = database.Metadata(resolved.value);
	if (!metadata)
	{
		return Result<ResourceId>::Failure("Texture dependency has no asset metadata at " + resolved.value.ToString() +
		                                   ": " + metadata.error);
	}
	auto textureSettings = TextureImporter::Settings(database, resolved.value);
	if (!textureSettings)
	{
		return Result<ResourceId>::Failure(textureSettings.error);
	}
	if (metadata.value.importer.empty())
	{
		const String slotName = slot;
		if (slotName == "normal")
		{
			textureSettings.value.type = TextureImportType::NormalMap;
			textureSettings.value.sRgb = false;
		}
		else if (slotName != "baseColor" && slotName != "emissive")
		{
			textureSettings.value.type = TextureImportType::LinearData;
			textureSettings.value.sRgb = false;
		}
	}
	auto imported = TextureImporter::Import(database, resolved.value, textureSettings.value);
	if (!imported)
	{
		return Result<ResourceId>::Failure(imported.error);
	}
	dependencies.push_back(resolved.value);
	return Result<ResourceId>::Success(imported.value.guid);
}

Result<Json> ConvertMaterial(aiScene const& scene, Ursa::UInt32 index, ResourceId modelGuid, AssetDatabase& database,
                             VfsIOSystem& ioSystem, Vector<VirtualPath>& dependencies,
                             Vector<ImportedArtifact>& artifacts, Map<String, Ursa::UInt64>& embeddedIds,
                             const Map<Ursa::UInt32, Ursa::UInt64>& materialIds, bool importTextures,
                             Vector<String>& diagnostics)
{
	aiMaterial* material = scene.mMaterials[index];
	if (!material)
	{
		return Result<Json>::Failure("Assimp returned a null material at index " + ToUrsaString(std::to_string(index)));
	}
	const String name = MaterialName(*material, index);
	AddUnsupportedMaterialDiagnostics(*material, name, diagnostics);
	const Ursa::UInt64 localFileId = materialIds.at(index);
	aiColor4D baseColor(1, 1, 1, 1);
	if (material->Get(AI_MATKEY_BASE_COLOR, baseColor) != AI_SUCCESS)
	{
		material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);
	}
	aiColor3D emissive(0, 0, 0);
	material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
	float metallic = 0.0f;
	float roughness = 1.0f;
	float opacity = 1.0f;
	material->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
	material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
	material->Get(AI_MATKEY_OPACITY, opacity);
	Json textures = Json::object();
	const aiTextureType types[] = {aiTextureType_BASE_COLOR,        aiTextureType_DIFFUSE,
	                               aiTextureType_NORMALS,           aiTextureType_METALNESS,
	                               aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_AMBIENT_OCCLUSION,
	                               aiTextureType_EMISSIVE};
	const char* slots[] = {"baseColor",         "baseColor", "normal",  "metallicRoughness",
	                       "metallicRoughness", "occlusion", "emissive"};
	for (Ursa::Size slotIndex = 0; importTextures && slotIndex < std::size(types); ++slotIndex)
	{
		const aiTextureType type = types[slotIndex];
		const String slot = slots[slotIndex];
		if (textures.contains(ToStdString(slot)) || material->GetTextureCount(type) == 0)
		{
			continue;
		}
		auto texture = MaterialTextureId(scene, *material, type, slots[slotIndex], modelGuid, database, ioSystem,
		                                 dependencies, artifacts, embeddedIds);
		if (texture)
		{
			Ursa::UInt64 localTextureId = 0;
			aiString texturePath;
			if (material->GetTexture(type, 0, &texturePath) == AI_SUCCESS && texturePath.length > 0 &&
			    (texturePath.C_Str()[0] == '*' || scene.GetEmbeddedTexture(texturePath.C_Str())))
			{
				const auto found = embeddedIds.find(ToUrsaString(texturePath.C_Str()));
				if (found != embeddedIds.end())
				{
					localTextureId = found->second;
				}
			}
			textures[ToStdString(slot)] = AssetReferenceToJson({texture.value, localTextureId});
		}
		else if (texture.error != "No texture assigned")
		{
			return Result<Json>::Failure("Material " + name + " texture " + slot + ": " + texture.error);
		}
	}
	MaterialInstance importedMaterial(MaterialType::PbrMetallicRoughness);
	auto setBaseColor = importedMaterial.SetBaseColor({baseColor.r, baseColor.g, baseColor.b, baseColor.a});
	auto setMetallic = importedMaterial.SetMetallic(metallic);
	auto setRoughness = importedMaterial.SetRoughness(roughness);
	auto setEmissive = importedMaterial.SetEmissive({emissive.r, emissive.g, emissive.b, 1.0f});
	auto setAlphaMode = importedMaterial.SetAlphaMode(opacity < 0.999f ? AlphaMode::Blend : AlphaMode::Opaque);
	auto setShader = importedMaterial.SetShader(Shader::BuiltinPbrShaderId());
	if (!setBaseColor || !setMetallic || !setRoughness || !setEmissive || !setAlphaMode || !setShader)
	{
		return Result<Json>::Failure("Cannot map source material " + name + " to the built-in PBR Shader");
	}
	ArchiveWriter writer;
	auto serialized = importedMaterial.Serialize(writer);
	if (!serialized)
	{
		return Result<Json>::Failure("Cannot serialize imported material " + name + ": " + serialized.error);
	}
	auto materialDocument = ArchiveToJson(writer);
	if (!materialDocument)
	{
		return Result<Json>::Failure(materialDocument.error);
	}
	materialDocument.value["textures"] = std::move(textures);
	Json record = {{"localFileId", localFileId},
	               {"kind", "Material"},
	               {"name", ToStdString(name)},
	               {"material", std::move(materialDocument.value)}};
	return Result<Json>::Success(std::move(record));
}
} // namespace

Result<Vector<ModelSkinInfluence>> ModelImporter::NormalizeSkinInfluences(Ursa::UInt32 vertexCount,
                                                                          const Vector<RawSkinInfluence>& influences,
                                                                          Ursa::UInt32 maximumInfluences)
{
	if (vertexCount == 0 || maximumInfluences == 0 || maximumInfluences > 4)
	{
		return Result<Vector<ModelSkinInfluence>>::Failure("Invalid skin vertex count or influence limit");
	}
	Vector<Vector<RawSkinInfluence>> perVertex(vertexCount);
	for (const RawSkinInfluence& influence : influences)
	{
		if (influence.vertexIndex >= vertexCount || influence.jointIndex > std::numeric_limits<Ursa::UInt16>::max() ||
		    !std::isfinite(influence.weight) || influence.weight < 0.0f)
		{
			return Result<Vector<ModelSkinInfluence>>::Failure(
			    "Imported skin weight has an invalid vertex, joint, or value");
		}
		if (influence.weight > 0.0f)
		{
			perVertex[influence.vertexIndex].push_back(influence);
		}
	}
	Vector<ModelSkinInfluence> normalized(vertexCount);
	for (Ursa::UInt32 vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
	{
		auto& vertexInfluences = perVertex[vertexIndex];
		std::sort(
		    vertexInfluences.begin(), vertexInfluences.end(),
		    [](const RawSkinInfluence& left, const RawSkinInfluence& right)
		    { return left.weight == right.weight ? left.jointIndex < right.jointIndex : left.weight > right.weight; });
		if (vertexInfluences.size() > maximumInfluences)
		{
			vertexInfluences.resize(maximumInfluences);
		}
		Ursa::Float32 totalWeight = 0.0f;
		for (const RawSkinInfluence& influence : vertexInfluences)
		{
			totalWeight += influence.weight;
		}
		if (!std::isfinite(totalWeight))
		{
			return Result<Vector<ModelSkinInfluence>>::Failure("Imported skin weights overflowed during normalization");
		}
		if (totalWeight == 0.0f)
		{
			continue;
		}
		for (Ursa::Size index = 0; index < vertexInfluences.size(); ++index)
		{
			normalized[vertexIndex].jointIndices[index] = static_cast<Ursa::UInt16>(vertexInfluences[index].jointIndex);
			normalized[vertexIndex].weights[index] = vertexInfluences[index].weight / totalWeight;
		}
	}
	return Result<Vector<ModelSkinInfluence>>::Success(std::move(normalized));
}

nlohmann::json ModelImportSettings::ToJson() const
{
	return {{"scaleFactor", scaleFactor},
	        {"generateNormals", generateNormals},
	        {"generateTangents", generateTangents},
	        {"flipUvs", flipUvs},
	        {"importMaterials", importMaterials},
	        {"importTextures", importTextures},
	        {"maxBoneInfluences", maxBoneInfluences}};
}

Result<ModelImportSettings> ModelImportSettings::FromJson(const nlohmann::json& value)
{
	if (!value.is_object())
	{
		return Result<ModelImportSettings>::Failure("Model import settings must be an object");
	}
	ModelImportSettings settings;
	if (value.empty())
	{
		return Result<ModelImportSettings>::Success(settings);
	}
	if (!value.contains("scaleFactor") || !value["scaleFactor"].is_number() || !value.contains("generateNormals") ||
	    !value["generateNormals"].is_boolean() || !value.contains("generateTangents") ||
	    !value["generateTangents"].is_boolean() || !value.contains("flipUvs") || !value["flipUvs"].is_boolean() ||
	    !value.contains("importMaterials") || !value["importMaterials"].is_boolean() ||
	    !value.contains("importTextures") || !value["importTextures"].is_boolean() ||
	    (value.contains("maxBoneInfluences") && !value["maxBoneInfluences"].is_number_unsigned()))
	{
		return Result<ModelImportSettings>::Failure("Model import settings contain invalid or missing fields");
	}
	settings.scaleFactor = value["scaleFactor"].get<Ursa::Float32>();
	settings.generateNormals = value["generateNormals"].get<bool>();
	settings.generateTangents = value["generateTangents"].get<bool>();
	settings.flipUvs = value["flipUvs"].get<bool>();
	settings.importMaterials = value["importMaterials"].get<bool>();
	settings.importTextures = value["importTextures"].get<bool>();
	if (value.contains("maxBoneInfluences"))
	{
		settings.maxBoneInfluences = value["maxBoneInfluences"].get<Ursa::UInt32>();
	}
	if (!std::isfinite(settings.scaleFactor) || settings.scaleFactor <= 0.0f || settings.scaleFactor > 10000.0f)
	{
		return Result<ModelImportSettings>::Failure("Model import scale factor is invalid");
	}
	if (settings.maxBoneInfluences == 0 || settings.maxBoneInfluences > 4)
	{
		return Result<ModelImportSettings>::Failure("Model bone influence count must be between 1 and 4");
	}
	return Result<ModelImportSettings>::Success(settings);
}

Result<ModelImportSettings> ModelImporter::Settings(const AssetDatabase& database, const VirtualPath& source)
{
	auto metadata = database.Metadata(source);
	if (!metadata)
	{
		return Result<ModelImportSettings>::Failure(metadata.error);
	}
	return metadata.value.importer == modelImporterId ? ModelImportSettings::FromJson(metadata.value.importSettings)
	                                                  : Result<ModelImportSettings>::Success(ModelImportSettings{});
}

Result<void> ModelImporter::SetSettings(AssetDatabase& database, const VirtualPath& source,
                                        const ModelImportSettings& settings)
{
	if (!IsSupportedModel(source) || !std::isfinite(settings.scaleFactor) || settings.scaleFactor <= 0.0f ||
	    settings.scaleFactor > 10000.0f || settings.maxBoneInfluences == 0 || settings.maxBoneInfluences > 4)
	{
		return Result<void>::Failure("Invalid FBX/OBJ model import settings at " + source.ToString());
	}
	return database.SetImporterSettings(source, modelImporterId, modelImporterVersion, settings.ToJson());
}

Result<ModelImportResult> ModelImporter::Import(AssetDatabase& database, const VirtualPath& source,
                                                const ModelImportSettings& settings, bool force)
{
	if (!IsSupportedModel(source))
	{
		return Result<ModelImportResult>::Failure("Model importer supports FBX and OBJ files: " + source.ToString());
	}
	if (!std::isfinite(settings.scaleFactor) || settings.scaleFactor <= 0.0f || settings.scaleFactor > 10000.0f ||
	    settings.maxBoneInfluences == 0 || settings.maxBoneInfluences > 4)
	{
		return Result<ModelImportResult>::Failure("Invalid model import scale factor at " + source.ToString());
	}
	auto metadata = database.Metadata(source);
	if (!metadata)
	{
		return Result<ModelImportResult>::Failure(metadata.error);
	}
	const Json serializedSettings = settings.ToJson();
	if ((metadata.value.importer != modelImporterId || metadata.value.importerVersion != modelImporterVersion ||
	     metadata.value.importSettings != serializedSettings) &&
	    database.Filesystem().CheckWritable(source))
	{
		auto savedSettings =
		    database.SetImporterSettings(source, modelImporterId, modelImporterVersion, serializedSettings);
		if (!savedSettings)
		{
			return Result<ModelImportResult>::Failure(savedSettings.error);
		}
		metadata = database.Metadata(source);
		if (!metadata)
		{
			return Result<ModelImportResult>::Failure(metadata.error);
		}
	}
	const ResourceId guid = metadata.value.guid;
	auto dependencies = database.ImportDependencies(source, modelImporterId);
	if (!dependencies)
	{
		return Result<ModelImportResult>::Failure(dependencies.error);
	}
	auto cache = database.CheckImport(source, modelImporterId, modelImporterVersion, "model-v5", serializedSettings,
	                                  dependencies.value);
	if (!force && cache && cache.value.upToDate)
	{
		auto artifactPath = database.Resolve(guid, 0);
		if (!artifactPath)
		{
			return Result<ModelImportResult>::Failure(artifactPath.error);
		}
		auto diagnostics = ReadModelDiagnostics(database.Filesystem(), artifactPath.value);
		if (!diagnostics)
		{
			return Result<ModelImportResult>::Failure(diagnostics.error);
		}
		return Result<ModelImportResult>::Success({guid, std::move(artifactPath.value), cache.value.artifacts,
		                                           std::move(dependencies.value), true, std::move(diagnostics.value)});
	}

	Assimp::Importer importer;
	auto* ioSystem = new VfsIOSystem(database.Filesystem(), source);
	importer.SetIOHandler(ioSystem);
	importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, settings.scaleFactor);
	unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality |
	                     aiProcess_ValidateDataStructure | aiProcess_SortByPType | aiProcess_GlobalScale |
	                     aiProcess_FlipWindingOrder;
	if (settings.generateNormals)
	{
		flags |= aiProcess_GenSmoothNormals;
	}
	if (settings.generateTangents)
	{
		flags |= aiProcess_CalcTangentSpace;
	}
	if (settings.flipUvs)
	{
		flags |= aiProcess_FlipUVs;
	}
	const aiScene* scene = importer.ReadFile(ToStdString(source.relative), flags);
	if (!scene || !scene->mRootNode)
	{
		const String diagnostic =
		    ioSystem->LastError().empty() ? ToUrsaString(importer.GetErrorString()) : ioSystem->LastError();
		return Result<ModelImportResult>::Failure("Cannot import " + source.ToString() + ": " + diagnostic);
	}
	Map<const aiNode*, Ursa::UInt64> nodeIdsByPointer;
	Map<const aiNode*, String> nodePathsByPointer;
	Map<String, Vector<const aiNode*>> nodesByName;
	Map<const aiNode*, aiMatrix4x4> globalTransforms;
	Vector<const aiNode*> orderedNodes;
	Set<Ursa::UInt64> uniqueNodeIds;
	auto indexNodes = [&](auto&& self, const aiNode& node, const String& parentPath, Ursa::UInt32 childIndex,
	                      const aiMatrix4x4& parentGlobal) -> Result<void>
	{
		const String name = ToUrsaString(node.mName.C_Str());
		const String nodePath = parentPath + "/" + name + "[" + ToUrsaString(std::to_string(childIndex)) + "]";
		const Ursa::UInt64 localFileId = StableLocalFileId("node:" + nodePath);
		if (!uniqueNodeIds.insert(localFileId).second)
		{
			return Result<void>::Failure("Model node local-file-ID collision at " + nodePath);
		}
		const aiMatrix4x4 global = parentGlobal * node.mTransformation;
		nodeIdsByPointer.emplace(&node, localFileId);
		nodePathsByPointer.emplace(&node, nodePath);
		nodesByName[name].push_back(&node);
		globalTransforms.emplace(&node, global);
		orderedNodes.push_back(&node);
		for (Ursa::UInt32 index = 0; index < node.mNumChildren; ++index)
		{
			auto indexed = self(self, *node.mChildren[index], nodePath, index, global);
			if (!indexed)
			{
				return indexed;
			}
		}
		return {};
	};
	auto indexedNodes = indexNodes(indexNodes, *scene->mRootNode, String{}, 0, aiMatrix4x4());
	if (!indexedNodes)
	{
		return Result<ModelImportResult>::Failure(indexedNodes.error + " at " + source.ToString());
	}
	Vector<VirtualPath> importedDependencies = ioSystem->Dependencies();
	Vector<ImportedArtifact> artifacts;
	Map<String, Ursa::UInt64> embeddedIds;
	Map<Ursa::UInt32, Ursa::UInt64> materialIds;
	Vector<Ursa::UInt64> allMaterialIds;
	Vector<String> diagnostics;
	const Ursa::UInt64 skeletonLocalFileId = StableLocalFileId("skeleton:root");
	Map<String, const aiBone*> bonesByName;
	Map<String, const aiNode*> boneNodesByName;
	Set<const aiNode*> requiredJointNodes;
	Map<const aiNode*, aiMatrix4x4> inverseBindByNode;
	for (Ursa::UInt32 meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
	{
		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!mesh)
		{
			return Result<ModelImportResult>::Failure("Assimp returned a null mesh at index " +
			                                          ToUrsaString(std::to_string(meshIndex)));
		}
		for (Ursa::UInt32 boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
		{
			const aiBone* bone = mesh->mBones[boneIndex];
			if (!bone)
			{
				return Result<ModelImportResult>::Failure("Assimp returned a null bone at mesh " +
				                                          ToUrsaString(std::to_string(meshIndex)));
			}
			const String name = ToUrsaString(bone->mName.C_Str());
			const auto nodeMatches = nodesByName.find(name);
			if (nodeMatches == nodesByName.end() || nodeMatches->second.empty())
			{
				return Result<ModelImportResult>::Failure("Skin bone '" + name + "' has no matching model node at " +
				                                          source.ToString());
			}
			if (nodeMatches->second.size() != 1)
			{
				return Result<ModelImportResult>::Failure("Skin bone '" + name + "' matches multiple model nodes at " +
				                                          source.ToString());
			}
			const aiNode* boneNode = nodeMatches->second[0];
			const auto previous = bonesByName.find(name);
			if (previous != bonesByName.end())
			{
				if (MatrixJson(previous->second->mOffsetMatrix) != MatrixJson(bone->mOffsetMatrix))
				{
					diagnostics.push_back("Skin bone " + name +
					                      " uses different bind matrices across meshes; first bind matrix is retained");
				}
			}
			else
			{
				bonesByName.emplace(name, bone);
				boneNodesByName.emplace(name, boneNode);
				inverseBindByNode.emplace(boneNode, bone->mOffsetMatrix);
			}
			for (const aiNode* jointNode = boneNode; jointNode; jointNode = jointNode->mParent)
			{
				requiredJointNodes.insert(jointNode);
			}
		}
	}
	for (Ursa::UInt32 animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
	{
		const aiAnimation* animation = scene->mAnimations[animationIndex];
		if (!animation)
		{
			return Result<ModelImportResult>::Failure("Assimp returned a null animation at index " +
			                                          ToUrsaString(std::to_string(animationIndex)));
		}
		for (Ursa::UInt32 channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
		{
			const aiNodeAnim* channel = animation->mChannels[channelIndex];
			if (!channel)
			{
				return Result<ModelImportResult>::Failure("Animation contains a null node channel at " +
				                                          source.ToString());
			}
			const String nodeName = ToUrsaString(channel->mNodeName.C_Str());
			const auto nodeMatches = nodesByName.find(nodeName);
			if (nodeMatches == nodesByName.end() || nodeMatches->second.size() != 1)
			{
				return Result<ModelImportResult>::Failure("Animation node '" + nodeName +
				                                          "' is missing or ambiguous at " + source.ToString());
			}
			for (const aiNode* jointNode = nodeMatches->second[0]; jointNode; jointNode = jointNode->mParent)
			{
				requiredJointNodes.insert(jointNode);
			}
		}
	}
	Map<const aiNode*, Ursa::UInt32> jointIndicesByNode;
	Json skeletonJoints = Json::array();
	const bool hasSkeleton = !requiredJointNodes.empty();
	if (hasSkeleton)
	{
		for (const aiNode* jointNode : orderedNodes)
		{
			if (requiredJointNodes.find(jointNode) == requiredJointNodes.end())
			{
				continue;
			}
			Json parentIndex = nullptr;
			for (const aiNode* parentNode = jointNode->mParent; parentNode; parentNode = parentNode->mParent)
			{
				const auto parent = jointIndicesByNode.find(parentNode);
				if (parent != jointIndicesByNode.end())
				{
					parentIndex = parent->second;
					break;
				}
			}
			aiMatrix4x4 inverseBind;
			const auto importedBind = inverseBindByNode.find(jointNode);
			if (importedBind != inverseBindByNode.end())
			{
				inverseBind = importedBind->second;
			}
			else
			{
				inverseBind = globalTransforms.at(jointNode);
				inverseBind.Inverse();
			}
			const Ursa::UInt32 jointIndex = static_cast<Ursa::UInt32>(skeletonJoints.size());
			jointIndicesByNode.emplace(jointNode, jointIndex);
			const String name = ToUrsaString(jointNode->mName.C_Str());
			aiVector3D bindScale;
			aiQuaternion bindRotation;
			aiVector3D bindPosition;
			jointNode->mTransformation.Decompose(bindScale, bindRotation, bindPosition);
			Json bindLocalTransform = {{"position", {bindPosition.x, bindPosition.y, bindPosition.z}},
			                           {"rotation", {bindRotation.x, bindRotation.y, bindRotation.z, bindRotation.w}},
			                           {"scale", {bindScale.x, bindScale.y, bindScale.z}}};
			skeletonJoints.push_back({{"index", jointIndex},
			                          {"name", ToStdString(name)},
			                          {"nodeLocalFileId", nodeIdsByPointer.at(jointNode)},
			                          {"parentJointIndex", std::move(parentIndex)},
			                          {"bindLocalTransform", std::move(bindLocalTransform)},
			                          {"inverseBindMatrix", MatrixJson(inverseBind)}});
		}
		Json skeletonDocument = {{"format", "UrsaSkeleton"},
		                         {"version", 1},
		                         {"localFileId", skeletonLocalFileId},
		                         {"joints", std::move(skeletonJoints)}};
		auto encodedSkeleton = EncodeScene(skeletonDocument);
		if (!encodedSkeleton)
		{
			return Result<ModelImportResult>::Failure(encodedSkeleton.error);
		}
		artifacts.push_back({skeletonLocalFileId, "Skeleton", std::move(encodedSkeleton.value)});
	}
	for (Ursa::UInt32 index = 0; index < scene->mNumMaterials; ++index)
	{
		const String name = MaterialName(*scene->mMaterials[index], index);
		const Ursa::UInt64 localFileId =
		    StableLocalFileId("material:" + ToUrsaString(std::to_string(index)) + ":" + name);
		if (std::find(allMaterialIds.begin(), allMaterialIds.end(), localFileId) != allMaterialIds.end())
		{
			return Result<ModelImportResult>::Failure("Model material local-file-ID collision at " + source.ToString());
		}
		materialIds.emplace(index, localFileId);
		allMaterialIds.push_back(localFileId);
	}

	Json materials = Json::array();
	if (settings.importMaterials)
	{
		for (Ursa::UInt32 index = 0; index < scene->mNumMaterials; ++index)
		{
			auto material = ConvertMaterial(*scene, index, guid, database, *ioSystem, importedDependencies, artifacts,
			                                embeddedIds, materialIds, settings.importTextures, diagnostics);
			if (!material)
			{
				return Result<ModelImportResult>::Failure(material.error + " at " + source.ToString());
			}
			materials.push_back(material.value);
		}
	}

	Json meshMaterialBindings = Json::object();
	Vector<Ursa::UInt64> meshIds(scene->mNumMeshes);
	Map<Ursa::UInt32, Ursa::UInt64> meshIdMap;
	for (Ursa::UInt32 index = 0; index < scene->mNumMeshes; ++index)
	{
		const aiMesh* mesh = scene->mMeshes[index];
		const String name = mesh && mesh->mName.length > 0 ? ToUrsaString(mesh->mName.C_Str())
		                                                   : "Mesh" + ToUrsaString(std::to_string(index));
		const Ursa::UInt64 localFileId = StableLocalFileId("mesh:" + ToUrsaString(std::to_string(index)) + ":" + name);
		meshIds[index] = localFileId;
		meshIdMap.emplace(index, localFileId);
		if (!mesh)
		{
			return Result<ModelImportResult>::Failure("Assimp returned a null mesh at index " +
			                                          ToUrsaString(std::to_string(index)));
		}
		Json materialLocalId = nullptr;
		if (settings.importMaterials && mesh->mMaterialIndex < scene->mNumMaterials)
		{
			materialLocalId = materialIds.at(mesh->mMaterialIndex);
		}
		auto data = GeometryData(*mesh);
		meshMaterialBindings[std::to_string(localFileId)] = materialLocalId;
		if (mesh->mNumBones != 0)
		{
			if (bonesByName.empty())
			{
				return Result<ModelImportResult>::Failure("Skinned mesh has no imported skeleton at " +
				                                          source.ToString());
			}
			Vector<RawSkinInfluence> rawInfluences;
			for (Ursa::UInt32 boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
			{
				const aiBone* bone = mesh->mBones[boneIndex];
				if (!bone || (bone->mNumWeights != 0 && !bone->mWeights))
				{
					return Result<ModelImportResult>::Failure("Skinned mesh contains malformed bone data at " +
					                                          source.ToString());
				}
				const String boneName = ToUrsaString(bone->mName.C_Str());
				const auto boneNode = boneNodesByName.find(boneName);
				if (boneNode == boneNodesByName.end())
				{
					return Result<ModelImportResult>::Failure("Skin bone " + boneName + " is absent from the skeleton");
				}
				const auto joint = jointIndicesByNode.find(boneNode->second);
				if (joint == jointIndicesByNode.end() || joint->second > std::numeric_limits<Ursa::UInt16>::max())
				{
					return Result<ModelImportResult>::Failure("Skin bone index exceeds the supported range: " +
					                                          boneName);
				}
				for (Ursa::UInt32 weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
				{
					const aiVertexWeight& weight = bone->mWeights[weightIndex];
					rawInfluences.push_back(
					    {weight.mVertexId, joint->second, static_cast<Ursa::Float32>(weight.mWeight)});
				}
			}
			auto normalizedInfluences =
			    NormalizeSkinInfluences(mesh->mNumVertices, rawInfluences, settings.maxBoneInfluences);
			if (!normalizedInfluences)
			{
				return Result<ModelImportResult>::Failure(normalizedInfluences.error + " at " + source.ToString());
			}
			auto skeleton = MakeShared<ModelSkeleton>(Memory::MemoryTag::Assets);
			skeleton->modelGuid = guid;
			skeleton->localFileId = skeletonLocalFileId;
			skeleton->joints.resize(jointIndicesByNode.size());
			data.skeletonReference = {guid, skeletonLocalFileId};
			data.skeleton = std::move(skeleton);
			for (const auto& influence : normalizedInfluences.value)
			{
				data.skinInfluences.push_back({influence.jointIndices, influence.weights});
			}
		}
		auto geometry = Mesh::Create(std::move(data));
		if (!geometry)
		{
			return Result<ModelImportResult>::Failure(geometry.error + " at " + source.ToString());
		}
		auto encodedMesh = EncodeMeshResource(guid, *geometry.value, localFileId);
		if (!encodedMesh)
		{
			return Result<ModelImportResult>::Failure(encodedMesh.error);
		}
		artifacts.push_back({localFileId, "Mesh", std::move(encodedMesh.value)});
	}
	Vector<Ursa::UInt64> animationClipIds;
	for (Ursa::UInt32 animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
	{
		const aiAnimation* animation = scene->mAnimations[animationIndex];
		if (!animation)
		{
			return Result<ModelImportResult>::Failure("Assimp returned a null animation at index " +
			                                          ToUrsaString(std::to_string(animationIndex)));
		}
		const String name = animation->mName.length > 0 ? ToUrsaString(animation->mName.C_Str())
		                                                : "Animation" + ToUrsaString(std::to_string(animationIndex));
		const Ursa::UInt64 localFileId =
		    StableLocalFileId("animation:" + ToUrsaString(std::to_string(animationIndex)) + ":" + name);
		const Ursa::Float64 ticksPerSecond = animation->mTicksPerSecond > 0.0 ? animation->mTicksPerSecond : 25.0;
		Ursa::Float64 durationTicks = animation->mDuration;
		Json channels = Json::array();
		if (animation->mNumMeshChannels != 0 || animation->mNumMorphMeshChannels != 0)
		{
			diagnostics.push_back("Animation " + name +
			                      ": mesh and morph animation channels are unsupported and were ignored");
		}
		for (Ursa::UInt32 channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
		{
			const aiNodeAnim* channel = animation->mChannels[channelIndex];
			if (!channel)
			{
				return Result<ModelImportResult>::Failure("Animation " + name + " contains a null node channel");
			}
			const String nodeName = ToUrsaString(channel->mNodeName.C_Str());
			const auto matchingNodes = nodesByName.find(nodeName);
			if (matchingNodes == nodesByName.end() || matchingNodes->second.size() != 1)
			{
				return Result<ModelImportResult>::Failure("Animation " + name + " node '" + nodeName +
				                                          "' is missing or ambiguous at " + source.ToString());
			}
			Json positionKeys = Json::array();
			for (Ursa::UInt32 keyIndex = 0; keyIndex < channel->mNumPositionKeys; ++keyIndex)
			{
				const aiVectorKey& key = channel->mPositionKeys[keyIndex];
				if (!std::isfinite(key.mTime) || !std::isfinite(key.mValue.x) || !std::isfinite(key.mValue.y) ||
				    !std::isfinite(key.mValue.z))
				{
					return Result<ModelImportResult>::Failure("Animation " + name + " has an invalid position key");
				}
				durationTicks = (std::max)(durationTicks, key.mTime);
				positionKeys.push_back({key.mTime / ticksPerSecond, Vector3Json(key.mValue)});
			}
			Json rotationKeys = Json::array();
			for (Ursa::UInt32 keyIndex = 0; keyIndex < channel->mNumRotationKeys; ++keyIndex)
			{
				const aiQuatKey& key = channel->mRotationKeys[keyIndex];
				if (!std::isfinite(key.mTime) || !std::isfinite(key.mValue.x) || !std::isfinite(key.mValue.y) ||
				    !std::isfinite(key.mValue.z) || !std::isfinite(key.mValue.w))
				{
					return Result<ModelImportResult>::Failure("Animation " + name + " has an invalid rotation key");
				}
				durationTicks = (std::max)(durationTicks, key.mTime);
				rotationKeys.push_back(
				    {key.mTime / ticksPerSecond, {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
			}
			Json scaleKeys = Json::array();
			for (Ursa::UInt32 keyIndex = 0; keyIndex < channel->mNumScalingKeys; ++keyIndex)
			{
				const aiVectorKey& key = channel->mScalingKeys[keyIndex];
				if (!std::isfinite(key.mTime) || !std::isfinite(key.mValue.x) || !std::isfinite(key.mValue.y) ||
				    !std::isfinite(key.mValue.z))
				{
					return Result<ModelImportResult>::Failure("Animation " + name + " has an invalid scale key");
				}
				durationTicks = (std::max)(durationTicks, key.mTime);
				scaleKeys.push_back({key.mTime / ticksPerSecond, Vector3Json(key.mValue)});
			}
			channels.push_back({{"nodeLocalFileId", nodeIdsByPointer.at(matchingNodes->second[0])},
			                    {"positionKeys", std::move(positionKeys)},
			                    {"rotationKeys", std::move(rotationKeys)},
			                    {"scaleKeys", std::move(scaleKeys)}});
		}
		if (!std::isfinite(durationTicks) || durationTicks < 0.0)
		{
			return Result<ModelImportResult>::Failure("Animation " + name + " has an invalid duration");
		}
		const Ursa::Float64 durationSeconds = durationTicks / ticksPerSecond;
		Json clipDocument = {{"format", "UrsaAnimationClip"},      {"version", 1},
		                     {"localFileId", localFileId},         {"name", ToStdString(name)},
		                     {"durationSeconds", durationSeconds}, {"ticksPerSecond", ticksPerSecond},
		                     {"channels", std::move(channels)}};
		auto encodedClip = EncodeScene(clipDocument);
		if (!encodedClip)
		{
			return Result<ModelImportResult>::Failure(encodedClip.error);
		}
		animationClipIds.push_back(localFileId);
		artifacts.push_back({localFileId, "AnimationClip", std::move(encodedClip.value)});
	}
	Json nodes = Json::array();
	Vector<ImportedArtifact> nodeArtifacts;
	auto visitNode = [&](auto&& self, const aiNode& node, Ursa::UInt64 parentId) -> Result<Ursa::UInt64>
	{
		const String name = ToUrsaString(node.mName.C_Str());
		const String nodePath = nodePathsByPointer.at(&node);
		const Ursa::UInt64 localFileId = nodeIdsByPointer.at(&node);
		Json attachedMeshes = Json::array();
		for (Ursa::UInt32 index = 0; index < node.mNumMeshes; ++index)
		{
			const Ursa::UInt32 meshIndex = node.mMeshes[index];
			if (meshIndex >= scene->mNumMeshes)
			{
				return Result<Ursa::UInt64>::Failure("Model node references an invalid mesh index at " + nodePath);
			}
			attachedMeshes.push_back(meshIdMap.at(meshIndex));
		}
		Json attachedChildren = Json::array();
		aiVector3D scale;
		aiQuaternion rotation;
		aiVector3D position;
		node.mTransformation.Decompose(scale, rotation, position);
		Json localTransform = {{"position", {position.x, position.y, position.z}},
		                       {"rotation", {rotation.x, rotation.y, rotation.z, rotation.w}},
		                       {"scale", {scale.x, scale.y, scale.z}}};
		Json nodeDocument = {{"localFileId", localFileId},
		                     {"parentLocalFileId", parentId},
		                     {"name", ToStdString(name)},
		                     {"transform", MatrixJson(node.mTransformation)},
		                     {"localTransform", std::move(localTransform)},
		                     {"meshLocalFileIds", attachedMeshes},
		                     {"children", attachedChildren}};
		for (Ursa::UInt32 index = 0; index < node.mNumChildren; ++index)
		{
			auto childId = self(self, *node.mChildren[index], localFileId);
			if (!childId)
			{
				return childId;
			}
			nodeDocument["children"].push_back(childId.value);
		}
		auto encodedNode = EncodeScene(nodeDocument);
		if (!encodedNode)
		{
			return Result<Ursa::UInt64>::Failure(encodedNode.error);
		}
		nodeArtifacts.push_back({localFileId, "ModelNode", std::move(encodedNode.value)});
		nodes.push_back(std::move(nodeDocument));
		return Result<Ursa::UInt64>::Success(localFileId);
	};
	auto rootId = visitNode(visitNode, *scene->mRootNode, 0);
	if (!rootId)
	{
		return Result<ModelImportResult>::Failure(rootId.error);
	}
	Json modelDocument = {{"format", "UrsaModel"},
	                      {"version", 1},
	                      {"guid", ToStdString(guid.ToString())},
	                      {"rootNodeLocalFileId", rootId.value},
	                      {"nodes", std::move(nodes)},
	                      {"meshLocalFileIds", Json::array()},
	                      {"materialLocalFileIds", Json::array()},
	                      {"skeletonLocalFileIds", Json::array()},
	                      {"animationClipLocalFileIds", Json::array()},
	                      {"dependencies", Json::array()},
	                      {"diagnostics", Json::array()}};
	modelDocument["meshMaterialBindings"] = std::move(meshMaterialBindings);
	for (Ursa::UInt64 id : meshIds)
	{
		modelDocument["meshLocalFileIds"].push_back(id);
	}
	if (hasSkeleton)
	{
		modelDocument["skeletonLocalFileIds"].push_back(skeletonLocalFileId);
	}
	for (Ursa::UInt64 id : animationClipIds)
	{
		modelDocument["animationClipLocalFileIds"].push_back(id);
	}
	if (settings.importMaterials)
	{
		for (Ursa::UInt64 id : allMaterialIds)
		{
			modelDocument["materialLocalFileIds"].push_back(id);
		}
		modelDocument["materials"] = std::move(materials);
	}
	for (const String& diagnostic : diagnostics)
	{
		modelDocument["diagnostics"].push_back(ToStdString(diagnostic));
	}
	for (const VirtualPath& dependency : importedDependencies)
	{
		modelDocument["dependencies"].push_back(ToStdString(dependency.ToString()));
	}
	auto encodedModel = EncodeScene(modelDocument);
	if (!encodedModel)
	{
		return Result<ModelImportResult>::Failure(encodedModel.error);
	}
	Vector<ImportedArtifact> allArtifacts;
	allArtifacts.push_back({0, "Model", std::move(encodedModel.value)});
	for (ImportedArtifact& artifact : artifacts)
	{
		allArtifacts.push_back(std::move(artifact));
	}
	for (ImportedArtifact& artifact : nodeArtifacts)
	{
		allArtifacts.push_back(std::move(artifact));
	}
	for (Ursa::UInt32 index = 0; index < scene->mNumMeshes; ++index)
	{
		if (meshIds[index] == 0)
		{
			return Result<ModelImportResult>::Failure("Model mesh has an empty local file ID at " + source.ToString());
		}
	}
	if (settings.importMaterials)
	{
		for (const Json& materialRecord : modelDocument["materials"])
		{
			auto encodedMaterial = EncodeScene(materialRecord["material"]);
			if (!encodedMaterial)
			{
				return Result<ModelImportResult>::Failure(encodedMaterial.error);
			}
			allArtifacts.push_back(
			    {materialRecord["localFileId"].get<Ursa::UInt64>(), "Material", std::move(encodedMaterial.value)});
		}
	}
	auto published = database.PublishImport(source, modelImporterId, modelImporterVersion, "model-v5",
	                                        serializedSettings, importedDependencies, allArtifacts);
	if (!published)
	{
		return Result<ModelImportResult>::Failure(published.error);
	}
	auto artifactPath = database.Resolve(guid, 0);
	if (!artifactPath)
	{
		return Result<ModelImportResult>::Failure(artifactPath.error);
	}
	auto status = database.CheckImport(source, modelImporterId, modelImporterVersion, "model-v5", serializedSettings,
	                                   importedDependencies);
	if (!status)
	{
		return Result<ModelImportResult>::Failure(status.error);
	}
	return Result<ModelImportResult>::Success({guid, std::move(artifactPath.value), std::move(status.value.artifacts),
	                                           std::move(importedDependencies), false, std::move(diagnostics)});
}
} // namespace Ursa::Editor
