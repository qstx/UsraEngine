#include <Assets/ModelImporter.hpp>
#include <Assets/AssetImportRules.hpp>

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

constexpr Ursa::UInt32 modelImporterVersion = ModelImporter::Version;
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
	return extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb";
}

Result<Vector<String>> ValidateGltfSource(const VirtualFileSystem& filesystem, const VirtualPath& source)
{
	const String sourceExtension = Lowered(source.relative.substr(source.relative.rfind('.')));
	if (sourceExtension != ".gltf" && sourceExtension != ".glb")
	{
		return Result<Vector<String>>::Success({});
	}
	auto bytesResult = filesystem.ReadPrefix(source, 256ull * 1024ull * 1024ull);
	if (!bytesResult)
	{
		return Result<Vector<String>>::Failure("Cannot read glTF source: " + bytesResult.error);
	}
	const auto& bytes = bytesResult.value;
	String jsonText;
	if (sourceExtension == ".glb")
	{
		auto readU32 = [&bytes](Ursa::Size offset) -> Ursa::UInt32
		{
			return static_cast<Ursa::UInt32>(bytes[offset]) | (static_cast<Ursa::UInt32>(bytes[offset + 1]) << 8) |
			       (static_cast<Ursa::UInt32>(bytes[offset + 2]) << 16) |
			       (static_cast<Ursa::UInt32>(bytes[offset + 3]) << 24);
		};
		if (bytes.size() < 20 || readU32(0) != 0x46546C67 || readU32(4) != 2 || readU32(8) != bytes.size())
		{
			return Result<Vector<String>>::Failure("Invalid GLB header, version, or total length");
		}
		Ursa::Size offset = 12;
		bool foundJson = false;
		while (offset + 8 <= bytes.size())
		{
			const Ursa::UInt32 chunkLength = readU32(offset);
			const Ursa::UInt32 chunkType = readU32(offset + 4);
			offset += 8;
			if (chunkLength > bytes.size() - offset)
			{
				return Result<Vector<String>>::Failure("GLB chunk length exceeds the container boundary");
			}
			if (!foundJson)
			{
				if (chunkType != 0x4E4F534A)
				{
					return Result<Vector<String>>::Failure("GLB first chunk must contain JSON");
				}
				jsonText.assign(reinterpret_cast<const char*>(bytes.data() + offset), chunkLength);
				foundJson = true;
			}
			offset += chunkLength;
		}
		if (!foundJson || offset != bytes.size())
		{
			return Result<Vector<String>>::Failure("GLB has a missing JSON chunk or malformed chunk boundary");
		}
	}
	else
	{
		jsonText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}
	Json document;
	try
	{
		document = Json::parse(ToStdString(jsonText));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<String>>::Failure("Invalid glTF JSON: " + ToUrsaString(error.what()));
	}
	if (!document.is_object() || !document.contains("asset") || !document["asset"].is_object() ||
	    !document["asset"].contains("version") || document["asset"]["version"] != "2.0")
	{
		return Result<Vector<String>>::Failure("Only glTF 2.0 assets are supported");
	}
	if (!document.contains("scenes") || !document["scenes"].is_array() || document["scenes"].empty())
	{
		return Result<Vector<String>>::Failure("glTF has no selectable scene");
	}
	Ursa::UInt64 selectedScene = 0;
	if (document.contains("scene"))
	{
		const Json& sceneIndex = document["scene"];
		if (!sceneIndex.is_number_integer() && !sceneIndex.is_number_unsigned())
		{
			return Result<Vector<String>>::Failure("glTF default scene index is invalid");
		}
		if (sceneIndex.is_number_unsigned())
		{
			selectedScene = sceneIndex.get<Ursa::UInt64>();
		}
		else
		{
			const Ursa::Int64 signedSceneIndex = sceneIndex.get<Ursa::Int64>();
			if (signedSceneIndex < 0)
			{
				return Result<Vector<String>>::Failure("glTF default scene index is invalid");
			}
			selectedScene = static_cast<Ursa::UInt64>(signedSceneIndex);
		}
	}
	if (selectedScene >= document["scenes"].size())
	{
		return Result<Vector<String>>::Failure("glTF default scene index is out of range");
	}
	if (document.contains("extensionsRequired"))
	{
		if (!document["extensionsRequired"].is_array())
		{
			return Result<Vector<String>>::Failure("glTF extensionsRequired must be an array");
		}
		if (!document["extensionsRequired"].empty())
		{
			String extensions;
			for (const auto& requiredExtension : document["extensionsRequired"])
			{
				if (!requiredExtension.is_string())
				{
					return Result<Vector<String>>::Failure("glTF extensionsRequired contains a non-string value");
				}
				if (!extensions.empty())
				{
					extensions += ", ";
				}
				extensions += ToUrsaString(requiredExtension.get<std::string>());
			}
			return Result<Vector<String>>::Failure("Unsupported required glTF extensions: " + extensions);
		}
	}
	Vector<String> diagnostics;
	if (document.contains("extensionsUsed") && document["extensionsUsed"].is_array())
	{
		for (const auto& usedExtension : document["extensionsUsed"])
		{
			if (usedExtension.is_string())
			{
				diagnostics.push_back("glTF optional extension " + ToUrsaString(usedExtension.get<std::string>()) +
				                      " may be ignored when its effect is not represented by the importer");
			}
		}
	}
	return Result<Vector<String>>::Success(std::move(diagnostics));
}

struct GltfNodeNormalization
{
	Vector<Ursa::UInt8> sourceOverride;
	Map<String, String> displayNames;
	Map<String, Ursa::UInt32> nodeIndices;
	Vector<String> diagnostics;
};

