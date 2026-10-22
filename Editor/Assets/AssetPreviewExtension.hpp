#pragma once

#include <Assets/AssetPreviewState.hpp>
#include <Scripting/ManagedEditorBridge.hpp>
#include <Scene/RenderingComponents.hpp>
#include <functional>

namespace Ursa::Editor
{
namespace Bindings
{
class EditorInvocation;
}

// The panel owns this provider. Script callbacks only record commands; the panel publishes their GPU result.
class AssetPreviewExtension final
{
public:
	using Callback = std::function<Result<Scripting::ManagedPreviewResult>(UInt64, Scripting::ManagedPreviewPhase,
	                                                                       const ViewportRect&, ManagedEditorContext&)>;
	AssetPreviewExtension(String selectedId, Callback selectedCallback);
	~AssetPreviewExtension();
	void Bind(const ManagedEditorContext& source);
	Result<Scripting::ManagedPreviewResult> Invoke(Scripting::ManagedPreviewPhase phase, const ViewportRect& rect = {});
	Result<bool> DrawInspector(ManagedEditorContext& source);

	const String& Id() const
	{
		return id;
	}

	UInt64 Instance() const
	{
		return instance;
	}

	UInt64 CreateUtility();
	void DestroyUtility(UInt64 utility);
	void BeginUtility(UInt64 utility, const ViewportRect& rect);
	void SetCamera(UInt64 utility, const Camera& camera);
	void SetLighting(UInt64 utility, Color ambient, Float32 intensity, Color key, Float32 keyIntensity, Quat rotation);
	void DrawMesh(UInt64 utility, MeshPtr mesh, std::shared_ptr<MaterialInstance> material, LocalTransform transform);
	void Instantiate(UInt64 utility, const AssetReference& prefab);
	void EndUtility(UInt64 utility);
	void DrawTexture(MaterialTexturePtr texture, const ViewportRect& rect);

	const MaterialTexturePtr& Image() const
	{
		return pendingImage;
	}

	Scene* RenderScene() const;
	const Camera& RenderCamera() const;

	const ViewportRect& OutputRect() const
	{
		return outputRect;
	}

	void SetInput(ViewportPoint mouse, ViewportPoint delta, Float32 wheel, bool dragging, Float64 time,
	              Float64 deltaTime);

	ViewportPoint Mouse() const
	{
		return mouse;
	}

	ViewportPoint Delta() const
	{
		return mouseDelta;
	}

	Float32 Wheel() const
	{
		return wheel;
	}

	bool Dragging() const
	{
		return dragging;
	}

	Float64 Time() const
	{
		return time;
	}

	Float64 DeltaTime() const
	{
		return deltaTime;
	}

	bool Drawing() const
	{
		return drawing;
	}

	bool GuiAllowed() const
	{
		return guiAllowed;
	}

	void RequestRepaint()
	{
		repaintRequested = true;
	}

	bool TakeRepaintRequest()
	{
		return std::exchange(repaintRequested, false);
	}

	void SetInvocation(Bindings::EditorInvocation* value)
	{
		activeInvocation = value;
	}

	Scripting::ManagedSceneApi& RuntimeApi();

	const ViewportRect& Bounds() const
	{
		return bounds;
	}

private:
	struct Utility
	{
		Scene scene;
		Camera camera;
		DirectionalLightComponent* light = nullptr;
		Vector<GameObject::Ptr> frameObjects;
	};

	Utility& FindUtility(UInt64 utility);
	void RequireDraw() const;
	void CheckRect(const ViewportRect& rect) const;
	String id;
	UInt64 instance;
	Callback callback;
	ManagedEditorContext context;
	Bindings::EditorInvocation* activeInvocation = nullptr;
	Scene resourceScene;
	std::unique_ptr<ResourceSession> resources;
	std::unique_ptr<Scripting::ManagedSceneApi> runtimeApi;
	UnorderedMap<UInt64, std::unique_ptr<Utility>> utilities;
	UInt64 activeUtility = 0;
	UInt64 renderedUtility = 0;
	MaterialTexturePtr pendingImage;
	ViewportRect bounds;
	ViewportRect outputRect;
	ViewportPoint mouse;
	ViewportPoint mouseDelta;
	Float32 wheel = 0;
	Float64 time = 0;
	Float64 deltaTime = 0;
	bool dragging = false;
	bool drawing = false;
	bool guiAllowed = false;
	bool repaintRequested = false;
};
} // namespace Ursa::Editor
