#include <Assets/AssetPreviewExtension.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/SceneResource.hpp>
#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Scripting/Bindings/EditorServices.hpp>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace Ursa::Editor
{
namespace
{
std::atomic<UInt64> nextIdentity{1};

template <class T> T Take(Result<T> value)
{
	return Scripting::Bindings::TakeBindingResult(std::move(value));
}

void Done(Result<void> value)
{
	Scripting::Bindings::CompleteBindingResult(std::move(value));
}

class PreviewRuntimeApi final : public Scripting::ManagedSceneApi
{
public:
	PreviewRuntimeApi(Scene& scene, ResourceSession* selectedResources, std::function<void(UInt32)> validator)
	    : ManagedSceneApi(scene, selectedResources), validator(std::move(validator))
	{
	}

	void Require(UInt32 policy) const override
	{
		validator(policy);
		ManagedSceneApi::Require(policy);
	}

	bool PersistentResourcesReadOnly() const override
	{
		return true;
	}

	bool LocalResourceReferencesReadOnly() const override
	{
		return true;
	}

	void RequireSceneMutation() const override
	{
		Require(0);
		throw std::runtime_error("Preview asset targets are read-only; create instances with PreviewRenderUtility");
	}

	void RequireGameExecution() const override
	{
		Require(0);
		throw std::runtime_error("Preview cannot access game execution, input or untracked IO");
	}

	void RequireRendering() const override
	{
		Require(0);
		throw std::runtime_error("Preview rendering must use PreviewRenderUtility");
	}

private:
	std::function<void(UInt32)> validator;
};
} // namespace

AssetPreviewExtension::AssetPreviewExtension(String selectedId, Callback selectedCallback)
    : id(std::move(selectedId)), instance(nextIdentity++), callback(std::move(selectedCallback))
{
}

AssetPreviewExtension::~AssetPreviewExtension()
{
	// Cleanup must not capture an expired Inspector draft or save closure.
	context.serializedTarget.reset();
	context.validateTarget = {};
	context.selection = {};
	context.document = nullptr;
	context.documents = nullptr;
	context.target = nullptr;
	context.targetUsesDocument = false;
	(void)Invoke(Scripting::ManagedPreviewPhase::Release);
}

void AssetPreviewExtension::Bind(const ManagedEditorContext& source)
{
	context = source;
	context.canEditTarget = false;
	context.canApplyImporter = false;
	context.saveTarget = {};
	context.markTargetDirty = {};
	context.requestImport = {};
	context.openAsset = {};
	context.preview = this;
}

Result<Scripting::ManagedPreviewResult> AssetPreviewExtension::Invoke(Scripting::ManagedPreviewPhase phase,
                                                                      const ViewportRect& rect)
{
	guiAllowed = phase == Scripting::ManagedPreviewPhase::Settings;
	if (phase == Scripting::ManagedPreviewPhase::Draw)
	{
		bounds = rect;
		pendingImage.reset();
		renderedUtility = 0;
		activeUtility = 0;
		drawing = true;
	}
	auto result = callback(instance, phase, rect, context);
	drawing = false;
	guiAllowed = false;
	if (result && activeUtility)
	{
		result = Result<Scripting::ManagedPreviewResult>::Failure("BeginPreview requires a matching EndPreview");
	}
	activeUtility = 0;
	if (!result)
	{
		pendingImage.reset();
		renderedUtility = 0;
		const auto target = context.assetSelection
		                        ? context.assetSelection->guid.ToString() + ":" +
		                              ToUrsaString(std::to_string(context.assetSelection->localFileId))
		                        : String("<closed target>");
		result.error = "Preview " + id + " for " + target + ": " + result.error;
	}
	return result;
}

Scripting::ManagedSceneApi& AssetPreviewExtension::RuntimeApi()
{
	if (!activeInvocation)
	{
		throw std::runtime_error("Preview Runtime capability requires an active callback");
	}
	if (!runtimeApi)
	{
		if (context.workspace)
		{
			resources =
			    Take(ResourceSession::Open(context.workspace->ContentFiles(), context.workspace->AssetResolver()));
		}
		runtimeApi = std::make_unique<PreviewRuntimeApi>(resourceScene, resources.get(),
		                                                 [this](UInt32 policy)
		                                                 {
			                                                 if (!activeInvocation)
			                                                 {
				                                                 throw std::runtime_error(
				                                                     "Preview capability is outside its callback");
			                                                 }
			                                                 activeInvocation->Require(policy);
		                                                 });
		if (!context.selection.scene && !context.selection.roots.empty())
		{
			runtimeApi->SetDetachedRoots(context.selection.roots);
		}
	}
	Done(runtimeApi->BindHostServices(activeInvocation->hostToken));
	return *runtimeApi;
}

Result<bool> AssetPreviewExtension::DrawInspector(ManagedEditorContext& source)
{
	auto propertiesContext = source;
	propertiesContext.preview = this;
	propertiesContext.previewProperties = true;
	guiAllowed = true;
	const auto result = callback(instance, Scripting::ManagedPreviewPhase::Inspector, {}, propertiesContext);
	source.importerControlsDrawn = propertiesContext.importerControlsDrawn;
	guiAllowed = false;
	return result ? Result<bool>::Success(true) : Result<bool>::Failure(result.error);
}

UInt64 AssetPreviewExtension::CreateUtility()
{
	if (utilities.size() >= 16)
	{
		throw std::runtime_error("A Preview cannot own more than 16 render utilities");
	}
	const auto token = nextIdentity++;
	auto utility = std::make_unique<Utility>();
	auto light = Take(utility->scene.CreateGameObject("Preview light"));
	utility->light = Take(light->AddComponent<DirectionalLightComponent>());
	Done(utility->light->SetIntensity(2));
	Done(utility->scene.SetAmbientLight({1, 1, 1, 1}, .4f));
	utility->camera.position = {0, 0, 3};
	utilities.emplace(token, std::move(utility));
	return token;
}

AssetPreviewExtension::Utility& AssetPreviewExtension::FindUtility(UInt64 utility)
{
	const auto found = utilities.find(utility);
	if (found == utilities.end())
	{
		throw std::runtime_error("PreviewRenderUtility is closed or belongs to another target");
	}
	return *found->second;
}

void AssetPreviewExtension::DestroyUtility(UInt64 utility)
{
	if (activeUtility == utility)
	{
		activeUtility = 0;
	}
	FindUtility(utility);
	utilities.erase(utility);
	if (renderedUtility == utility)
	{
		renderedUtility = 0;
	}
}

void AssetPreviewExtension::RequireDraw() const
{
	if (!drawing)
	{
		throw std::runtime_error("Preview drawing is available only inside OnPreviewGUI");
	}
}

void AssetPreviewExtension::CheckRect(const ViewportRect& rect) const
{
	RequireDraw();
	if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
	    rect.width <= 0 || rect.height <= 0 || rect.x < bounds.x || rect.y < bounds.y ||
	    rect.x + rect.width > bounds.x + bounds.width + .01f || rect.y + rect.height > bounds.y + bounds.height + .01f)
	{
		throw std::runtime_error("Preview Rect must be finite and contained in the host image region");
	}
}