Result<GltfNodeNormalization> NormalizeGltfNodes(const VirtualFileSystem& filesystem, const VirtualPath& source)
{
	GltfNodeNormalization normalized;
	const String extension = Lowered(source.relative.substr(source.relative.rfind('.')));
	if (extension != ".gltf" && extension != ".glb")
	{
		return Result<GltfNodeNormalization>::Success(std::move(normalized));
	}
	auto sourceBytes = filesystem.Read(source);
	if (!sourceBytes)
	{
		return Result<GltfNodeNormalization>::Failure(sourceBytes.error);
	}
	const Vector<Ursa::UInt8>& bytes = sourceBytes.value;
	String jsonText;
	Vector<std::pair<Ursa::UInt32, Vector<Ursa::UInt8>>> remainingChunks;
	auto readU32 = [](const Vector<Ursa::UInt8>& data, Ursa::Size offset) -> Ursa::UInt32
	{
		return static_cast<Ursa::UInt32>(data[offset]) | (static_cast<Ursa::UInt32>(data[offset + 1]) << 8) |
		       (static_cast<Ursa::UInt32>(data[offset + 2]) << 16) |
		       (static_cast<Ursa::UInt32>(data[offset + 3]) << 24);
	};
	if (extension == ".glb")
	{
		Ursa::Size offset = 12;
		bool jsonChunk = false;
		while (offset + 8 <= bytes.size())
		{
			const Ursa::UInt32 length = readU32(bytes, offset);
			const Ursa::UInt32 type = readU32(bytes, offset + 4);
			offset += 8;
			if (length > bytes.size() - offset)
			{
				return Result<GltfNodeNormalization>::Failure("GLB chunk length exceeds the container boundary");
			}
			if (!jsonChunk)
			{
				if (type != 0x4E4F534A)
				{
					return Result<GltfNodeNormalization>::Failure("GLB first chunk must contain JSON");
				}
				jsonText.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
				jsonChunk = true;
			}
			else
			{
				remainingChunks.emplace_back(type, Vector<Ursa::UInt8>(bytes.begin() + Ursa::Size(offset),
				                                                       bytes.begin() + Ursa::Size(offset + length)));
			}
			offset += length;
		}
		if (!jsonChunk || offset != bytes.size())
		{
			return Result<GltfNodeNormalization>::Failure("Malformed GLB chunk boundary");
		}
	}
	else
	{
		jsonText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}
	Json document;
	try
	{
		document = Json::parse(ToStdString(jsonText));
	}
	catch (const std::exception& error)
	{
		return Result<GltfNodeNormalization>::Failure("Invalid glTF JSON: " + ToUrsaString(error.what()));
	}
	if (!document.contains("nodes"))
	{
		document["nodes"] = Json::array();
	}
	if (!document["nodes"].is_array())
	{
		return Result<GltfNodeNormalization>::Failure("glTF nodes must be an array");
	}
	if (document.contains("materials") && document["materials"].is_array())
	{
		for (Ursa::Size materialIndex = 0; materialIndex < document["materials"].size(); ++materialIndex)
		{
			const Json& material = document["materials"][materialIndex];
			if (!material.is_object())
			{
				continue;
			}
			const String materialName = material.contains("name") && material["name"].is_string()
			                                ? ToUrsaString(material["name"].get<std::string>())
			                                : "Material" + ToUrsaString(std::to_string(materialIndex));
			if (material.contains("alphaMode") && material["alphaMode"].is_string() &&
			    material["alphaMode"] != "OPAQUE")
			{
				normalized.diagnostics.push_back("Material " + materialName +
				                                 ": glTF alphaMode is not represented by the current material model");
			}
			if (material.contains("alphaCutoff"))
			{
				normalized.diagnostics.push_back("Material " + materialName +
				                                 ": glTF alphaCutoff is not represented by the current material model");
			}
			if (material.contains("doubleSided") && material["doubleSided"] == true)
			{
				normalized.diagnostics.push_back(
				    "Material " + materialName +
				    ": glTF doubleSided state is not represented by the current material model");
			}
			const auto inspectTexture = [&](const char* slot, const Json& textureInfo)
			{
				if (!textureInfo.is_object())
				{
					return;
				}
				if (textureInfo.contains("texCoord") && textureInfo["texCoord"].is_number() &&
				    textureInfo["texCoord"].get<double>() > 0.0)
				{
					normalized.diagnostics.push_back("Material " + materialName + ": " + ToUrsaString(slot) +
					                                 " uses a nonzero glTF texCoord set; only UV set 0 is supported");
				}
				if (textureInfo.contains("extensions") && textureInfo["extensions"].is_object() &&
				    textureInfo["extensions"].contains("KHR_texture_transform"))
				{
					normalized.diagnostics.push_back("Material " + materialName + ": " + ToUrsaString(slot) +
					                                 " uses KHR_texture_transform, which is not represented");
				}
				if (!textureInfo.contains("index") || !textureInfo["index"].is_number_unsigned() ||
				    !document.contains("textures") || !document["textures"].is_array())
				{
					return;
				}
				const Ursa::UInt32 textureIndex = textureInfo["index"].get<Ursa::UInt32>();
				if (textureIndex < document["textures"].size() && document["textures"][textureIndex].is_object() &&
				    document["textures"][textureIndex].contains("sampler"))
				{
					normalized.diagnostics.push_back("Material " + materialName + ": " + ToUrsaString(slot) +
					                                 " uses explicit glTF sampler settings, which are not represented");
				}
			};
			if (material.contains("pbrMetallicRoughness") && material["pbrMetallicRoughness"].is_object())
			{
				const Json& pbr = material["pbrMetallicRoughness"];
				if (pbr.contains("baseColorTexture"))
				{
					inspectTexture("baseColorTexture", pbr["baseColorTexture"]);
				}
				if (pbr.contains("metallicRoughnessTexture"))
				{
					inspectTexture("metallicRoughnessTexture", pbr["metallicRoughnessTexture"]);
				}
			}
			for (const char* slot : {"normalTexture", "occlusionTexture", "emissiveTexture"})
			{
				if (material.contains(slot))
				{
					inspectTexture(slot, material[slot]);
				}
			}
		}
	}
	if (document.contains("skins") && document["skins"].is_array())
	{
		if (!document.contains("buffers") || !document["buffers"].is_array())
		{
			document["buffers"] = Json::array();
		}
		if (!document.contains("bufferViews") || !document["bufferViews"].is_array())
		{
			document["bufferViews"] = Json::array();
		}
		if (!document.contains("accessors") || !document["accessors"].is_array())
		{
			document["accessors"] = Json::array();
		}
		static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		for (Json& skin : document["skins"])
		{
			if (!skin.is_object() || skin.contains("inverseBindMatrices") || !skin.contains("joints") ||
			    !skin["joints"].is_array())
			{
				continue;
			}
			const Ursa::Size jointCount = skin["joints"].size();
			if (jointCount == 0 || jointCount > std::numeric_limits<Ursa::UInt32>::max() / 64)
			{
				return Result<GltfNodeNormalization>::Failure("glTF skin has an invalid joint count");
			}
			Vector<Ursa::UInt8> identityMatrices;
			identityMatrices.reserve(jointCount * 64);
			for (Ursa::Size joint = 0; joint < jointCount; ++joint)
			{
				for (Ursa::Size component = 0; component < 16; ++component)
				{
					const Ursa::Float32 value = component % 5 == 0 ? 1.0f : 0.0f;
					Ursa::UInt32 bits = 0;
					std::memcpy(&bits, &value, sizeof(bits));
					for (Ursa::UInt32 byte = 0; byte < 4; ++byte)
					{
						identityMatrices.push_back(static_cast<Ursa::UInt8>(bits >> (byte * 8)));
					}
				}
			}
			std::string base64;
			base64.reserve((identityMatrices.size() + 2) / 3 * 4);
			for (Ursa::Size offset = 0; offset < identityMatrices.size(); offset += 3)
			{
				const Ursa::UInt32 a = identityMatrices[offset];
				const Ursa::UInt32 b = offset + 1 < identityMatrices.size() ? identityMatrices[offset + 1] : 0;
				const Ursa::UInt32 c = offset + 2 < identityMatrices.size() ? identityMatrices[offset + 2] : 0;
				const Ursa::UInt32 bits = (a << 16) | (b << 8) | c;
				base64.push_back(alphabet[(bits >> 18) & 63]);
				base64.push_back(alphabet[(bits >> 12) & 63]);
				base64.push_back(offset + 1 < identityMatrices.size() ? alphabet[(bits >> 6) & 63] : '=');
				base64.push_back(offset + 2 < identityMatrices.size() ? alphabet[bits & 63] : '=');
			}
			const Ursa::UInt32 bufferIndex = static_cast<Ursa::UInt32>(document["buffers"].size());
			const Ursa::UInt32 viewIndex = static_cast<Ursa::UInt32>(document["bufferViews"].size());
			const Ursa::UInt32 accessorIndex = static_cast<Ursa::UInt32>(document["accessors"].size());
			document["buffers"].push_back(
			    {{"byteLength", identityMatrices.size()}, {"uri", "data:application/octet-stream;base64," + base64}});
			document["bufferViews"].push_back(
			    {{"buffer", bufferIndex}, {"byteOffset", 0}, {"byteLength", identityMatrices.size()}});
			document["accessors"].push_back({{"bufferView", viewIndex},
			                                 {"byteOffset", 0},
			                                 {"componentType", 5126},
			                                 {"count", jointCount},
			                                 {"type", "MAT4"}});
			skin["inverseBindMatrices"] = accessorIndex;
		}
	}
	// Assimp associates bones with the source mesh object. Duplicate a shared glTF mesh for each
	// distinct skin so each imported aiMesh retains that skin's joint ordering and bind matrices.
	if (document.contains("meshes") && document["meshes"].is_array())
	{
		Map<Ursa::UInt64, Ursa::UInt32> meshBySkin;
		for (Json& node : document["nodes"])
		{
			if (!node.is_object() || !node.contains("mesh") || !node["mesh"].is_number_unsigned() ||
			    !node.contains("skin") || !node["skin"].is_number_unsigned())
			{
				continue;
			}
			const Ursa::UInt32 sourceMesh = node["mesh"].get<Ursa::UInt32>();
			const Ursa::UInt32 skin = node["skin"].get<Ursa::UInt32>();
			if (sourceMesh >= document["meshes"].size())
			{
				continue;
			}
			const Ursa::UInt64 key = (Ursa::UInt64(sourceMesh) << 32) | skin;
			auto mapped = meshBySkin.find(key);
			if (mapped == meshBySkin.end())
			{
				Ursa::UInt32 importedMesh = sourceMesh;
				const bool hasAnotherSkin =
				    std::any_of(meshBySkin.begin(), meshBySkin.end(), [sourceMesh](const auto& entry)
				                { return Ursa::UInt32(entry.first >> 32) == sourceMesh; });
				if (hasAnotherSkin)
				{
					if (document["meshes"].size() >= std::numeric_limits<Ursa::UInt32>::max())
					{
						return Result<GltfNodeNormalization>::Failure(
						    "glTF shared mesh skin variants exceed the index limit");
					}
					importedMesh = static_cast<Ursa::UInt32>(document["meshes"].size());
					document["meshes"].push_back(document["meshes"][sourceMesh]);
				}
				mapped = meshBySkin.emplace(key, importedMesh).first;
			}
			node["mesh"] = mapped->second;
		}
	}
	for (Ursa::UInt32 index = 0; index < document["nodes"].size(); ++index)
	{
		Json& node = document["nodes"][index];
		if (!node.is_object())
		{
			return Result<GltfNodeNormalization>::Failure("glTF node entry must be an object");
		}
		const String internalName = "__ursa_gltf_node_" + ToUrsaString(std::to_string(index));
		const String displayName = node.contains("name") && node["name"].is_string()
		                               ? ToUrsaString(node["name"].get<std::string>())
		                               : "Node " + ToUrsaString(std::to_string(index));
		node["name"] = ToStdString(internalName);
		normalized.displayNames.emplace(internalName, displayName);
		normalized.nodeIndices.emplace(internalName, index);
	}
	const String encodedJson = ToUrsaString(document.dump());
	if (extension == ".gltf")
	{
		normalized.sourceOverride.assign(encodedJson.begin(), encodedJson.end());
	}
	else
	{
		Vector<Ursa::UInt8>& output = normalized.sourceOverride;
		auto appendU32 = [&output](Ursa::UInt32 value)
		{
			output.push_back(static_cast<Ursa::UInt8>(value & 0xff));
			output.push_back(static_cast<Ursa::UInt8>((value >> 8) & 0xff));
			output.push_back(static_cast<Ursa::UInt8>((value >> 16) & 0xff));
			output.push_back(static_cast<Ursa::UInt8>((value >> 24) & 0xff));
		};
		Vector<Ursa::UInt8> encodedJsonBytes(encodedJson.begin(), encodedJson.end());
		while (encodedJsonBytes.size() % 4 != 0)
		{
			encodedJsonBytes.push_back(static_cast<Ursa::UInt8>(' '));
		}
		Ursa::UInt64 totalLength = 12 + 8 + encodedJsonBytes.size();
		for (const auto& chunk : remainingChunks)
		{
			totalLength += 8 + chunk.second.size();
		}
		if (totalLength > std::numeric_limits<Ursa::UInt32>::max())
		{
			return Result<GltfNodeNormalization>::Failure("Normalized GLB exceeds the format size limit");
		}
		appendU32(0x46546C67);
		appendU32(2);
		appendU32(static_cast<Ursa::UInt32>(totalLength));
		appendU32(static_cast<Ursa::UInt32>(encodedJsonBytes.size()));
		appendU32(0x4E4F534A);
		output.insert(output.end(), encodedJsonBytes.begin(), encodedJsonBytes.end());
		for (const auto& chunk : remainingChunks)
		{
			appendU32(static_cast<Ursa::UInt32>(chunk.second.size()));
			appendU32(chunk.first);
			output.insert(output.end(), chunk.second.begin(), chunk.second.end());
		}
	}
	return Result<GltfNodeNormalization>::Success(std::move(normalized));
}

