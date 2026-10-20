#pragma once

#include <Assets/AssetPreviewContent.hpp>
#include <Assets/AssetPreviewTexture.hpp>
#include <Assets/MaterialPreview.hpp>
#include <Scene/ViewportLayout.hpp>

namespace Ursa::Editor
{
// The Inspector owns layout; this session owns only disposable viewing state.
class AssetPreviewPanel final
{
public:
	AssetPreviewPanel(Rhi::DevicePtr selectedDevice, ImGuiBridge& bridge);
	~AssetPreviewPanel();
	Result<void> BeginFrame();
	Result<void> Prepare(ProjectWorkspace& workspace, const ProjectBrowserModel& browser,
	                     const EditorSelectionContext& selection, const EditorDocument* document,
	                     const String& projectKey);

	bool Supported() const
	{
		return state.Target().Supported();
	}

	Float32 ReservedHeight() const;

	const String& SettingsDiagnostic() const
	{
		return settingsDiagnostic;
	}

	void Expand();
	void Draw(ProjectWorkspace& workspace, ComponentRegistry& registry,
	          const AssetPreviewTextureInput* textureInput = nullptr, Float32 maximumHeight = 1200);
	Result<void> RenderPending(Float64 deltaSeconds, const EditorRenderViewport& render);
	void Reload(bool pipelineChanged = false);

	const AssetPreviewState& State() const
	{
		return state;
	}

	const AssetPreviewClock& Clock() const
	{
		return clock;
	}

	const ViewportRect& ImageRect() const
	{
		return imageRect;
	}

	const ViewportRect& TimelineRect() const
	{
		return timelineRect;
	}

	const ViewportRect& DividerRect() const
	{
		return dividerRect;
	}

	ViewportSize DisplayedTextureSize() const
	{
		return textureSize;
	}

	UInt64 RenderCount() const
	{
		return renderCount;
	}

	AssetPreviewContent* Content() const
	{
		return content.get();
	}

private:
	Result<void> ReleaseImages();
	void ResetView();
	void DrawControls(ProjectWorkspace& workspace);
	void SaveSettings();
	Result<void> LoadContent(ProjectWorkspace& workspace, ComponentRegistry& registry,
	                         const AssetPreviewTextureInput* textureInput);
	Result<void> UploadTexture();
	Camera PreviewCamera() const;

	Rhi::DevicePtr device;
	ImGuiBridge& gui;
	AssetPreviewState state;
	AssetPreviewClock clock;
	std::unique_ptr<AssetPreviewContent> content;
	std::unique_ptr<AssetPreviewContent> candidate;
	std::unique_ptr<MaterialPreview> front;
	std::unique_ptr<MaterialPreview> back;
	Rhi::TexturePtr texture;
	Rhi::SamplerPtr sampler;
	ImTextureID textureId = ImTextureID_Invalid;
	Vector<ImTextureID> retired;
	Vector<std::unique_ptr<AssetPreviewContent>> retiredContents;
	const ProjectWorkspace* preparedWorkspace = nullptr;
	ResourceId projectSession = ResourceId::Generate();
	UInt64 pipelineVersion = 0;
	VirtualFileSystem* settingsFiles = nullptr;
	VirtualPath settingsPath;
	String settingsKey;
	String settingsDiagnostic;
	String contentError;
	ResourceId sourceGuid;
	ResourceId cameraIdentity = ResourceId::Generate();
	UInt64 subAssetId = 0;
	bool expanded = true;
	Float32 height = 280;
	Float32 resizeStartHeight = 0;
	Float32 resizeStartMouseY = 0;
	bool resizeDirty = false;
	bool visible = false;
	bool loadPending = false;
	bool resetPending = true;
	bool source = false;
	bool fit = true;
	TexturePreviewChannel channel = TexturePreviewChannel::Rgba;
	UInt32 mip = 0;
	ViewportSize textureSize;
	ViewportSize targetSize;
	ViewportRect imageRect;
	ViewportRect timelineRect;
	ViewportRect dividerRect;
	ViewportPoint pan;
	MaterialPreviewShape shape = MaterialPreviewShape::Sphere;
	Vec3 center;
	Float32 radius = 1;
	Float32 yaw = 0.4f;
	Float32 pitch = 0.2f;
	Float32 distance = 3;
	UInt64 renderCount = 0;
};
} // namespace Ursa::Editor
