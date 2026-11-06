#pragma once

#include <Application/SceneBuildSettings.hpp>
#include <Scene/Scene.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <optional>

namespace Ursa
{
struct RuntimeScene
{
	BuildScene asset;
	UInt64 token = 0;
	bool loaded = false;
};

// The Scene container and script environment belong to one Run. Single loads
// replace only its graph at a drawable host-frame boundary.
class RuntimeSceneManager final
{
public:
	RuntimeSceneManager(Scene& graph, ResourceSession& resources, SceneBuildSettings settings, BuildScene initial);
	Result<void> Load(const String& nameOrPath, UInt32 mode = 0);
	Result<void> Load(Int32 index, UInt32 mode = 0);
	Result<void> Load(AssetReference reference, UInt32 mode = 0);
	Result<bool> ActivatePending(const FrameContext& context);
	Result<void> Initialize(const FrameContext& context);
	void Close();

	const RuntimeScene& ActiveScene() const
	{
		return active;
	}

	bool IsLoading() const
	{
		return processing || pending.has_value();
	}

	const String& LastLoadError() const
	{
		return lastError;
	}

	const String& LastDiagnostic() const
	{
		return diagnostic;
	}

	bool IsLoaded(UInt64 token) const
	{
		return active.loaded && active.token == token;
	}

	std::function<Result<void>(Scene&)> validateCandidate;
	std::function<Result<void>()> invalidateOldObjects;
	std::function<void()> cancelInput;
	std::function<Result<void>(const RuntimeScene&, bool)> notify;

private:
	Result<void> Queue(Result<BuildScene> target, UInt32 mode);
	Result<void> Fail(String error);
	Scene& graph;
	ResourceSession& resources;
	SceneBuildSettings settings;
	ComponentRegistry registry;
	RuntimeScene active;
	std::optional<BuildScene> pending;
	std::optional<BuildScene> processingTarget;
	String lastError;
	String diagnostic;
	bool processing = false;
	bool activating = false;
	bool initialized = false;
	bool closed = false;
};
} // namespace Ursa