Ursa::UInt64 StableLocalFileId(const String& key)
{
	return StableImportObjectId(modelImporterId, key).value;
}

Json Vector3Json(const aiVector3D& value)
{
	return Json::array({static_cast<Ursa::Float32>(value.x), static_cast<Ursa::Float32>(value.y),
	                    static_cast<Ursa::Float32>(value.z)});
}

AnimationInterpolation AnimationMode(aiAnimInterpolation value)
{
	switch (value)
	{
		case aiAnimInterpolation_Step:
			return AnimationInterpolation::Step;
		case aiAnimInterpolation_Cubic_Spline:
			return AnimationInterpolation::CubicSpline;
		default:
			return AnimationInterpolation::Linear;
	}
}

String AnimationModeName(AnimationInterpolation value)
{
	switch (value)
	{
		case AnimationInterpolation::Step:
			return "STEP";
		case AnimationInterpolation::CubicSpline:
			return "CUBICSPLINE";
		default:
			return "LINEAR";
	}
}

Result<Json> ConvertVectorTrack(const aiVectorKey* keys, Ursa::UInt32 keyCount, Ursa::Float64 ticksPerSecond,
                                Ursa::Float64& durationTicks, AnimationInterpolation& interpolation)
{
	Json result = Json::array();
	if (keyCount == 0)
	{
		interpolation = AnimationInterpolation::Linear;
		return Result<Json>::Success(std::move(result));
	}
	interpolation = AnimationMode(keys[0].mInterpolation);
	const Ursa::UInt32 stride = interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
	if (keyCount % stride != 0)
	{
		return Result<Json>::Failure("Animation cubic-spline vector track has an incomplete tangent triplet");
	}
	Ursa::Float64 previousTime = -1.0;
	for (Ursa::UInt32 logicalIndex = 0; logicalIndex < keyCount / stride; ++logicalIndex)
	{
		const Ursa::UInt32 valueIndex = logicalIndex * stride + (stride == 3 ? 1 : 0);
		const aiVectorKey& key = keys[valueIndex];
		if (!std::isfinite(key.mTime) || key.mTime < 0.0 || key.mTime <= previousTime || !std::isfinite(key.mValue.x) ||
		    !std::isfinite(key.mValue.y) || !std::isfinite(key.mValue.z))
		{
			return Result<Json>::Failure("Animation contains an invalid or unordered vector key");
		}
		previousTime = key.mTime;
		durationTicks = (std::max)(durationTicks, key.mTime);
		Json encoded = Json::array({key.mTime / ticksPerSecond, Vector3Json(key.mValue)});
		if (stride == 3)
		{
			const aiVectorKey& inTangent = keys[logicalIndex * stride];
			const aiVectorKey& outTangent = keys[logicalIndex * stride + 2];
			if (inTangent.mTime != key.mTime || outTangent.mTime != key.mTime || !std::isfinite(inTangent.mValue.x) ||
			    !std::isfinite(inTangent.mValue.y) || !std::isfinite(inTangent.mValue.z) ||
			    !std::isfinite(outTangent.mValue.x) || !std::isfinite(outTangent.mValue.y) ||
			    !std::isfinite(outTangent.mValue.z))
			{
				return Result<Json>::Failure("Animation contains invalid cubic-spline vector tangents");
			}
			encoded.push_back(Vector3Json(inTangent.mValue));
			encoded.push_back(Vector3Json(outTangent.mValue));
		}
		result.push_back(std::move(encoded));
	}
	return Result<Json>::Success(std::move(result));
}

