#include <Core/Types.hpp>
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
	const std::filesystem::path virtualPath = "play-clone.ursascene";
	auto encoded = EncodeSceneDocument(virtualPath, documentId, draft.GetScene(), registry, resolveMaterial);
	if (!encoded)
	{
		return Result<void>::Failure("Cannot clone edit scene: " + encoded.error);
	}
	ComponentLoadContext context(resources);
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
	services = std::move(preparedServices);
	scene = std::move(decoded.value.scene);
	scheduler.Reset();
	frameIndex = 0;
	elapsedSeconds = 0.0;
	return {};
}

Result<void> PlaySession::Tick(Ursa::Float32 deltaSeconds, Window* window, FrameInput input)
{
	if (!scene)
	{
		return Result<void>::Failure("Play mode is not active");
	}
	const auto schedule = scheduler.Advance(deltaSeconds);
	elapsedSeconds += schedule.deltaSeconds;
	FrameContext context;
	context.deltaSeconds = schedule.deltaSeconds;
	context.fixedDeltaSeconds = scheduler.FixedSeconds();
	context.elapsedSeconds = elapsedSeconds;
	context.frameIndex = ++frameIndex;
	context.window = window;
	context.input = input;
	scene->BeginFrame();
	auto result = scene->Update(context);
	for (Ursa::UInt32 step = 0; result && step < schedule.fixedSteps; ++step)
	{
		result = scene->FixedUpdate(context);
	}
	if (result)
	{
		result = scene->LateUpdate(context);
	}
	return result;
}

void PlaySession::Stop()
{
	if (scene)
	{
		scene->Clear();
	}
	scene.reset();
	services.reset();
	scheduler.Reset();
	frameIndex = 0;
	elapsedSeconds = 0.0;
}
} // namespace Ursa::Editor
