#pragma once

#include <ScriptableAssets/ScriptableAsset.hpp>
#include <Serialization/AssetReference.hpp>
#include <Serialization/ResourceSession.hpp>

namespace Ursa
{
inline constexpr UInt32 scriptableAssetFileVersion = 1;

Result<void> SaveScriptableAssetSource(VirtualFileSystem& files, const VirtualPath& path, const ScriptableAsset& asset);

Result<ScriptableAsset> LoadScriptableAssetSource(const VirtualFileSystem& files, const VirtualPath& path,
                                                  ResourceId metadataGuid);

Result<Vector<UInt8>> EncodeScriptableAssetArtifact(const ScriptableAsset& asset);

Result<ScriptableAsset> DecodeScriptableAssetArtifact(const Vector<UInt8>& bytes);

Result<Vector<AssetReference>> ScriptableAssetDependencies(const ScriptableAsset& asset);

struct ScriptableAssetGraph
{
	AssetReference root;
	Vector<ScriptableAsset> assets;
};

Result<ScriptableAssetGraph> LoadScriptableAssetGraph(ResourceSession& resources, AssetReference root);
} // namespace Ursa