Result<Json> ConvertRotationTrack(const aiQuatKey* keys, Ursa::UInt32 keyCount, Ursa::Float64 ticksPerSecond,
                                  Ursa::Float64& durationTicks, AnimationInterpolation& interpolation)
{
	Json result = Json::array();
	if (keyCount == 0)
	{
		interpolation = AnimationInterpolation::Linear;
		return Result<Json>::Success(std::move(result));
	}
	interpolation = AnimationMode(keys[0].mInterpolation);
	const Ursa::UInt32 stride = interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
	if (keyCount % stride != 0)
	{
		return Result<Json>::Failure("Animation cubic-spline rotation track has an incomplete tangent triplet");
	}
	Ursa::Float64 previousTime = -1.0;
	for (Ursa::UInt32 logicalIndex = 0; logicalIndex < keyCount / stride; ++logicalIndex)
	{
		const Ursa::UInt32 valueIndex = logicalIndex * stride + (stride == 3 ? 1 : 0);
		const aiQuatKey& key = keys[valueIndex];
		if (!std::isfinite(key.mTime) || key.mTime < 0.0 || key.mTime <= previousTime || !std::isfinite(key.mValue.x) ||
		    !std::isfinite(key.mValue.y) || !std::isfinite(key.mValue.z) || !std::isfinite(key.mValue.w))
		{
			return Result<Json>::Failure("Animation contains an invalid or unordered rotation key");
		}
		previousTime = key.mTime;
		durationTicks = (std::max)(durationTicks, key.mTime);
		Json encoded =
		    Json::array({key.mTime / ticksPerSecond, {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
		if (stride == 3)
		{
			const aiQuatKey& inTangent = keys[logicalIndex * stride];
			const aiQuatKey& outTangent = keys[logicalIndex * stride + 2];
			const auto finite = [](const aiQuaternion& value)
			{
				return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
				       std::isfinite(value.w);
			};
			if (inTangent.mTime != key.mTime || outTangent.mTime != key.mTime || !finite(inTangent.mValue) ||
			    !finite(outTangent.mValue))
			{
				return Result<Json>::Failure("Animation contains invalid cubic-spline rotation tangents");
			}
			encoded.push_back({inTangent.mValue.x, inTangent.mValue.y, inTangent.mValue.z, inTangent.mValue.w});
			encoded.push_back({outTangent.mValue.x, outTangent.mValue.y, outTangent.mValue.z, outTangent.mValue.w});
		}
		result.push_back(std::move(encoded));
	}
	return Result<Json>::Success(std::move(result));
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
	VfsIOSystem(const VirtualFileSystem& selectedFilesystem, VirtualPath selectedSource,
	            Vector<Ursa::UInt8> sourceOverride = {})
	    : filesystem(selectedFilesystem), source(std::move(selectedSource)), sourceOverride(std::move(sourceOverride))
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
		if (path.value == source && !sourceOverride.empty())
		{
			return new VfsStream(sourceOverride);
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
		const String uri = ToUrsaString(requestedPath);
		String requested;
		requested.reserve(uri.size());
		const auto hexValue = [](char value) -> Int32
		{
			if (value >= '0' && value <= '9')
			{
				return value - '0';
			}
			if (value >= 'a' && value <= 'f')
			{
				return value - 'a' + 10;
			}
			if (value >= 'A' && value <= 'F')
			{
				return value - 'A' + 10;
			}
			return -1;
		};
		for (Ursa::Size index = 0; index < uri.size(); ++index)
		{
			if (uri[index] == '%')
			{
				if (index + 2 >= uri.size())
				{
					return Result<VirtualPath>::Failure("Assimp dependency has an invalid percent escape: " + uri);
				}
				const Int32 high = hexValue(uri[index + 1]);
				const Int32 low = hexValue(uri[index + 2]);
				if (high < 0 || low < 0)
				{
					return Result<VirtualPath>::Failure("Assimp dependency has an invalid percent escape: " + uri);
				}
				const char decoded = static_cast<char>((high << 4) | low);
				if (decoded == '\0')
				{
					return Result<VirtualPath>::Failure("Assimp dependency contains an encoded null byte");
				}
				requested.push_back(decoded);
				index += 2;
			}
			else
			{
				requested.push_back(uri[index]);
			}
		}
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
	Vector<Ursa::UInt8> sourceOverride;
	String sourceDirectory;
	Map<String, VirtualPath> dependencies;
	String lastError;
};

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
	                                      "$mat.shadingm",
	                                      "$tex.file",
	                                      "$tex.uvwsrc",
	                                      "$tex.mapping",
	                                      "$tex.blend",
	                                      "$tex.op",
	                                      "$tex.mappingname",
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
                                     ModelImportCandidate& candidate, Map<String, Ursa::UInt64>& embeddedIds,
                                     const ModelImporter::TextureDependency& textureDependency)
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
		const String localKey = String("texture:") + path;
		const UInt64 localFileId = StableLocalFileId(localKey);
		if (!candidate.textures.emplace(localFileId, std::move(texture.value)).second)
		{
			return Result<ResourceId>::Failure("Embedded texture output identity collision");
		}
		candidate.outputKeys[localFileId] = localKey;
		candidate.displayNames[localFileId] =
		    embedded->mFilename.length ? ToUrsaString(embedded->mFilename.C_Str()) : "Embedded Texture " + path;
		embeddedIds.emplace(path, localFileId);
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
	if (textureDependency)
	{
		auto dependency = textureDependency(resolved.value, textureSettings.value);
		if (dependency)
		{
			dependencies.push_back(resolved.value);
		}
		return dependency;
	}
	auto imported = TextureImporter::Import(database, resolved.value, textureSettings.value);
	if (!imported)
	{
		return Result<ResourceId>::Failure(imported.error);
	}
	dependencies.push_back(resolved.value);
	return Result<ResourceId>::Success(imported.value.guid);
}

Result<ModelCandidateMaterial> ConvertMaterial(aiScene const& scene, Ursa::UInt32 index, ResourceId modelGuid,
                                               AssetDatabase& database, VfsIOSystem& ioSystem,
                                               Vector<VirtualPath>& dependencies, ModelImportCandidate& candidate,
                                               Map<String, Ursa::UInt64>& embeddedIds,
                                               const Map<Ursa::UInt32, Ursa::UInt64>& materialIds, bool importTextures,
                                               Vector<String>& diagnostics,
                                               const ModelImporter::TextureDependency& textureDependency)
{
	aiMaterial* material = scene.mMaterials[index];
	if (!material)
	{
		return Result<ModelCandidateMaterial>::Failure("Assimp returned a null material at index " +
		                                               ToUrsaString(std::to_string(index)));
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
	Map<MaterialTextureSlot, AssetReference> textures;
	const aiTextureType types[] = {aiTextureType_BASE_COLOR,        aiTextureType_DIFFUSE,
	                               aiTextureType_NORMALS,           aiTextureType_METALNESS,
	                               aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_LIGHTMAP,
	                               aiTextureType_AMBIENT_OCCLUSION, aiTextureType_EMISSIVE};
	const char* slots[] = {"baseColor",         "baseColor", "normal",    "metallicRoughness",
	                       "metallicRoughness", "occlusion", "occlusion", "emissive"};
	const MaterialTextureSlot materialSlots[] = {
	    MaterialTextureSlot::BaseColor,         MaterialTextureSlot::BaseColor,         MaterialTextureSlot::Normal,
	    MaterialTextureSlot::MetallicRoughness, MaterialTextureSlot::MetallicRoughness, MaterialTextureSlot::Occlusion,
	    MaterialTextureSlot::Occlusion,         MaterialTextureSlot::Emissive};
	for (Ursa::Size slotIndex = 0; importTextures && slotIndex < std::size(types); ++slotIndex)
	{
		const aiTextureType type = types[slotIndex];
		const String slot = slots[slotIndex];
		if (textures.find(materialSlots[slotIndex]) != textures.end() || material->GetTextureCount(type) == 0)
		{
			continue;
		}
		auto texture = MaterialTextureId(scene, *material, type, slots[slotIndex], modelGuid, database, ioSystem,
		                                 dependencies, candidate, embeddedIds, textureDependency);
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
			textures[materialSlots[slotIndex]] = {texture.value, localTextureId};
		}
		else if (texture.error != "No texture assigned")
		{
			return Result<ModelCandidateMaterial>::Failure("Material " + name + " texture " + slot + ": " +
			                                               texture.error);
		}
	}
	auto importedMaterial = std::make_shared<MaterialInstance>(MaterialType::PbrMetallicRoughness);
	auto setBaseColor = importedMaterial->SetBaseColor({baseColor.r, baseColor.g, baseColor.b, baseColor.a});
	auto setMetallic = importedMaterial->SetMetallic(metallic);
	auto setRoughness = importedMaterial->SetRoughness(roughness);
	auto setEmissive = importedMaterial->SetEmissive({emissive.r, emissive.g, emissive.b, 1.0f});
	auto setAlphaMode = importedMaterial->SetAlphaMode(opacity < 0.999f ? AlphaMode::Blend : AlphaMode::Opaque);
	auto setShader = importedMaterial->SetShader(Shader::BuiltinPbrShaderId());
	if (!setBaseColor || !setMetallic || !setRoughness || !setEmissive || !setAlphaMode || !setShader)
	{
		return Result<ModelCandidateMaterial>::Failure("Cannot map source material " + name +
		                                               " to the built-in PBR Shader");
	}
	auto bound = importedMaterial->BindAssetIdentity({modelGuid, localFileId});
	if (!bound)
	{
		return Result<ModelCandidateMaterial>::Failure(bound.error);
	}
	return Result<ModelCandidateMaterial>::Success(
	    {localFileId, name, std::move(importedMaterial), std::move(textures)});
}

} // namespace

const Vector<String>& ModelImporter::SupportedExtensions()
{
	static const Vector<String> extensions{".fbx", ".obj", ".gltf", ".glb"};
	return extensions;
}

bool ModelImporter::IsSupportedSource(const VirtualPath& path)
{
	return IsSupportedModel(path);
}

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
	auto cache = database.CheckImport(source, modelImporterId, modelImporterVersion, ModelImporter::ContentTarget,
	                                  serializedSettings, dependencies.value);
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
		                                           std::move(dependencies.value), true, std::move(diagnostics.value),
		                                           cache.value.mainObjectLocalFileId});
	}

	auto prepared = BuildCandidate(database, source, settings);
	if (!prepared)
	{
		return Result<ModelImportResult>::Failure(prepared.error);
	}
	auto encoded = prepared.value.Encode();
	if (!encoded)
	{
		return Result<ModelImportResult>::Failure(encoded.error);
	}
	auto published = database.PublishImport(source, modelImporterId, modelImporterVersion, ModelImporter::ContentTarget,
	                                        serializedSettings, prepared.value.dependencies, encoded.value,
	                                        prepared.value.mainObjectLocalFileId);
	if (!published)
	{
		return Result<ModelImportResult>::Failure(published.error);
	}
	auto artifactPath = database.Resolve(guid);
	auto status = database.CurrentImport(source);
	if (!artifactPath || !status || !status.value.upToDate)
	{
		return Result<ModelImportResult>::Failure(!artifactPath ? artifactPath.error
		                                          : !status     ? status.error
		                                                        : String("Model publication is not current"));
	}
	return Result<ModelImportResult>::Success(
	    {guid, std::move(artifactPath.value), std::move(status.value.artifacts), std::move(prepared.value.dependencies),
	     false, std::move(prepared.value.diagnostics), prepared.value.mainObjectLocalFileId});
}

