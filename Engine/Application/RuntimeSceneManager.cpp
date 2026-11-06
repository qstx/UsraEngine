#include <Application/RuntimeSceneManager.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <atomic>
#include <stdexcept>

namespace Ursa
{
namespace
{
std::atomic<UInt64> nextSceneToken{1};

bool Same(const BuildScene& a, const BuildScene& b)
{
	return a.reference.guid == b.reference.guid && a.reference.localFileId == b.reference.localFileId;
}
} // namespace

RuntimeSceneManager::RuntimeSceneManager(Scene& scene, ResourceSession& session, SceneBuildSettings list,
                                         BuildScene initial)
    : graph(scene), resources(session), settings(std::move(list))
{
	const auto registered = RegisterBuiltinComponents(registry);
	if (!registered)
	{
		throw std::runtime_error(registered.error.c_str());
	}
	active = {std::move(initial), nextSceneToken.fetch_add(1), true};
}

Result<void> RuntimeSceneManager::Fail(String error)
{
	lastError = std::move(error);
	return Result<void>::Failure(lastError);
}

Result<void> RuntimeSceneManager::Load(const String& nameOrPath, UInt32 mode)
{
	diagnostic.clear();
	return Queue(settings.Resolve(nameOrPath, &diagnostic), mode);
}

Result<void> RuntimeSceneManager::Load(Int32 index, UInt32 mode)
{
	diagnostic.clear();
	return Queue(settings.Resolve(index), mode);
}

Result<void> RuntimeSceneManager::Load(AssetReference reference, UInt32 mode)
{
	diagnostic.clear();
	return Queue(settings.Resolve(reference), mode);
}

Result<void> RuntimeSceneManager::Queue(Result<BuildScene> target, UInt32 mode)
{
	if (closed)
	{
		return Fail("Scene manager belongs to a closed Run");
	}
	if (mode != 0)
	{
		return Fail("Only LoadSceneMode.Single is supported; Additive is not implemented");
	}
	if (!target)
	{
		return Fail(target.error);
	}
	if (processingTarget && !activating && Same(*processingTarget, target.value))
	{
		lastError.clear();
		return {};
	}
	if (pending)
	{
		if (Same(*pending, target.value))
		{
			lastError.clear();
			return {};
		}
		return Fail("A different Scene load is already pending");
	}
	if (processing && !activating)
	{
		return Fail("A different Scene load is already being prepared");
	}
	pending = std::move(target.value);
	lastError.clear();
	return {};
}

Result<void> RuntimeSceneManager::Initialize(const FrameContext& context)
{
	if (closed)
	{
		return Fail("Scene manager belongs to a closed Run");
	}
	if (initialized)
	{
		return {};
	}
	auto prepared = graph.PrepareFrame(context, false);
	if (!prepared)
	{
		return prepared;
	}
	initialized = true;
	if (notify)
	{
		auto notified = notify(active, true);
		if (!notified)
		{
			return notified;
		}
	}
	return graph.PrepareFrame(context);
}

Result<bool> RuntimeSceneManager::ActivatePending(const FrameContext& context)
{
	if (closed)
	{
		return Result<bool>::Failure("Scene manager belongs to a closed Run");
	}
	if (processing)
	{
		return Result<bool>::Failure("Scene activation cannot be reentered");
	}
	if (!pending)
	{
		return Result<bool>::Success(false);
	}
	processing = true;
	processingTarget = std::move(pending);
	pending.reset();

	struct ProcessingScope
	{
		RuntimeSceneManager& owner;

		~ProcessingScope()
		{
			owner.processing = false;
			owner.activating = false;
			owner.processingTarget.reset();
		}
	} scope{*this};

	const BuildScene target = *processingTarget;
	std::unique_ptr<Scene> candidate;
	try
	{
		auto& files = resources.Index().Filesystem();
		auto document = ReadJsonResource(files, target.path, ResourceKind::Scene);
		if (!document)
		{
			lastError = target.path.ToString() + ": " + document.error;
			return Result<bool>::Success(false);
		}
		ComponentLoadContext load(resources);
		if (document.value.contains("instances"))
		{
			auto decoded = DecodeLinkedSceneDocument(target.path.ToString(), std::move(document.value), registry, load);
			if (!decoded)
			{
				lastError = target.path.ToString() + ": " + decoded.error;
				return Result<bool>::Success(false);
			}
			candidate = std::move(decoded.value.scene);
		}
		else
		{
			auto decoded = DecodeSceneDocument(target.path.ToString(), document.value, registry, load);
			if (!decoded)
			{
				lastError = target.path.ToString() + ": " + decoded.error;
				return Result<bool>::Success(false);
			}
			candidate = std::move(decoded.value.scene);
		}
		if (validateCandidate)
		{
			auto valid = validateCandidate(*candidate);
			if (!valid)
			{
				lastError = target.path.ToString() + ": " + valid.error;
				return Result<bool>::Success(false);
			}
		}
	}
	catch (const std::exception& error)
	{
		lastError = target.path.ToString() + ": " + error.what();
		return Result<bool>::Success(false);
	}
	// All preparation failures above preserve the active graph. From this point
	// onward a native/host failure is fatal; user lifecycle cannot be rolled back.
	activating = true;
	if (cancelInput)
	{
		cancelInput();
	}
	graph.Clear();
	active.loaded = false;
	if (invalidateOldObjects)
	{
		auto invalidated = invalidateOldObjects();
		if (!invalidated)
		{
			return Result<bool>::Failure(invalidated.error);
		}
	}
	if (notify)
	{
		auto unloaded = notify(active, false);
		if (!unloaded)
		{
			return Result<bool>::Failure(unloaded.error);
		}
	}
	auto installed = graph.ReplaceGraph(*candidate);
	if (!installed)
	{
		return Result<bool>::Failure(installed.error);
	}
	active = {target, nextSceneToken.fetch_add(1), true};
	initialized = false;
	FrameContext activation = context;
	activation.input = {};
	activation.gui = nullptr;
	auto ready = Initialize(activation);
	if (!ready)
	{
		return Result<bool>::Failure(ready.error);
	}
	return Result<bool>::Success(true);
}

void RuntimeSceneManager::Close()
{
	closed = true;
	active.loaded = false;
	pending.reset();
	processingTarget.reset();
	processing = false;
	validateCandidate = {};
	invalidateOldObjects = {};
	cancelInput = {};
	notify = {};
}
} // namespace Ursa
