#pragma once

#include <Assets/AssetDatabase.hpp>

namespace Ursa::Editor
{
struct ModelImportSettings
{
	Ursa::Float32 scaleFactor = 1.0f;
	bool generateNormals = true;
	bool generateTangents = true;
	bool flipUvs = false;
	bool importMaterials = true;
	bool importTextures = true;
	Ursa::UInt32 maxBoneInfluences = 4;

	nlohmann::json ToJson() const;

	static Result<ModelImportSettings> FromJson(const nlohmann::json& value);
};

struct ModelImportResult
{
	ResourceId guid;
	VirtualPath artifactPath;
	Vector<ImportedArtifactLocation> subAssets;
	Vector<VirtualPath> dependencies;
	bool cacheHit = false;
	Vector<String> diagnostics;
};

struct RawSkinInfluence
{
	Ursa::UInt32 vertexIndex = 0;
	Ursa::UInt32 jointIndex = 0;
	Ursa::Float32 weight = 0.0f;
};

struct ModelSkinInfluence
{
	Array<Ursa::UInt16, 4> jointIndices{};
	Array<Ursa::Float32, 4> weights{};
};

class ModelImporter final
{
public:
	static Result<Vector<ModelSkinInfluence>> NormalizeSkinInfluences(Ursa::UInt32 vertexCount,
	                                                                  const Vector<RawSkinInfluence>& influences,
	                                                                  Ursa::UInt32 maximumInfluences);

	static Result<ModelImportSettings> Settings(const AssetDatabase& database, const VirtualPath& source);

	static Result<void> SetSettings(AssetDatabase& database, const VirtualPath& source,
	                                const ModelImportSettings& settings);

	static Result<ModelImportResult> Import(AssetDatabase& database, const VirtualPath& source,
	                                        const ModelImportSettings& settings);
};
} // namespace Ursa::Editor