Result<ModelImportCandidate> ModelImporter::BuildCandidate(AssetDatabase& database, const VirtualPath& source,
                                                           const ModelImportSettings& settings,
                                                           const TextureDependency& textureDependency)
{
	if (!IsSupportedModel(source) || !std::isfinite(settings.scaleFactor) || settings.scaleFactor <= 0 ||
	    settings.scaleFactor > 10000 || settings.maxBoneInfluences == 0 || settings.maxBoneInfluences > 4)
	{
		return Result<ModelImportCandidate>::Failure("Invalid model source or candidate settings");
	}
	auto metadata = database.Metadata(source);
	if (!metadata)
	{
		return Result<ModelImportCandidate>::Failure(metadata.error);
	}
	const auto guid = metadata.value.guid;
	auto gltfValidation = ValidateGltfSource(database.Filesystem(), source);
	if (!gltfValidation)
	{
		return Result<ModelImportCandidate>::Failure(gltfValidation.error + " at " + source.ToString());
	}
	auto normalizedGltf = NormalizeGltfNodes(database.Filesystem(), source);
	if (!normalizedGltf)
	{
		return Result<ModelImportCandidate>::Failure(normalizedGltf.error + " at " + source.ToString());
	}
	ModelImportCandidate candidate;
	candidate.guid = guid;
	candidate.mainObjectLocalFileId = StableLocalFileId("main");
	candidate.outputKeys[candidate.mainObjectLocalFileId] = "main";
	candidate.displayNames[candidate.mainObjectLocalFileId] = source.FileName();

	Assimp::Importer importer;
	const auto& gltfDisplayNames = normalizedGltf.value.displayNames;
	const auto& gltfNodeIndices = normalizedGltf.value.nodeIndices;
	auto* ioSystem = new VfsIOSystem(database.Filesystem(), source, std::move(normalizedGltf.value.sourceOverride));
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
	const aiScene* scene = nullptr;
	try
	{
		scene = importer.ReadFile(ToStdString(source.relative), flags);
	}
	catch (const std::exception& error)
	{
		return Result<ModelImportCandidate>::Failure("Assimp failed to parse " + source.ToString() + ": " +
		                                             ToUrsaString(error.what()));
	}
	if (!scene || !scene->mRootNode)
	{
		const String diagnostic =
		    ioSystem->LastError().empty() ? ToUrsaString(importer.GetErrorString()) : ioSystem->LastError();
		return Result<ModelImportCandidate>::Failure("Cannot import " + source.ToString() + ": " + diagnostic);
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
		const auto originalNodeIndex = gltfNodeIndices.find(name);
		const String nodeKey = originalNodeIndex == gltfNodeIndices.end()
		                           ? "node:" + nodePath
		                           : "node:gltf-index:" + ToUrsaString(std::to_string(originalNodeIndex->second));
		const Ursa::UInt64 localFileId = StableLocalFileId(nodeKey);
		candidate.outputKeys[localFileId] = nodeKey;
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
		return Result<ModelImportCandidate>::Failure(indexedNodes.error + " at " + source.ToString());
	}
	Vector<VirtualPath> importedDependencies = ioSystem->Dependencies();

	Map<String, Ursa::UInt64> embeddedIds;
	Map<Ursa::UInt32, Ursa::UInt64> materialIds;
	Vector<Ursa::UInt64> allMaterialIds;
	Vector<String> diagnostics = std::move(gltfValidation.value);
	diagnostics.insert(diagnostics.end(), normalizedGltf.value.diagnostics.begin(),
	                   normalizedGltf.value.diagnostics.end());
	const Ursa::UInt64 skeletonLocalFileId = StableLocalFileId("skeleton:root");
	candidate.outputKeys[skeletonLocalFileId] = "skeleton:root";
	Map<String, const aiBone*> bonesByName;
	Map<String, const aiNode*> boneNodesByName;
	Set<const aiNode*> requiredJointNodes;
	Map<const aiNode*, aiMatrix4x4> inverseBindByNode;
	for (Ursa::UInt32 meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
	{
		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!mesh)
		{
			return Result<ModelImportCandidate>::Failure("Assimp returned a null mesh at index " +
			                                             ToUrsaString(std::to_string(meshIndex)));
		}
		for (Ursa::UInt32 boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
		{
			const aiBone* bone = mesh->mBones[boneIndex];
			if (!bone)
			{
				return Result<ModelImportCandidate>::Failure("Assimp returned a null bone at mesh " +
				                                             ToUrsaString(std::to_string(meshIndex)));
			}
			const String name = ToUrsaString(bone->mName.C_Str());
			const auto nodeMatches = nodesByName.find(name);
			if (nodeMatches == nodesByName.end() || nodeMatches->second.empty())
			{
				return Result<ModelImportCandidate>::Failure("Skin bone '" + name + "' has no matching model node at " +
				                                             source.ToString());
			}
			if (nodeMatches->second.size() != 1)
			{
				return Result<ModelImportCandidate>::Failure("Skin bone '" + name +
				                                             "' matches multiple model nodes at " + source.ToString());
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
			return Result<ModelImportCandidate>::Failure("Assimp returned a null animation at index " +
			                                             ToUrsaString(std::to_string(animationIndex)));
		}
		for (Ursa::UInt32 channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
		{
			const aiNodeAnim* channel = animation->mChannels[channelIndex];
			if (!channel)
			{
				return Result<ModelImportCandidate>::Failure("Animation contains a null node channel at " +
				                                             source.ToString());
			}
			const String nodeName = ToUrsaString(channel->mNodeName.C_Str());
			const auto nodeMatches = nodesByName.find(nodeName);
			if (nodeMatches == nodesByName.end() || nodeMatches->second.size() != 1)
			{
				return Result<ModelImportCandidate>::Failure("Animation node '" + nodeName +
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
			const String internalName = ToUrsaString(jointNode->mName.C_Str());
			const auto originalName = gltfDisplayNames.find(internalName);
			const String name = originalName == gltfDisplayNames.end() ? internalName : originalName->second;
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
		auto skeleton = MakeShared<ModelSkeleton>(Memory::MemoryTag::Assets);
		skeleton->modelGuid = guid;
		skeleton->localFileId = skeletonLocalFileId;
		for (const auto& joint : skeletonDocument["joints"])
		{
			SkeletonJoint value;
			value.nodeLocalFileId = joint["nodeLocalFileId"].get<UInt64>();
			value.parentJointIndex = joint["parentJointIndex"].is_null() ? -1 : joint["parentJointIndex"].get<Int32>();
			value.name = ToUrsaString(joint["name"].get<std::string>());
			const auto& local = joint["bindLocalTransform"];
			value.bindLocalTransform.position = {local["position"][0], local["position"][1], local["position"][2]};
			value.bindLocalTransform.rotation = {local["rotation"][0], local["rotation"][1], local["rotation"][2],
			                                     local["rotation"][3]};
			value.bindLocalTransform.scale = {local["scale"][0], local["scale"][1], local["scale"][2]};
			for (int row = 0; row < 4; ++row)
			{
				for (int column = 0; column < 4; ++column)
				{
					value.inverseBindMatrix.At(row, column) =
					    joint["inverseBindMatrix"][row * 4 + column].get<Float32>();
				}
			}
			skeleton->joints.push_back(std::move(value));
		}
		candidate.skeletons.emplace(skeletonLocalFileId, std::move(skeleton));
		candidate.archives.emplace(skeletonLocalFileId, ModelCandidateArchive{"Skeleton", std::move(skeletonDocument)});
	}
	for (Ursa::UInt32 index = 0; index < scene->mNumMaterials; ++index)
	{
		const String name = MaterialName(*scene->mMaterials[index], index);
		const Ursa::UInt64 localFileId =
		    StableLocalFileId("material:" + ToUrsaString(std::to_string(index)) + ":" + name);
		candidate.outputKeys[localFileId] = "material:" + ToUrsaString(std::to_string(index)) + ":" + name;
		candidate.displayNames[localFileId] = name;
		if (std::find(allMaterialIds.begin(), allMaterialIds.end(), localFileId) != allMaterialIds.end())
		{
			return Result<ModelImportCandidate>::Failure("Model material local-file-ID collision at " +
			                                             source.ToString());
		}
		materialIds.emplace(index, localFileId);
		allMaterialIds.push_back(localFileId);
	}

	if (settings.importMaterials)
	{
		for (Ursa::UInt32 index = 0; index < scene->mNumMaterials; ++index)
		{
			auto material =
			    ConvertMaterial(*scene, index, guid, database, *ioSystem, importedDependencies, candidate, embeddedIds,
			                    materialIds, settings.importTextures, diagnostics, textureDependency);
			if (!material)
			{
				return Result<ModelImportCandidate>::Failure(material.error + " at " + source.ToString());
			}
			candidate.materials.emplace(material.value.localFileId, std::move(material.value));
		}
	}

	Map<UInt64, Vector<UInt64>> meshMaterialBindings;
	Vector<Ursa::UInt64> meshIds(scene->mNumMeshes);
	Map<Ursa::UInt32, Ursa::UInt64> meshIdMap;
	for (Ursa::UInt32 index = 0; index < scene->mNumMeshes; ++index)
	{
		const aiMesh* mesh = scene->mMeshes[index];
		const String name = mesh && mesh->mName.length > 0 ? ToUrsaString(mesh->mName.C_Str())
		                                                   : "Mesh" + ToUrsaString(std::to_string(index));
		const Ursa::UInt64 localFileId = StableLocalFileId("mesh:" + ToUrsaString(std::to_string(index)) + ":" + name);
		candidate.outputKeys[localFileId] = "mesh:" + ToUrsaString(std::to_string(index)) + ":" + name;
		candidate.displayNames[localFileId] = name;
		meshIds[index] = localFileId;
		meshIdMap.emplace(index, localFileId);
		if (!mesh)
		{
			return Result<ModelImportCandidate>::Failure("Assimp returned a null mesh at index " +
			                                             ToUrsaString(std::to_string(index)));
		}
		if (mesh->mNumAnimMeshes != 0)
		{
			diagnostics.push_back("Mesh " + name + ": Morph Targets are unsupported and were ignored");
		}
		if (mesh->GetNumUVChannels() > 1)
		{
			diagnostics.push_back("Mesh " + name + ": only UV set 0 is supported; additional UV sets were ignored");
		}
		UInt64 materialLocalId = 0;
		if (settings.importMaterials && mesh->mMaterialIndex < scene->mNumMaterials)
		{
			materialLocalId = materialIds.at(mesh->mMaterialIndex);
		}
		auto data = GeometryData(*mesh);
		meshMaterialBindings[localFileId] = {materialLocalId};
		if (mesh->mNumBones != 0)
		{
			if (bonesByName.empty())
			{
				return Result<ModelImportCandidate>::Failure("Skinned mesh has no imported skeleton at " +
				                                             source.ToString());
			}
			Vector<RawSkinInfluence> rawInfluences;
			Map<Ursa::UInt32, Ursa::UInt16> skinSlotsByJoint;
			data.skeletonReference = {guid, skeletonLocalFileId};
			data.skeleton = candidate.skeletons.at(skeletonLocalFileId);
			for (Ursa::UInt32 boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
			{
				const aiBone* bone = mesh->mBones[boneIndex];
				if (!bone || (bone->mNumWeights != 0 && !bone->mWeights))
				{
					return Result<ModelImportCandidate>::Failure("Skinned mesh contains malformed bone data at " +
					                                             source.ToString());
				}
				const String boneName = ToUrsaString(bone->mName.C_Str());
				const auto boneNode = boneNodesByName.find(boneName);
				if (boneNode == boneNodesByName.end())
				{
					return Result<ModelImportCandidate>::Failure("Skin bone " + boneName +
					                                             " is absent from the skeleton");
				}
				const auto joint = jointIndicesByNode.find(boneNode->second);
				if (joint == jointIndicesByNode.end() || boneIndex > std::numeric_limits<Ursa::UInt16>::max() ||
				    joint->second > std::numeric_limits<Ursa::UInt16>::max())
				{
					return Result<ModelImportCandidate>::Failure("Skin bone index exceeds the supported range: " +
					                                             boneName);
				}
				const Ursa::UInt16 skinSlot = static_cast<Ursa::UInt16>(boneIndex);
				if (!skinSlotsByJoint.emplace(joint->second, skinSlot).second)
				{
					return Result<ModelImportCandidate>::Failure("Skin contains a duplicate joint binding: " +
					                                             boneName);
				}
				data.skinJointIndices.push_back(static_cast<Ursa::UInt16>(joint->second));
				Mat4 inverseBind;
				for (Int32 row = 0; row < 4; ++row)
				{
					for (Int32 column = 0; column < 4; ++column)
					{
						inverseBind.At(row, column) = bone->mOffsetMatrix[row][column];
					}
				}
				data.skinInverseBindMatrices.push_back(inverseBind);
				for (Ursa::UInt32 weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
				{
					const aiVertexWeight& weight = bone->mWeights[weightIndex];
					rawInfluences.push_back({weight.mVertexId, skinSlot, static_cast<Ursa::Float32>(weight.mWeight)});
				}
			}
			auto normalizedInfluences =
			    NormalizeSkinInfluences(mesh->mNumVertices, rawInfluences, settings.maxBoneInfluences);
			if (!normalizedInfluences)
			{
				return Result<ModelImportCandidate>::Failure(normalizedInfluences.error + " at " + source.ToString());
			}
			Vector<Ursa::UInt32> influenceCounts(mesh->mNumVertices, 0);
			Ursa::UInt32 truncatedVertices = 0;
			for (const auto& influence : rawInfluences)
			{
				if (influence.weight > 0.0f &&
				    ++influenceCounts[influence.vertexIndex] == settings.maxBoneInfluences + 1)
				{
					++truncatedVertices;
				}
			}
			if (truncatedVertices != 0)
			{
				diagnostics.push_back("Mesh " + ToUrsaString(mesh->mName.C_Str()) + ": influences were truncated to " +
				                      ToUrsaString(std::to_string(settings.maxBoneInfluences)) + " on " +
				                      ToUrsaString(std::to_string(truncatedVertices)) + " vertices");
			}
			for (const auto& influence : normalizedInfluences.value)
			{
				data.skinInfluences.push_back({influence.jointIndices, influence.weights});
			}
		}
		auto geometry = Mesh::Create(std::move(data));
		if (!geometry)
		{
			return Result<ModelImportCandidate>::Failure(geometry.error + " at " + source.ToString());
		}
		auto bound = geometry.value->BindAssetIdentity({guid, localFileId});
		if (!bound)
		{
			return Result<ModelImportCandidate>::Failure(bound.error);
		}
		if (!candidate.meshes.emplace(localFileId, std::move(geometry.value)).second)
		{
			return Result<ModelImportCandidate>::Failure("Mesh output identity collision");
		}
	}
	Vector<Ursa::UInt64> animationClipIds;
	for (Ursa::UInt32 animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
	{
		const aiAnimation* animation = scene->mAnimations[animationIndex];
		if (!animation)
		{
			return Result<ModelImportCandidate>::Failure("Assimp returned a null animation at index " +
			                                             ToUrsaString(std::to_string(animationIndex)));
		}
		const String name = animation->mName.length > 0 ? ToUrsaString(animation->mName.C_Str())
		                                                : "Animation" + ToUrsaString(std::to_string(animationIndex));
		const Ursa::UInt64 localFileId =
		    StableLocalFileId("animation:" + ToUrsaString(std::to_string(animationIndex)) + ":" + name);
		candidate.outputKeys[localFileId] = "animation:" + ToUrsaString(std::to_string(animationIndex)) + ":" + name;
		candidate.displayNames[localFileId] = name;
		const Ursa::Float64 ticksPerSecond = animation->mTicksPerSecond > 0.0 ? animation->mTicksPerSecond : 25.0;
		Ursa::Float64 durationTicks = animation->mDuration;
		Json channels = Json::array();
		if (animation->mNumMeshChannels != 0 || animation->mNumMorphMeshChannels != 0)
		{
			diagnostics.push_back("Animation " + name +
			                      ": mesh and morph animation channels are unsupported and were ignored");
		}
		if (animation->mNumChannels == 0)
		{
			if (animation->mNumMeshChannels != 0 || animation->mNumMorphMeshChannels != 0)
			{
				continue;
			}
			diagnostics.push_back("Animation " + name + ": empty animation clip was ignored");
			continue;
		}
		for (Ursa::UInt32 channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
		{
			const aiNodeAnim* channel = animation->mChannels[channelIndex];
			if (!channel)
			{
				return Result<ModelImportCandidate>::Failure("Animation " + name + " contains a null node channel");
			}
			const String nodeName = ToUrsaString(channel->mNodeName.C_Str());
			const auto matchingNodes = nodesByName.find(nodeName);
			if (matchingNodes == nodesByName.end() || matchingNodes->second.size() != 1)
			{
				return Result<ModelImportCandidate>::Failure("Animation " + name + " node '" + nodeName +
				                                             "' is missing or ambiguous at " + source.ToString());
			}
			AnimationInterpolation positionInterpolation;
			AnimationInterpolation rotationInterpolation;
			AnimationInterpolation scaleInterpolation;
			auto positionKeys = ConvertVectorTrack(channel->mPositionKeys, channel->mNumPositionKeys, ticksPerSecond,
			                                       durationTicks, positionInterpolation);
			auto rotationKeys = ConvertRotationTrack(channel->mRotationKeys, channel->mNumRotationKeys, ticksPerSecond,
			                                         durationTicks, rotationInterpolation);
			auto scaleKeys = ConvertVectorTrack(channel->mScalingKeys, channel->mNumScalingKeys, ticksPerSecond,
			                                    durationTicks, scaleInterpolation);
			if (!positionKeys || !rotationKeys || !scaleKeys)
			{
				return Result<ModelImportCandidate>::Failure("Animation " + name + ": " +
				                                             (!positionKeys   ? positionKeys.error
				                                              : !rotationKeys ? rotationKeys.error
				                                                              : scaleKeys.error));
			}
			Json channelDocument = Json::object();
			channelDocument["nodeLocalFileId"] = nodeIdsByPointer.at(matchingNodes->second[0]);
			channelDocument["positionKeys"] = std::move(positionKeys.value);
			channelDocument["rotationKeys"] = std::move(rotationKeys.value);
			channelDocument["scaleKeys"] = std::move(scaleKeys.value);
			channelDocument["positionInterpolation"] = ToStdString(AnimationModeName(positionInterpolation));
			channelDocument["rotationInterpolation"] = ToStdString(AnimationModeName(rotationInterpolation));
			channelDocument["scaleInterpolation"] = ToStdString(AnimationModeName(scaleInterpolation));
			channels.push_back(std::move(channelDocument));
		}
		if (!std::isfinite(durationTicks) || durationTicks < 0.0)
		{
			return Result<ModelImportCandidate>::Failure("Animation " + name + " has an invalid duration");
		}
		const Ursa::Float64 durationSeconds = durationTicks / ticksPerSecond;
		Json clipDocument = {{"format", "UrsaAnimationClip"},      {"version", 2},
		                     {"localFileId", localFileId},         {"name", ToStdString(name)},
		                     {"durationSeconds", durationSeconds}, {"ticksPerSecond", ticksPerSecond},
		                     {"channels", std::move(channels)}};
		animationClipIds.push_back(localFileId);
		if (!candidate.archives.emplace(localFileId, ModelCandidateArchive{"AnimationClip", std::move(clipDocument)})
		         .second)
		{
			return Result<ModelImportCandidate>::Failure("Animation output identity collision");
		}
	}
	auto visitNode = [&](auto&& self, const aiNode& node, UInt64 parentId) -> Result<UInt64>
	{
		ModelCandidateNode value;
		const String internalName = ToUrsaString(node.mName.C_Str());
		const auto originalName = gltfDisplayNames.find(internalName);
		value.name = originalName == gltfDisplayNames.end() ? internalName : originalName->second;
		value.localFileId = nodeIdsByPointer.at(&node);
		value.parentLocalFileId = parentId;
		aiVector3D scale;
		aiQuaternion rotation;
		aiVector3D position;
		node.mTransformation.Decompose(scale, rotation, position);
		value.transform.position = {position.x, position.y, position.z};
		value.transform.rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
		value.transform.scale = {scale.x, scale.y, scale.z};
		for (UInt32 index = 0; index < node.mNumMeshes; ++index)
		{
			const auto mesh = meshIdMap.find(node.mMeshes[index]);
			if (mesh == meshIdMap.end())
			{
				return Result<UInt64>::Failure("Model node refers to an invalid Mesh");
			}
			value.meshes.push_back(mesh->second);
		}
		for (UInt32 index = 0; index < node.mNumChildren; ++index)
		{
			auto child = self(self, *node.mChildren[index], value.localFileId);
			if (!child)
			{
				return child;
			}
			value.children.push_back(child.value);
		}
		const auto id = value.localFileId;
		candidate.nodes.push_back(std::move(value));
		return Result<UInt64>::Success(id);
	};
	auto rootId = visitNode(visitNode, *scene->mRootNode, 0);
	if (!rootId)
	{
		return Result<ModelImportCandidate>::Failure(rootId.error);
	}
	candidate.rootNodeLocalFileId = rootId.value;
	candidate.meshOrder = std::move(meshIds);
	candidate.animationOrder = std::move(animationClipIds);
	candidate.meshMaterialBindings = std::move(meshMaterialBindings);
	candidate.dependencies = std::move(importedDependencies);
	candidate.diagnostics = std::move(diagnostics);
	Map<UInt64, String> names;
	names[candidate.mainObjectLocalFileId] = source.FileName();
	for (const auto& node : candidate.nodes)
	{
		names[node.localFileId] = node.name.empty() ? "Node" : node.name;
	}
	for (const auto& mesh : candidate.meshes)
	{
		names[mesh.first] = candidate.displayNames.at(mesh.first);
	}
	for (const auto& material : candidate.materials)
	{
		names[material.first] = material.second.name;
	}
	for (const auto& texture : candidate.textures)
	{
		names[texture.first] = candidate.displayNames.at(texture.first);
	}
	for (const auto& archive : candidate.archives)
	{
		const auto name = ToUrsaString(archive.second.document.value("name", std::string{}));
		names[archive.first] = name.empty() ? archive.second.kind : name;
	}
	candidate.displayNames = std::move(names);
	auto valid = candidate.Validate();
	return valid ? Result<ModelImportCandidate>::Success(std::move(candidate))
	             : Result<ModelImportCandidate>::Failure(valid.error);
}

} // namespace Ursa::Editor
