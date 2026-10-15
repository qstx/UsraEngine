#pragma once

#include <Rendering/Commands/RenderCommandProtocol.hpp>
#include <Rendering/Scene/RenderScene.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Rendering/Shader/ShaderPassSelection.hpp>
#include <thread>

namespace Ursa::Rendering
{
struct ShaderTagId
{
	String name;
};

enum class SortingCriteria
{
	None,
	CommonOpaque,
	CommonTransparent
};

struct SortingSettings
{
	SortingCriteria criteria = SortingCriteria::CommonOpaque;
	Vec3 cameraPosition;
};

struct DrawingSettings
{
	Vector<ShaderTagId> shaderTags;
	SortingSettings sorting;
	std::shared_ptr<MaterialInstance> overrideMaterial;
	Int32 overrideMaterialPass = -1;
	String activePipeline = "UrsaForward";
};

struct FilteringSettings
{
	Int32 minimumQueue = 0;
	Int32 maximumQueue = 5000;
	UInt32 layerMask = 0xffffffffu;
};

struct CullingResults
{
	RenderContextToken context;
	UInt32 index = 0;
	UInt32 generation = 0;
};

struct RendererList
{
	RenderContextToken context;
	UInt32 index = 0;
	UInt32 generation = 0;
};

struct RendererListEntry
{
	UInt32 objectIndex = 0;
	std::shared_ptr<MaterialInstance> material;
	UInt32 subShaderIndex = 0;
	UInt32 passIndex = 0;
	Int32 renderQueue = 2000;
	Float32 distanceSquared = 0;
	String activePipeline = "UrsaForward";
	std::optional<ResolvedGraphicsPass> resolvedPass;
};

struct ShaderPassSelectionCacheEntry
{
	bool matched = false;
	UInt32 subShaderIndex = 0;
	UInt32 passIndex = 0;
	std::optional<ResolvedGraphicsPass> resolvedPass;
};

class RenderSceneContext final
{
public:
	RenderSceneContext();

	Result<RenderContextToken> Begin(UInt64 sessionId, UInt64 frameId, const RenderScene& scene);

	Result<void> End();

	Result<CullingResults> Cull(const CameraData& camera);

	Result<RendererList> CreateRendererList(CullingResults results, const DrawingSettings& drawing,
	                                        const FilteringSettings& filtering);

	Result<const Vector<UInt32>*> VisibleObjects(CullingResults results) const;

	Result<const Vector<RendererListEntry>*> Entries(RendererList list) const;

	Result<void> Validate(RenderContextToken token) const;

	const RenderScene* Scene() const
	{
		return scene;
	}

private:
	struct Visibility
	{
		Vector<UInt32> indices;
		UInt32 generation = 0;
	};

	struct List
	{
		Vector<RendererListEntry> entries;
		UInt32 generation = 0;
	};

	std::thread::id ownerThread;
	RenderContextToken context;
	const RenderScene* scene = nullptr;
	UInt64 sceneVersion = 0;
	bool active = false;
	Vector<UniquePtr<Visibility>> visibility;
	Vector<UniquePtr<List>> lists;
	Map<String, ShaderPassSelectionCacheEntry> passSelectionCache;
	Size visibilityCount = 0;
	Size listCount = 0;
};

static_assert(sizeof(CullingResults) == 32 && sizeof(RendererList) == 32, "Render result ABI changed");
} // namespace Ursa::Rendering