void AssetPreviewExtension::BeginUtility(UInt64 utility, const ViewportRect& rect)
{
	CheckRect(rect);
	if (activeUtility || renderedUtility || pendingImage)
	{
		throw std::runtime_error("Only one output may be published per Preview callback");
	}
	auto& target = FindUtility(utility);
	for (const auto& object : target.frameObjects)
	{
		Done(target.scene.DestroyGameObject(object));
	}
	target.frameObjects.clear();
	Done(target.scene.CommitChanges());
	activeUtility = utility;
	outputRect = rect;
}

void AssetPreviewExtension::SetCamera(UInt64 utility, const Camera& camera)
{
	for (auto value :
	     {camera.position.x, camera.position.y, camera.position.z, camera.target.x, camera.target.y, camera.target.z,
	      camera.up.x, camera.up.y, camera.up.z, camera.verticalFovRadians, camera.nearPlane, camera.farPlane})
	{
		if (!std::isfinite(value))
		{
			throw std::runtime_error("Preview camera values must be finite");
		}
	}
	if (camera.nearPlane <= 0 || camera.farPlane <= camera.nearPlane || camera.verticalFovRadians <= .001f ||
	    camera.verticalFovRadians >= 3.13f ||
	    Dot(camera.position - camera.target, camera.position - camera.target) < 1e-10f ||
	    Dot(camera.up, camera.up) < 1e-10f)
	{
		throw std::runtime_error("Invalid Preview camera projection or view");
	}
	FindUtility(utility).camera = camera;
}

