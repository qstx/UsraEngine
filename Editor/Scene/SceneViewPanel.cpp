#include <Ui/EditorIcons.hpp>
#include <Scene/SceneViewPanel.hpp>

#include <Scene/EditorScenePreview.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Rendering/Commands/RenderCommandExecutor.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Math/Vec4.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace Ursa::Editor
{
namespace
{
ImVec2 CurrentViewportFramebufferScale()
{
	const ImVec2 viewportScale = ImGui::GetWindowViewport()->FramebufferScale;
	const ImVec2 fallback = ImGui::GetIO().DisplayFramebufferScale;
	return ImVec2(viewportScale.x > 0.f ? viewportScale.x : std::max(fallback.x, 1.f),
	              viewportScale.y > 0.f ? viewportScale.y : std::max(fallback.y, 1.f));
}

Vector<UInt8> ToBytes(const String& text)
{
	Vector<UInt8> bytes;
	bytes.reserve(text.size());
	for (unsigned char value : text)
	{
		bytes.push_back(value);
	}
	return bytes;
}

bool HasEffectiveGameCamera(const Scene& scene)
{
	const ComponentId cameraId = scene.ActiveCamera();
	if (!cameraId || !dynamic_cast<const CameraComponent*>(scene.GetComponent(cameraId)))
	{
		return false;
	}
	auto active = scene.IsComponentActive(cameraId);
	return active && active.value;
}

Vec4 Transform(const Mat4& matrix, Vec4 vector)
{
	const Float32 input[] = {vector.x, vector.y, vector.z, vector.w};
	Float32 output[4]{};
	for (Int32 row = 0; row < 4; ++row)
	{
		for (Int32 column = 0; column < 4; ++column)
		{
			output[row] += matrix.At(row, column) * input[column];
		}
	}
	return {output[0], output[1], output[2], output[3]};
}

bool IntersectBounds(Vec3 origin, Vec3 direction, const Bounds& bounds, Float32& distance)
{
	if (bounds.empty)
	{
		return false;
	}
	Float32 minimum = 0.f;
	Float32 maximum = std::numeric_limits<Float32>::max();
	const Float32 origins[] = {origin.x, origin.y, origin.z};
	const Float32 directions[] = {direction.x, direction.y, direction.z};
	const Float32 lower[] = {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z};
	const Float32 upper[] = {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};
	for (Size axis = 0; axis < 3; ++axis)
	{
		if (std::abs(directions[axis]) < 1.0e-7f)
		{
			if (origins[axis] < lower[axis] || origins[axis] > upper[axis])
			{
				return false;
			}
			continue;
		}
		Float32 first = (lower[axis] - origins[axis]) / directions[axis];
		Float32 second = (upper[axis] - origins[axis]) / directions[axis];
		if (first > second)
		{
			std::swap(first, second);
		}
		minimum = std::max(minimum, first);
		maximum = std::min(maximum, second);
		if (minimum > maximum)
		{
			return false;
		}
	}
	distance = minimum;
	return maximum >= 0.f && std::isfinite(distance);
}

Vec3 Add(Vec3 left, Vec3 right)
{
	return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 Scale(Vec3 value, Float32 scalar)
{
	return {value.x * scalar, value.y * scalar, value.z * scalar};
}
} // namespace

Result<SceneDropPoint> ResolveSceneDropPoint(const Camera& camera, Float32 aspect, const ViewportLayout& layout,
                                             ViewportPoint screenPosition,
                                             const Vector<Rendering::RenderObject>& renderObjects)
{
	const auto pixel = ScreenToViewportPixel(screenPosition, layout);
	if (!pixel || !layout.targetPixels.width || !layout.targetPixels.height || !std::isfinite(aspect) || aspect <= 0.f)
	{
		return Result<SceneDropPoint>::Failure("The pointer is outside the Scene image viewport");
	}
	try
	{
		const Float32 x = 2.f * pixel->x / static_cast<Float32>(layout.targetPixels.width) - 1.f;
		const Float32 y = 1.f - 2.f * pixel->y / static_cast<Float32>(layout.targetPixels.height);
		const Mat4 inverse = camera.ViewProjection(aspect).Inverse();
		const auto unproject = [&](Float32 depth) -> Result<Vec3>
		{
			const Vec4 homogeneous = Transform(inverse, {x, y, depth, 1.f});
			if (!std::isfinite(homogeneous.w) || std::abs(homogeneous.w) < 1.0e-7f)
			{
				return Result<Vec3>::Failure("The Scene camera ray has an invalid homogeneous coordinate");
			}
			return Result<Vec3>::Success(
			    {homogeneous.x / homogeneous.w, homogeneous.y / homogeneous.w, homogeneous.z / homogeneous.w});
		};
		auto nearPoint = unproject(0.f);
		auto farPoint = unproject(1.f);
		if (!nearPoint || !farPoint)
		{
			return Result<SceneDropPoint>::Failure(!nearPoint ? nearPoint.error : farPoint.error);
		}
		const Vec3 direction = Normalized(farPoint.value - nearPoint.value);
		if (Dot(direction, direction) < 0.99f)
		{
			return Result<SceneDropPoint>::Failure("The Scene camera ray is degenerate");
		}
		Float32 nearest = std::numeric_limits<Float32>::max();
		bool hitGeometry = false;
		for (const auto& object : renderObjects)
		{
			Float32 distance = 0.f;
			if (IntersectBounds(nearPoint.value, direction, object.worldBounds, distance) && distance < nearest)
			{
				nearest = distance;
				hitGeometry = true;
			}
		}
		if (hitGeometry)
		{
			return Result<SceneDropPoint>::Success({Add(nearPoint.value, Scale(direction, nearest)), true});
		}
		const Vec3 planeNormal = Normalized(camera.target - camera.position);
		const Float32 denominator = Dot(direction, planeNormal);
		if (std::abs(denominator) < 1.0e-6f)
		{
			return Result<SceneDropPoint>::Failure("The Scene camera ray is parallel to the camera target plane");
		}
		const Float32 planeDistance = Dot(camera.target - nearPoint.value, planeNormal) / denominator;
		if (!std::isfinite(planeDistance) || planeDistance < 0.f)
		{
			return Result<SceneDropPoint>::Failure("The Scene camera target plane is behind the viewport ray");
		}
		return Result<SceneDropPoint>::Success({Add(nearPoint.value, Scale(direction, planeDistance)), false});
	}
	catch (const std::exception& error)
	{
		return Result<SceneDropPoint>::Failure(ToUrsaString(error.what()));
	}
}

Result<std::unique_ptr<SceneViewPanel>> SceneViewPanel::Create(Rhi::DevicePtr selectedDevice, ImGuiBridge& gui)
{
	if (!selectedDevice)
	{
		return Result<std::unique_ptr<SceneViewPanel>>::Failure("Scene view requires an RHI device");
	}
	auto panel = std::unique_ptr<SceneViewPanel>(new SceneViewPanel(std::move(selectedDevice), gui));
	auto initialized = panel->Initialize();
	if (!initialized)
	{
		return Result<std::unique_ptr<SceneViewPanel>>::Failure(initialized.error);
	}
	return Result<std::unique_ptr<SceneViewPanel>>::Success(std::move(panel));
}

SceneViewPanel::~SceneViewPanel()
{
	if (gui)
	{
		if (sceneTarget.imageId != ImTextureID_Invalid)
		{
			gui->UnregisterTexture(sceneTarget.imageId);
		}
		if (gameTarget.imageId != ImTextureID_Invalid)
		{
			gui->UnregisterTexture(gameTarget.imageId);
		}
	}
}

Result<void> SceneViewPanel::Initialize()
{
	auto selectedPreview = EditorScenePreview::Create(device);
	if (!selectedPreview)
	{
		return Result<void>::Failure(selectedPreview.error);
	}
	sceneTarget.preview = std::move(selectedPreview.value);
	auto gamePreview = EditorScenePreview::Create(device);
	if (!gamePreview)
	{
		return Result<void>::Failure(gamePreview.error);
	}
	gameTarget.preview = std::move(gamePreview.value);
	auto selectedSampler = device->CreateSampler({true, false});
	if (!selectedSampler)
	{
		return Result<void>::Failure(selectedSampler.error);
	}
	sampler = std::move(selectedSampler.value);
	return {};
}

Result<void> SceneViewPanel::EnsureTarget(ViewportTarget& target, ViewportSize size)
{
	if (!size.width || !size.height)
	{
		return Result<void>::Failure("Viewport target size must be positive");
	}
	if (target.color && target.size.width == size.width && target.size.height == size.height)
	{
		return {};
	}
	auto capabilities = device->Capabilities();
	auto validated = ResolveViewportSize(size, GameViewSizeMode::FixedResolution, size.width, size.height,
	                                     capabilities.maxTextureSize, 16'777'216);
	if (!validated)
	{
		return Result<void>::Failure(validated.error);
	}
	auto replacement = Rendering::CreateRenderTexture(
	    *device, {size.width, size.height, static_cast<UInt32>(Rhi::Format::RgbA8Unorm), 32});
	if (!replacement)
	{
		return Result<void>::Failure("Cannot create " + ToUrsaString(std::to_string(size.width)) + "x" +
		                             ToUrsaString(std::to_string(size.height)) +
		                             " viewport target: " + replacement.error);
	}
	// The GUI can present before the first successful pipeline callback (for example during reload).
	// Give a replacement target valid contents and a sampled layout before exposing its image ID.
	auto initializer = Rendering::RenderCommandExecutor::Create(device);
	if (!initializer)
	{
		return Result<void>::Failure(initializer.error);
	}
	auto initialized = initializer.value->Begin(replacement.value, Rhi::ResourceState::ShaderRead);
	if (initialized)
	{
		Rendering::RenderCommand clear;
		clear.opcode = Rendering::RenderCommandOpcode::ClearTarget;
		clear.clearFlags = 1;
		clear.color = {0.035f, 0.055f, 0.09f, 1};
		initialized = initializer.value->Execute({clear});
	}
	if (initialized)
	{
		initialized = initializer.value->Submit();
	}
	if (!initialized)
	{
		return initialized;
	}
	auto submitted = initializer.value->End();
	if (!submitted)
	{
		return Result<void>::Failure(submitted.error);
	}
	auto registered = gui->RegisterTexture(replacement.value->color, sampler);
	if (!registered)
	{
		return Result<void>::Failure("Cannot register viewport target: " + registered.error);
	}
	auto idle = device->WaitIdle();
	if (!idle)
	{
		gui->UnregisterTexture(registered.value);
		return Result<void>::Failure("Cannot replace viewport target safely: " + idle.error);
	}
	if (target.imageId != ImTextureID_Invalid)
	{
		gui->UnregisterTexture(target.imageId);
	}
	target.imageId = registered.value;
	target.output = std::move(replacement.value);
	target.color = target.output->color;
	target.size = size;
	return {};
}

void SceneViewPanel::SaveCustomPresets(VirtualFileSystem& userFiles, String& diagnostic)
{
	auto path = VirtualPath::Parse("user:Settings/GameViewPresets.json");
	if (!path)
	{
		diagnostic = path.error;
		return;
	}
	const String text = SerializeGameViewPresets(customPresets);
	auto written = userFiles.WriteAtomic(path.value, ToBytes(text));
	diagnostic = written ? String{} : written.error;
}

void SceneViewPanel::SaveProjectSettings(VirtualFileSystem& userFiles, const String& projectKey, String& diagnostic)
{
	auto path = VirtualPath::Parse("user:Settings/GameViewProjects/" + projectKey + ".json");
	if (!path)
	{
		diagnostic = path.error;
		return;
	}
	GameViewSettingsState settings;
	settings.mode = gameSizeMode;
	settings.width = gamePresetWidth;
	settings.height = gamePresetHeight;
	settings.scale = gameScale;
	settings.fit = gameFit;
	settings.pan = gamePan;
	settings.presetId = selectedCustomPreset;
	const String text = SerializeGameViewSettings(settings);
	auto written = userFiles.WriteAtomic(path.value, ToBytes(text));
	diagnostic = written ? String{} : written.error;
}

void SceneViewPanel::LoadSettings(VirtualFileSystem& userFiles, const String& projectKey, String& diagnostic)
{
	if (!globalSettingsLoaded)
	{
		globalSettingsLoaded = true;
		auto path = VirtualPath::Parse("user:Settings/GameViewPresets.json");
		if (path)
		{
			auto text = userFiles.ReadText(path.value);
			if (text)
			{
				auto presets =
				    DeserializeGameViewPresets(text.value, device->Capabilities().maxTextureSize, 16'777'216);
				if (presets)
				{
					customPresets = std::move(presets.value);
				}
				else
				{
					diagnostic = presets.error;
					customPresets.clear();
				}
			}
		}
	}
	if (projectSettingsLoaded && loadedProjectKey == projectKey)
	{
		return;
	}
	projectSettingsLoaded = true;
	loadedProjectKey = projectKey;
	gameSizeMode = GameViewSizeMode::FreeAspect;
	gamePresetWidth = 16;
	gamePresetHeight = 9;
	gameScale = 1;
	gameFit = true;
	gamePan = {};
	selectedCustomPreset.clear();
	auto path = VirtualPath::Parse("user:Settings/GameViewProjects/" + projectKey + ".json");
	if (!path)
	{
		diagnostic = path.error;
		return;
	}
	auto text = userFiles.ReadText(path.value);
	if (!text)
	{
		return;
	}
	auto settings = DeserializeGameViewSettings(text.value);
	if (!settings)
	{
		gameSizeMode = GameViewSizeMode::FreeAspect;
		gamePresetWidth = 16;
		gamePresetHeight = 9;
		gameScale = 1;
		gameFit = true;
		gamePan = {};
		selectedCustomPreset.clear();
		diagnostic = settings.error;
	}
	else
	{
		const auto capabilities = device->Capabilities();
		const auto& saved = settings.value;
		if (saved.width > capabilities.maxTextureSize || saved.height > capabilities.maxTextureSize ||
		    (saved.mode == GameViewSizeMode::FixedResolution &&
		     static_cast<UInt64>(saved.width) * saved.height > 16'777'216))
		{
			gameSizeMode = GameViewSizeMode::FreeAspect;
			gamePresetWidth = 16;
			gamePresetHeight = 9;
			gameScale = 1;
			gameFit = true;
			gamePan = {};
			selectedCustomPreset.clear();
			diagnostic = "Saved Game resolution exceeds supported limits; restored defaults.";
		}
		else
		{
			gameSizeMode = saved.mode;
			gamePresetWidth = saved.width;
			gamePresetHeight = saved.height;
			gameScale = saved.scale;
			gameFit = saved.fit;
			gamePan = saved.pan;
			selectedCustomPreset = saved.presetId;
		}
		if (!selectedCustomPreset.empty() &&
		    std::none_of(customPresets.begin(), customPresets.end(),
		                 [this](const GameViewPreset& preset) { return preset.id == selectedCustomPreset; }))
		{
			selectedCustomPreset.clear();
			gameSizeMode = GameViewSizeMode::FreeAspect;
			gameFit = true;
			gamePan = {};
			diagnostic = "Saved Game preset no longer exists; restored Free Aspect.";
		}
	}
}

void SceneViewPanel::DrawGameToolbar(VirtualFileSystem* userFiles, const String& projectKey, String& diagnostic)
{
	if (userFiles && userFiles->IsMounted("user"))
	{
		LoadSettings(*userFiles, projectKey, diagnostic);
	}
	settingsChanged = false;
	presetsChanged = false;
	bool requestOpenCustomPreset = false;
	const String fixedLabel =
	    ToUrsaString(std::to_string(gamePresetWidth)) + "x" + ToUrsaString(std::to_string(gamePresetHeight));
	const auto selectedCustom =
	    std::find_if(customPresets.begin(), customPresets.end(),
	                 [this](const GameViewPreset& preset) { return preset.id == selectedCustomPreset; });
	const char* selectedLabel = selectedCustom != customPresets.end()          ? selectedCustom->name.c_str()
	                            : gameSizeMode == GameViewSizeMode::FreeAspect ? "Free Aspect"
	                            : gameSizeMode == GameViewSizeMode::AspectRatio
	                                ? (gamePresetWidth == 16 && gamePresetHeight == 9    ? "16:9"
	                                   : gamePresetWidth == 16 && gamePresetHeight == 10 ? "16:10"
	                                   : gamePresetWidth == 4 && gamePresetHeight == 3   ? "4:3"
	                                                                                     : "9:16")
	                                : fixedLabel.c_str();
	const Float32 resolutionLabelWidth = ImGui::CalcTextSize("Resolution").x + ImGui::GetStyle().ItemInnerSpacing.x;
	ImGui::SetNextItemWidth(std::max(1.f, std::min(220.f, ImGui::GetContentRegionAvail().x - resolutionLabelWidth)));
	if (ImGui::BeginCombo("Resolution", selectedLabel))
	{
		if (ImGui::Selectable("Free Aspect", gameSizeMode == GameViewSizeMode::FreeAspect))
		{
			gameSizeMode = GameViewSizeMode::FreeAspect;
			selectedCustomPreset.clear();
			gameFit = true;
			gamePan = {};
			settingsChanged = true;
		}
		const char* ratios[] = {"16:9", "16:10", "4:3", "9:16"};
		const UInt32 widths[] = {16, 16, 4, 9};
		const UInt32 heights[] = {9, 10, 3, 16};
		for (int index = 0; index < 4; ++index)
		{
			if (ImGui::Selectable(ratios[index], gameSizeMode == GameViewSizeMode::AspectRatio &&
			                                         gamePresetWidth == widths[index] &&
			                                         gamePresetHeight == heights[index]))
			{
				gameSizeMode = GameViewSizeMode::AspectRatio;
				gamePresetWidth = widths[index];
				gamePresetHeight = heights[index];
				selectedCustomPreset.clear();
				gameFit = true;
				gamePan = {};
				settingsChanged = true;
			}
		}
		const UInt32 resolutions[][2] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {1080, 1920}};
		for (const auto& resolution : resolutions)
		{
			const String label =
			    ToUrsaString(std::to_string(resolution[0])) + "x" + ToUrsaString(std::to_string(resolution[1]));
			if (ImGui::Selectable(label.c_str(), gameSizeMode == GameViewSizeMode::FixedResolution &&
			                                         gamePresetWidth == resolution[0] &&
			                                         gamePresetHeight == resolution[1]))
			{
				gameSizeMode = GameViewSizeMode::FixedResolution;
				gamePresetWidth = resolution[0];
				gamePresetHeight = resolution[1];
				selectedCustomPreset.clear();
				gameFit = true;
				gamePan = {};
				settingsChanged = true;
			}
		}
		if (!customPresets.empty())
		{
			ImGui::Separator();
		}
		for (const GameViewPreset& preset : customPresets)
		{
			const String label = preset.name + "##" + preset.id;
			if (ImGui::Selectable(label.c_str(), selectedCustomPreset == preset.id))
			{
				selectedCustomPreset = preset.id;
				gameSizeMode = preset.mode;
				gamePresetWidth = preset.width;
				gamePresetHeight = preset.height;
				gameFit = true;
				gamePan = {};
				settingsChanged = true;
			}
		}
		ImGui::Separator();
		if (Icons::MenuItem("Add Custom Resolution..."))
		{
			customPresetEditingId.clear();
			customPresetName.fill(0);
			customPresetWidth = 1280;
			customPresetHeight = 720;
			customPresetMode = 0;
			editCustomPreset = true;
			requestOpenCustomPreset = true;
		}
		ImGui::EndCombo();
	}
	if (!selectedCustomPreset.empty())
	{
		Icons::SameLine();
		if (Icons::SmallButton("Edit"))
		{
			auto found = std::find_if(customPresets.begin(), customPresets.end(), [this](const GameViewPreset& preset)
			                          { return preset.id == selectedCustomPreset; });
			if (found != customPresets.end())
			{
				customPresetEditingId = found->id;
				std::memcpy(customPresetName.data(), found->name.data(),
				            std::min(found->name.size(), customPresetName.size() - 1));
				customPresetWidth = static_cast<Int32>(found->width);
				customPresetHeight = static_cast<Int32>(found->height);
				customPresetMode = found->mode == GameViewSizeMode::AspectRatio ? 1 : 0;
				editCustomPreset = true;
				requestOpenCustomPreset = true;
			}
		}
		Icons::SameLine();
		if (Icons::SmallButton("Delete"))
		{
			customPresets.erase(std::remove_if(customPresets.begin(), customPresets.end(),
			                                   [this](const GameViewPreset& preset)
			                                   { return preset.id == selectedCustomPreset; }),
			                    customPresets.end());
			selectedCustomPreset.clear();
			gameSizeMode = GameViewSizeMode::FreeAspect;
			gameFit = true;
			gamePan = {};
			presetsChanged = true;
			settingsChanged = true;
		}
	}
	if (requestOpenCustomPreset)
	{
		ImGui::OpenPopup("Custom Game Resolution");
	}
	if (ImGui::BeginPopupModal("Custom Game Resolution", &editCustomPreset, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::InputText("Name", customPresetName.data(), customPresetName.size());
		const char* modes[] = {"Fixed Resolution", "Aspect Ratio"};
		ImGui::Combo("Type", &customPresetMode, modes, 2);
		ImGui::InputInt("Width", &customPresetWidth);
		ImGui::InputInt("Height", &customPresetHeight);
		if (Icons::Button("Save"))
		{
			const auto capabilities = device->Capabilities();
			const auto valid = ResolveViewportSize(
			    {static_cast<UInt32>(std::max(1, customPresetWidth)),
			     static_cast<UInt32>(std::max(1, customPresetHeight))},
			    GameViewSizeMode::FixedResolution, static_cast<UInt32>(std::max(0, customPresetWidth)),
			    static_cast<UInt32>(std::max(0, customPresetHeight)), capabilities.maxTextureSize, 16'777'216);
			if (!customPresetName[0] || customPresetWidth <= 0 || customPresetHeight <= 0 || !valid)
			{
				diagnostic = "Custom resolution needs a name and positive dimensions within the device limits.";
			}
			else
			{
				GameViewPreset preset;
				if (customPresetEditingId.empty())
				{
					UInt32 sequence = 1;
					while (std::find_if(customPresets.begin(), customPresets.end(),
					                    [sequence](const GameViewPreset& item)
					                    { return item.id == ToUrsaString("custom-" + std::to_string(sequence)); }) !=
					       customPresets.end())
					{
						++sequence;
					}
					preset.id = ToUrsaString("custom-" + std::to_string(sequence));
				}
				else
				{
					preset.id = customPresetEditingId;
				}
				preset.name = ToUrsaString(customPresetName.data());
				preset.mode = customPresetMode == 1 ? GameViewSizeMode::AspectRatio : GameViewSizeMode::FixedResolution;
				preset.width = static_cast<UInt32>(customPresetWidth);
				preset.height = static_cast<UInt32>(customPresetHeight);
				if (customPresetEditingId.empty())
				{
					customPresets.push_back(preset);
				}
				else
				{
					auto found =
					    std::find_if(customPresets.begin(), customPresets.end(),
					                 [this](const GameViewPreset& item) { return item.id == customPresetEditingId; });
					if (found != customPresets.end())
					{
						*found = preset;
					}
				}
				selectedCustomPreset = preset.id;
				gameSizeMode = preset.mode;
				gamePresetWidth = preset.width;
				gamePresetHeight = preset.height;
				gameFit = true;
				gamePan = {};
				presetsChanged = true;
				settingsChanged = true;
				editCustomPreset = false;
				ImGui::CloseCurrentPopup();
			}
		}
		Icons::SameLine();
		if (Icons::Button("Cancel"))
		{
			editCustomPreset = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	const Float32 scaleLabelWidth = ImGui::CalcTextSize("Scale").x + ImGui::GetStyle().ItemInnerSpacing.x;
	Icons::SameLine(150.f + scaleLabelWidth);
	ImGui::SetNextItemWidth(std::max(1.f, std::min(150.f, ImGui::GetContentRegionAvail().x - scaleLabelWidth)));
	Float32 sliderValue = std::log2(std::max(gameScale, 0.1f));
	if (ImGui::SliderFloat("Scale", &sliderValue, -3.321928f, 2.32f, "%.2f"))
	{
		gameScale = SnapViewportScale(std::exp2(sliderValue));
		gameFit = false;
		settingsChanged = true;
	}
	Icons::SameLine(50.f);
	if (gameFit)
	{
		ImGui::TextUnformatted("Fit");
	}
	else
	{
		ImGui::Text("%.2fx", gameScale);
	}
	Icons::SameLine();
	if (Icons::Button("Fit", Icons::Icon::Fit, "Fit game view"))
	{
		gameFit = true;
		gamePan = {};
		settingsChanged = true;
	}
}

Result<void> SceneViewPanel::Draw(SceneDraft& draft, EditorDocument& document, EditorHierarchy& hierarchy,
                                  PlaySession& play, VirtualFileSystem* userFiles, const String& projectKey,
                                  bool& showScene, bool& showGame, bool& viewportFocused, String& diagnostic,
                                  const std::function<Result<void>(const EditorDragSession&, Vec3)>& placeAssets,
                                  ImGuiViewport** focusedGameViewport)
{
	viewportFocused = false;
	if (focusedGameViewport)
	{
		*focusedGameViewport = nullptr;
	}
	sceneTarget.visible = false;
	gameTarget.visible = false;
	Scene& scene = play.Active() ? *play.GetScene() : draft.GetScene();
	if (showScene)
	{
		if (ImGui::Begin("Scene", &showScene))
		{
			const ImVec2 dpi = CurrentViewportFramebufferScale();
			if (ImGui::BeginPopupContextWindow("ScenePanelActions", ImGuiPopupFlags_NoOpenOverItems))
			{
				if (ImGui::MenuItem("Close Panel"))
				{
					showScene = false;
				}
				ImGui::EndPopup();
			}
			if (Icons::Button("Translate", Icons::Icon::Move, "Translate", gizmoOperation == GizmoOperation::Translate))
			{
				gizmoOperation = GizmoOperation::Translate;
			}
			Icons::SameLine();
			if (Icons::Button("Rotate", Icons::Icon::Rotate, "Rotate", gizmoOperation == GizmoOperation::Rotate))
			{
				gizmoOperation = GizmoOperation::Rotate;
			}
			Icons::SameLine();
			if (Icons::Button("Scale", Icons::Icon::Scale, "Scale", gizmoOperation == GizmoOperation::Scale))
			{
				gizmoOperation = GizmoOperation::Scale;
			}
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const ImVec2 position = ImGui::GetCursorScreenPos();
			const UInt32 width =
			    static_cast<UInt32>(std::max(0.f, static_cast<Float32>(std::floor(available.x * dpi.x))));
			const UInt32 height =
			    static_cast<UInt32>(std::max(0.f, static_cast<Float32>(std::floor(available.y * dpi.y))));
			if (width && height)
			{
				auto target = EnsureTarget(sceneTarget, {width, height});
				if (!target)
				{
					diagnostic = target.error;
				}
			}
			if (sceneTarget.color && width && height)
			{
				sceneTarget.visible = true;
				const ViewportRect content{position.x, position.y, available.x, available.y};
				const auto layout = ComputeViewportLayout(content, dpi.x, sceneTarget.size, 1, {}, true);
				sceneLayout = layout;
				const Float32 clipX = std::max(content.x, layout.displayRect.x);
				const Float32 clipY = std::max(content.y, layout.displayRect.y);
				ImGui::PushClipRect({clipX, clipY}, {clipX + layout.clipRect.width, clipY + layout.clipRect.height},
				                    true);
				ImGui::SetCursorScreenPos({layout.displayRect.x, layout.displayRect.y});
				ImGui::Image(sceneTarget.imageId, {layout.displayRect.width, layout.displayRect.height});
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					        editorDragSessionPayloadType,
					        ImGuiDragDropFlags_AcceptNoDrawDefaultRect | ImGuiDragDropFlags_AcceptBeforeDelivery))
					{
						auto drag = DecodeEditorDragPayload(payload->DataType, payload->Data,
						                                    static_cast<Size>(payload->DataSize));
						if (!drag)
						{
							if (payload->IsDelivery())
							{
								diagnostic = drag.error;
							}
						}
						else if (!play.Active() && draft.Kind() == ResourceKind::Scene && placeAssets)
						{
							const ImVec2 mouse = ImGui::GetIO().MousePos;
							const Float32 aspect = static_cast<Float32>(sceneTarget.size.width) /
							                       static_cast<Float32>(sceneTarget.size.height);
							auto renderData = scene.CollectPreviewRenderData(sceneTarget.preview->GetCamera(), aspect);
							auto dropPoint =
							    renderData ? ResolveSceneDropPoint(sceneTarget.preview->GetCamera(), aspect, layout,
							                                       {mouse.x, mouse.y}, renderData.value.objects)
							               : Result<SceneDropPoint>::Failure(renderData.error);
							if (dropPoint)
							{
								const Vec4 clip = Transform(
								    sceneTarget.preview->GetCamera().ViewProjection(aspect),
								    {dropPoint.value.world.x, dropPoint.value.world.y, dropPoint.value.world.z, 1.f});
								if (std::isfinite(clip.w) && std::abs(clip.w) > 1.0e-7f)
								{
									const ImVec2 previewPoint{
									    layout.displayRect.x +
									        (clip.x / clip.w + 1.f) * layout.displayRect.width * 0.5f,
									    layout.displayRect.y +
									        (1.f - clip.y / clip.w) * layout.displayRect.height * 0.5f};
									auto* drawList = ImGui::GetWindowDrawList();
									drawList->AddCircleFilled(previewPoint, 6.f,
									                          ImGui::GetColorU32(ImGuiCol_DragDropTarget), 16);
									drawList->AddCircle(previewPoint, 9.f, ImGui::GetColorU32(ImGuiCol_Text), 16, 1.5f);
								}
								if (ClaimEditorDragDropDelivery(payload))
								{
									auto placed = placeAssets(drag.value, dropPoint.value.world);
									diagnostic = placed ? String{} : placed.error;
								}
							}
							else if (payload->IsDelivery())
							{
								diagnostic = dropPoint.error;
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
				if (ImGui::BeginPopupContextItem("SceneViewportActions"))
				{
					const bool canCreate = !play.Active() && draft.Kind() == ResourceKind::Scene;
					if (ImGui::MenuItem("Create Empty", nullptr, false, canCreate))
					{
						GameObject::Ptr parent;
						auto selected = hierarchy.SelectedGameObject(scene);
						if (selected)
						{
							parent = selected.value;
						}
						else if (!scene.RootGameObjects().empty())
						{
							parent = scene.RootGameObjects().front();
						}
						auto created = scene.CreateGameObject("GameObject", parent);
						if (created)
						{
							auto selectedCreated = hierarchy.Select(scene, created.value);
							document.MarkDirty();
							diagnostic = selectedCreated ? String{} : selectedCreated.error;
						}
						else
						{
							diagnostic = created.error;
						}
					}
					ImGui::EndPopup();
				}
				ImGui::PopClipRect();
				GameObject::Ptr gizmoTarget;
				ResourceId prefabObjectId;
				if (!play.Active() && draft.Kind() == ResourceKind::Prefab && draft.GetPrefabAsset())
				{
					auto selectedAssetObject = hierarchy.SelectedGameObject(*draft.GetPrefabAsset());
					if (selectedAssetObject)
					{
						prefabObjectId = selectedAssetObject.value->Id();
						auto selectedPreviewObject = draft.PreviewGameObject(prefabObjectId);
						if (selectedPreviewObject)
						{
							gizmoTarget = selectedPreviewObject.value;
						}
					}
				}
				else
				{
					auto selected = hierarchy.SelectedGameObject(scene);
					if (selected)
					{
						gizmoTarget = selected.value;
					}
				}
				if (gizmoTarget && layout.visible)
				{
					GizmoInteractionReport interaction;
					auto manipulated = DrawGizmo(scene, gizmoTarget, sceneTarget.preview->GetCamera(),
					                             {layout.displayRect.x, layout.displayRect.y, layout.displayRect.width,
					                              layout.displayRect.height},
					                             gizmoOperation, &interaction);
					if (interaction.isOver != gizmoWasOver || interaction.isUsing != gizmoWasUsing ||
					    interaction.matrixChanged)
					{
						const ImVec2 mouse = ImGui::GetIO().MousePos;
						const std::string message = "Scene Gizmo object=" + ToStdString(gizmoTarget->Id().ToString()) +
						                            " rect=" + std::to_string(static_cast<int>(layout.displayRect.x)) +
						                            "," + std::to_string(static_cast<int>(layout.displayRect.y)) + "," +
						                            std::to_string(static_cast<int>(layout.displayRect.width)) + "," +
						                            std::to_string(static_cast<int>(layout.displayRect.height)) +
						                            " mouse=" + std::to_string(static_cast<int>(mouse.x)) + "," +
						                            std::to_string(static_cast<int>(mouse.y)) +
						                            " over=" + (interaction.isOver ? "true" : "false") +
						                            " using=" + (interaction.isUsing ? "true" : "false") +
						                            " matrixChanged=" + (interaction.matrixChanged ? "true" : "false");
						URSA_LOG_DEBUG("Editor", ToUrsaString(message));
					}
					gizmoWasOver = interaction.isOver;
					gizmoWasUsing = interaction.isUsing;
					if (manipulated && manipulated.value)
					{
						if (prefabObjectId)
						{
							auto synced = draft.SyncPrefabTransform(prefabObjectId);
							if (synced)
							{
								synced = draft.RefreshPrefabPreview();
							}
							if (!synced)
							{
								diagnostic = synced.error;
							}
						}
						if (!play.Active())
						{
							document.MarkDirty();
						}
					}
					else if (!manipulated)
					{
						diagnostic = manipulated.error;
					}
				}
			}
		}
		ImGui::End();
	}
	if (showGame)
	{
		if (ImGui::Begin("Game", &showGame))
		{
			const ImVec2 dpi = CurrentViewportFramebufferScale();
			if (ImGui::BeginPopupContextWindow("GamePanelActions", ImGuiPopupFlags_NoOpenOverItems))
			{
				if (ImGui::MenuItem("Close Panel"))
				{
					showGame = false;
				}
				ImGui::EndPopup();
			}
			DrawGameToolbar(userFiles, projectKey, diagnostic);
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const ImVec2 position = ImGui::GetCursorScreenPos();
			const UInt32 availableWidth =
			    static_cast<UInt32>(std::max(0.f, static_cast<Float32>(std::floor(available.x * dpi.x))));
			const UInt32 availableHeight =
			    static_cast<UInt32>(std::max(0.f, static_cast<Float32>(std::floor(available.y * dpi.y))));
			const auto size = ResolveViewportSize({availableWidth, availableHeight}, gameSizeMode, gamePresetWidth,
			                                      gamePresetHeight, device->Capabilities().maxTextureSize, 16'777'216);
			if ((!availableWidth || !availableHeight) || (!size && !gameTarget.color))
			{
				if (!availableWidth || !availableHeight)
				{
					ImGui::TextDisabled("Resize the Game view to render its camera.");
				}
				else
				{
					ImGui::TextWrapped("%s", size.error.c_str());
				}
			}
			else if (!HasEffectiveGameCamera(scene))
			{
				ImGui::TextDisabled("The current scene has no active game camera.");
			}
			else
			{
				if (size)
				{
					auto target = EnsureTarget(gameTarget, size.value);
					if (!target)
					{
						diagnostic = target.error;
					}
				}
				if (gameTarget.color)
				{
					gameTarget.visible = true;
					const ViewportRect content{position.x, position.y, available.x, available.y};
					const auto layout =
					    ComputeViewportLayout(content, dpi.x, gameTarget.size, gameScale, gamePan, gameFit);
					if (gameScale != layout.scale)
					{
						gameScale = layout.scale;
						if (!gameFit)
						{
							settingsChanged = true;
						}
					}
					const ViewportPoint clampedPan = ClampViewportPan(
					    gamePan,
					    {static_cast<UInt32>(std::ceil(layout.displayRect.width)),
					     static_cast<UInt32>(std::ceil(layout.displayRect.height))},
					    {static_cast<UInt32>(std::floor(available.x)), static_cast<UInt32>(std::floor(available.y))});
					if (clampedPan.x != gamePan.x || clampedPan.y != gamePan.y)
					{
						gamePan = clampedPan;
						settingsChanged = true;
					}
					const Float32 clipX = std::max(content.x, layout.displayRect.x);
					const Float32 clipY = std::max(content.y, layout.displayRect.y);
					ImGui::PushClipRect({clipX, clipY}, {clipX + layout.clipRect.width, clipY + layout.clipRect.height},
					                    true);
					ImGui::SetCursorScreenPos({layout.displayRect.x, layout.displayRect.y});
					ImGui::Image(gameTarget.imageId, {layout.displayRect.width, layout.displayRect.height});
					const bool imageHovered = ImGui::IsItemHovered();
					const bool panning = imageHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
					if (panning)
					{
						const ImVec2 delta = ImGui::GetIO().MouseDelta;
						gamePan.x += delta.x;
						gamePan.y += delta.y;
						gameFit = false;
						settingsChanged = true;
					}
					ImGui::PopClipRect();
					viewportFocused = play.Active() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
					                  imageHovered && !panning;
					if (viewportFocused && focusedGameViewport)
					{
						*focusedGameViewport = ImGui::GetWindowViewport();
					}
				}
			}
		}
		ImGui::End();
	}
	if (userFiles && userFiles->IsMounted("user"))
	{
		if (presetsChanged)
		{
			SaveCustomPresets(*userFiles, diagnostic);
		}
		if (settingsChanged)
		{
			SaveProjectSettings(*userFiles, projectKey, diagnostic);
		}
	}
	return {};
}

Result<void> SceneViewPanel::RenderPending(SceneDraft& draft, PlaySession& play,
                                           const EditorRenderViewport& renderViewport)
{
	Scene& scene = play.Active() ? *play.GetScene() : draft.GetScene();
	if (sceneTarget.visible)
	{
		if (!renderViewport || !sceneTarget.output)
		{
			return Result<void>::Failure("Scene viewport has no active render pipeline or target");
		}
		const Float32 aspect = Float32(sceneTarget.size.width) / Float32(sceneTarget.size.height);
		const Camera& editorCamera = sceneTarget.preview->GetCamera();
		Rendering::CameraSettings settings;
		settings.type = Rendering::CameraType::SceneView;
		settings.background = {0.035f, 0.055f, 0.09f, 1.0f};
		settings.targetWidth = sceneTarget.size.width;
		settings.targetHeight = sceneTarget.size.height;
		Rendering::CameraData camera{sceneViewCameraId, editorCamera.ViewProjection(aspect), editorCamera.position,
		                             settings};
		camera.view = editorCamera.View();
		camera.projection = editorCamera.Projection(aspect);
		camera.inverseViewProjection = camera.viewProjection.Inverse();
		camera.nearPlane = editorCamera.nearPlane;
		camera.farPlane = editorCamera.farPlane;
		camera.debugName = "Scene View";
		auto rendered = renderViewport(scene, sceneTarget.output, Rendering::CameraType::SceneView,
		                               std::optional<Rendering::CameraData>{std::move(camera)});
		if (!rendered)
		{
			return Result<void>::Failure("Scene viewport render failed: " + rendered.error);
		}
	}
	if (gameTarget.visible && HasEffectiveGameCamera(scene))
	{
		if (!renderViewport || !gameTarget.output)
		{
			return Result<void>::Failure("Game viewport has no active render pipeline or target");
		}
		auto rendered = renderViewport(scene, gameTarget.output, Rendering::CameraType::Game, {});
		if (!rendered)
		{
			return Result<void>::Failure("Game viewport render failed: " + rendered.error);
		}
	}
	return {};
}

void SceneViewPanel::DrawEmpty(bool& showScene, bool& showGame)
{
	if (showScene)
	{
		ImGui::Begin("Scene", &showScene);
		ImGui::TextDisabled("Open a scene to view and edit it.");
		ImGui::End();
	}
	if (showGame)
	{
		ImGui::Begin("Game", &showGame);
		ImGui::TextDisabled("Open a scene to preview its active game camera.");
		ImGui::End();
	}
}

Result<void> SceneViewPanel::ReloadResources()
{
	auto scenePreview = EditorScenePreview::Create(device);
	if (!scenePreview)
	{
		return Result<void>::Failure(scenePreview.error);
	}
	scenePreview.value->GetCamera() = sceneTarget.preview->GetCamera();
	auto gamePreview = EditorScenePreview::Create(device);
	if (!gamePreview)
	{
		return Result<void>::Failure(gamePreview.error);
	}
	gameTarget.preview = std::move(gamePreview.value);
	sceneTarget.preview = std::move(scenePreview.value);
	return {};
}
} // namespace Ursa::Editor
