#include <Core/Types.hpp>
#include <Core/Profiler.hpp>
#include <Application/PlaySession.hpp>
#include <Serialization/SceneResource.hpp>

namespace Ursa::Editor
{
FrameInput SelectPlayInput(FrameInput polled, bool viewportFocused, bool editorTextActive)
{
	return viewportFocused && !editorTextActive ? polled : FrameInput{};
}

Result<void> PlaySession::Start(const SceneDraft& draft, ResourceId documentId, const ComponentRegistry& registry,
                                ResourceSession& resources, const PlayContextSetup& setup)
{
	URSA_PROFILE_DIAGNOSTIC_SCOPE("Editor.Play.Start");
	URSA_PROFILE_OPERATION("Editor.Play.Start", "", "begin");
	const auto start = [&]() -> Result<void>
	{
		if (Active())
		{
			return Result<void>::Failure("Play mode is already active");
		}
		if (draft.Kind() != ResourceKind::Scene)
		{
			return Result<void>::Failure("Play mode requires a scene document");
		}
		const MaterialIdResolver resolveMaterial = [&draft](const std::shared_ptr<MaterialInstance>& material)
		{ return draft.MaterialId(material); };
		const String virtualPath = "play-clone.ursascene";
		auto encoded = EncodeSceneDocument(virtualPath, documentId, draft.GetScene(), registry, resolveMaterial);
		if (!encoded)
		{
			return Result<void>::Failure("Cannot clone edit scene: " + encoded.error);
		}
		auto isolated = resources.Isolated();
		if (!isolated)
		{
			return Result<void>::Failure(isolated.error);
		}
		ComponentLoadContext context(*isolated.value);
		std::shared_ptr<void> preparedServices;
		if (setup)
		{
			auto prepared = setup(context);
			if (!prepared)
			{
				return Result<void>::Failure("Cannot create play services: " + prepared.error);
			}
			preparedServices = std::move(prepared.value);
		}
		auto decoded = DecodeSceneDocument(virtualPath, encoded.value, registry, context);
		if (!decoded)
		{
			return Result<void>::Failure("Cannot clone edit scene: " + decoded.error);
		}
		auto sceneReady = context.NotifySceneReady(*decoded.value.scene);
		if (!sceneReady)
		{
			return Result<void>::Failure("Cannot initialize the play scene: " + sceneReady.error);
		}
		resourceSession = std::move(isolated.value);
		services = std::move(preparedServices);
		scene = std::move(decoded.value.scene);
		frameRunner.Reset();
		return {};
	};
	auto result = start();
	URSA_PROFILE_OPERATION("Editor.Play.Start", "", result ? "complete" : result.error.c_str());
	return result;
}

Result<void> PlaySession::Tick(Ursa::Float32 deltaSeconds, Window* window, FrameInput input)
{
	if (!scene)
	{
		return Result<void>::Failure("Play mode is not active");
	}
	if (paused && !stepRequested)
	{
		return {};
	}
	stepRequested = false;
	return frameRunner.Tick(*scene, deltaSeconds, window, input);
}

void PlaySession::Stop()
{
	URSA_PROFILE_DIAGNOSTIC_SCOPE("Editor.Play.Stop");
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	const bool wasActive = Active();
#endif
	if (scene)
	{
		scene->Clear();
	}
	scene.reset();
	services.reset();
	resourceSession.reset();
	frameRunner.Reset();
	paused = false;
	stepRequested = false;
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	if (wasActive)
	{
		URSA_PROFILE_OPERATION("Editor.Play.Stop", "", "complete");
	}
#endif
}
} // namespace Ursa::Editor