void AssetPreviewExtension::SetLighting(UInt64 utility, Color ambient, Float32 intensity, Color key,
                                        Float32 keyIntensity, Quat rotation)
{
	auto& target = FindUtility(utility);
	Done(target.scene.SetAmbientLight(ambient, intensity));
	Done(target.light->SetColor(key));
	Done(target.light->SetIntensity(keyIntensity));
	auto object = target.light->GetGameObject();
	auto transform = Take(object->GetLocalTransform());
	transform.rotation = rotation;
	Done(object->SetLocalTransform(transform));
}

void AssetPreviewExtension::DrawMesh(UInt64 utility, MeshPtr mesh, std::shared_ptr<MaterialInstance> material,
                                     LocalTransform transform)
{
	RequireDraw();
	if (activeUtility != utility || !mesh || !material)
	{
		throw std::runtime_error("DrawMesh requires an active Preview and valid resources");
	}
	auto& scene = FindUtility(utility).scene;
	auto object = Take(scene.CreateGameObject("Preview mesh"));
	FindUtility(utility).frameObjects.push_back(object);
	Done(object->SetLocalTransform(transform));
	Done(Take(object->AddComponent<MeshFilterComponent>())->SetSharedMesh(std::move(mesh)));
	Done(Take(object->AddComponent<MeshRendererComponent>())->SetSharedMaterials({std::move(material)}));
}

void AssetPreviewExtension::Instantiate(UInt64 utility, const AssetReference& prefab)
{
	RequireDraw();
	if (activeUtility != utility || !context.workspace)
	{
		throw std::runtime_error("Prefab instantiation requires an active project Preview");
	}
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	auto prefabResources =
	    Take(ResourceSession::Open(context.workspace->ContentFiles(), context.workspace->AssetResolver()));
	ComponentLoadContext loadContext(*prefabResources);
	auto path = Take(prefabResources->ResolveAsset(prefab.guid, prefab.localFileId));
	auto asset = Take(LoadPrefabAssetResource(prefabResources->Index().Filesystem(), path, registry, loadContext));
	auto& target = FindUtility(utility);
	target.frameObjects.push_back(Take(asset.Instantiate(target.scene, registry, loadContext)));
}

void AssetPreviewExtension::EndUtility(UInt64 utility)
{
	RequireDraw();
	if (activeUtility != utility)
	{
		throw std::runtime_error("EndPreview does not match BeginPreview");
	}
	Done(FindUtility(utility).scene.CommitChanges());
	activeUtility = 0;
	renderedUtility = utility;
}

void AssetPreviewExtension::DrawTexture(MaterialTexturePtr texture, const ViewportRect& rect)
{
	CheckRect(rect);
	if (!texture || pendingImage || activeUtility || renderedUtility)
	{
		throw std::runtime_error("Preview image is null or an output is already recorded");
	}
	pendingImage = std::move(texture);
	outputRect = rect;
}

Scene* AssetPreviewExtension::RenderScene() const
{
	const auto found = utilities.find(renderedUtility);
	return found == utilities.end() ? nullptr : &found->second->scene;
}

const Camera& AssetPreviewExtension::RenderCamera() const
{
	return utilities.at(renderedUtility)->camera;
}

void AssetPreviewExtension::SetInput(ViewportPoint selectedMouse, ViewportPoint delta, Float32 selectedWheel,
                                     bool selectedDragging, Float64 selectedTime, Float64 selectedDeltaTime)
{
	mouse = selectedMouse;
	mouseDelta = delta;
	wheel = selectedWheel;
	dragging = selectedDragging;
	time = selectedTime;
	deltaTime = selectedDeltaTime;
}
} // namespace Ursa::Editor
