#include <Assets/AssetPreviewPanel.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <Ui/EditorIcons.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Ursa::Editor
{
namespace
{
constexpr Float32 DividerHeight = 10;
}

AssetPreviewPanel::AssetPreviewPanel(Rhi::DevicePtr selectedDevice, ImGuiBridge& bridge)
    : device(std::move(selectedDevice)), gui(bridge)
{
}

AssetPreviewPanel::~AssetPreviewPanel()
{
	auto released = ReleaseImages();
	if (!released)
	{
		URSA_LOG_ERROR("Editor", "Preview shutdown after device failure: " + released.error);
	}
}

Result<void> AssetPreviewPanel::ReleaseImages()
{
	auto idle = device->WaitIdle();
	if (!idle)
	{
		return idle;
	}
	for (const auto id : retired)
	{
		gui.RetireTexture(id);
	}
	retired.clear();
	retiredContents.clear();
	if (textureId != ImTextureID_Invalid)
	{
		gui.RetireTexture(textureId);
	}
	textureId = ImTextureID_Invalid;
	texture.reset();
	sampler.reset();
	front.reset();
	back.reset();
	return {};
}

Result<void> AssetPreviewPanel::BeginFrame()
{
	visible = false;
	timelineRect = {};
	if (!retired.empty() || !retiredContents.empty())
	{
		auto idle = device->WaitIdle();
		if (!idle)
		{
			return idle;
		}
		for (const auto id : retired)
		{
			gui.UnregisterTexture(id);
		}
		retired.clear();
		retiredContents.clear();
	}
	return {};
}

Result<void> AssetPreviewPanel::Prepare(ProjectWorkspace& workspace, const ProjectBrowserModel& browser,
                                        const EditorSelectionContext& selection, const EditorDocument* document,
                                        const String& projectKey)
{
	settingsFiles = &workspace.Index().Filesystem();
	if (preparedWorkspace != &workspace || settingsKey != projectKey)
	{
		preparedWorkspace = &workspace;
		projectSession = ResourceId::Generate();
		settingsKey = projectKey;
		settingsPath = VirtualPath::Parse("user:Settings/AssetPreviews/" + projectKey + ".json").value;
		expanded = true;
		height = 280;
		settingsDiagnostic.clear();
		if (settingsFiles->IsMounted("user"))
		{
			auto exists = settingsFiles->Exists(settingsPath);
			if (!exists)
			{
				settingsDiagnostic = exists.error;
			}
			else if (exists.value)
			{
				auto text = settingsFiles->ReadText(settingsPath);
				try
				{
					if (!text)
					{
						throw std::runtime_error(text.error.c_str());
					}
					const auto data = nlohmann::json::parse(ToStdString(text.value));
					const auto savedHeight = data.at("height").get<Float32>();
					if (!std::isfinite(savedHeight))
					{
						throw std::runtime_error("Invalid Preview height");
					}
					const auto savedExpanded = data.at("expanded").get<bool>();
					height = std::clamp(savedHeight, 120.f, 1200.f);
					expanded = savedExpanded;
				}
				catch (const std::exception& error)
				{
					settingsDiagnostic = ToUrsaString(error.what());
				}
			}
		}
	}
	const auto* entry = selection.kind == EditorSelectionKind::Asset ? browser.Find(selection.assetPath) : nullptr;
	if (entry)
	{
		auto exists = workspace.Index().Filesystem().Exists(entry->path);
		if (exists && !exists.value)
		{
			entry = nullptr;
		}
	}
	if (!entry || sourceGuid != entry->guid)
	{
		sourceGuid = entry ? entry->guid : ResourceId{};
		subAssetId = 0;
	}
	UInt64 version = workspace.Database() ? workspace.Database()->Revision() : 0;
	version ^= (workspace.BuiltinDatabase() ? workspace.BuiltinDatabase()->Revision() : 0) << 32;
	auto selected =
	    ResolveAssetPreviewTarget(selection, entry, document, version, subAssetId,
	                              subAssetId ? std::optional<ResourceKind>(ResourceKind::Mesh) : std::nullopt);
	selected.projectSession = projectSession;
	selected.pipelineVersion = pipelineVersion;
	if (selected.draft)
	{
		selected.contentVersion ^= selected.draft->Revision() * 0x9e3779b97f4a7c15ULL;
		if (auto material = selected.draft->Payload<MaterialDraft>())
		{
			selected.contentVersion ^= material->MaterialPtr()->Version();
		}
	}
	const bool identityChanged = !state.Target().SameIdentity(selected);
	if (state.Select(std::move(selected)))
	{
		loadPending = Supported();
	}
	if (identityChanged)
	{
		auto released = ReleaseImages();
		if (!released)
		{
			return released;
		}
		candidate.reset();
		content.reset();
		clock = {};
		source = false;
		fit = true;
		pan = {};
		mip = 0;
		channel = TexturePreviewChannel::Rgba;
		shape = MaterialPreviewShape::Sphere;
		resetPending = true;
		contentError.clear();
		cameraIdentity = ResourceId::Generate();
	}
	return {};
}

Float32 AssetPreviewPanel::ReservedHeight() const
{
	return expanded ? height : ImGui::GetFrameHeightWithSpacing() + DividerHeight + ImGui::GetStyle().ItemSpacing.y;
}

void AssetPreviewPanel::Expand()
{
	if (!expanded)
	{
		expanded = true;
		SaveSettings();
	}
}

void AssetPreviewPanel::SaveSettings()
{
	if (!settingsFiles || !settingsFiles->IsMounted("user"))
	{
		return;
	}
	const auto text = nlohmann::json{{"expanded", expanded}, {"height", height}}.dump(2);
	const auto* begin = reinterpret_cast<const UInt8*>(text.data());
	Vector<UInt8> bytes(begin, begin + text.size());
	auto saved = settingsFiles->WriteAtomic(settingsPath, bytes);
	settingsDiagnostic = saved ? String{} : saved.error;
}

void AssetPreviewPanel::Reload(bool pipelineChanged)
{
	if (pipelineChanged)
	{
		++pipelineVersion;
		auto selected = state.Target();
		selected.pipelineVersion = pipelineVersion;
		state.Select(std::move(selected));
	}
	state.Invalidate();
	loadPending = Supported();
}

Result<void> AssetPreviewPanel::LoadContent(ProjectWorkspace& workspace, ComponentRegistry& registry,
                                            const AssetPreviewTextureInput* textureInput)
{
	loadPending = false;
	auto loaded = AssetPreviewContent::Load(state.Target(), workspace, registry, textureInput);
	if (!loaded)
	{
		return Result<void>::Failure(loaded.error);
	}
	candidate = std::move(loaded.value);
	const auto oldClip = content ? content->SelectedClip() : 0;
	const auto oldTime = clock.Time();
	clock = {};
	if (oldClip)
	{
		for (const auto& clip : candidate->Clips())
		{
			if (clip.localFileId == oldClip && std::isfinite(clip.duration) && clip.duration > 0)
			{
				auto selected = candidate->SelectClip(oldClip);
				if (!selected)
				{
					return selected;
				}
				(void)clock.SetDuration(clip.duration);
				(void)clock.Seek(std::min(oldTime, clip.duration));
				auto sampled = candidate->Sample(clock.Time());
				if (!sampled)
				{
					return sampled;
				}
				break;
			}
		}
	}
	return {};
}

void AssetPreviewPanel::ResetView()
{
	auto* active = candidate ? candidate.get() : content.get();
	center = {};
	radius = 0.8f;
	if (active && active->RenderScene())
	{
		auto bounds = active->GeometryBounds();
		if (bounds && !bounds.value.empty)
		{
			const auto& b = bounds.value;
			center = {(b.minimum.x + b.maximum.x) * .5f, (b.minimum.y + b.maximum.y) * .5f,
			          (b.minimum.z + b.maximum.z) * .5f};
			const auto extent = b.maximum - b.minimum;
			radius = std::max(
			    .01f, static_cast<Float32>(std::hypot(Float64(extent.x), Float64(extent.y), Float64(extent.z)) * .5));
		}
	}
	yaw = .4f;
	pitch = .2f;
	distance = radius * 3.3f;
	resetPending = false;
}

Camera AssetPreviewPanel::PreviewCamera() const
{
	Camera camera;
	camera.target = center;
	const Float32 aspect = targetSize.height ? Float32(targetSize.width) / targetSize.height : 1;
	const auto adjusted = distance / std::min(1.f, std::max(.05f, aspect));
	camera.position = {center.x + adjusted * std::sin(yaw) * std::cos(pitch), center.y + adjusted * std::sin(pitch),
	                   center.z + adjusted * std::cos(yaw) * std::cos(pitch)};
	camera.nearPlane = std::max(.0001f, radius * .001f);
	camera.farPlane = std::max(100.f, adjusted + radius * 10.f);
	return camera;
}

void AssetPreviewPanel::DrawControls(ProjectWorkspace& workspace)
{
	auto* active = candidate ? candidate.get() : content.get();
	if (state.Target().kind == AssetPreviewKind::Material)
	{
		int value = static_cast<int>(shape);
		ImGui::SetNextItemWidth(110);
		if (ImGui::Combo("##Shape", &value, "Sphere\0Cube\0Plane\0"))
		{
			shape = static_cast<MaterialPreviewShape>(value);
			state.Invalidate();
		}
		Icons::SameLine(110);
	}
	if (state.Target().kind == AssetPreviewKind::Model || subAssetId)
	{
		const String label = subAssetId ? "Mesh " + ToUrsaString(std::to_string(subAssetId)) : "Whole model";
		ImGui::SetNextItemWidth(-1);
		if (ImGui::BeginCombo("##Subasset", label.c_str()))
		{
			if (ImGui::Selectable("Whole model", !subAssetId))
			{
				subAssetId = 0;
			}
			for (const auto* database : {workspace.Database(), workspace.BuiltinDatabase()})
			{
				if (!database)
				{
					continue;
				}
				for (const auto& artifact : database->ImportedArtifacts())
				{
					if (artifact.guid != sourceGuid || artifact.kind != "Mesh" || !artifact.localFileId)
					{
						continue;
					}
					const auto name = "Mesh " + ToUrsaString(std::to_string(artifact.localFileId));
					if (ImGui::Selectable(name.c_str(), subAssetId == artifact.localFileId))
					{
						subAssetId = artifact.localFileId;
					}
				}
			}
			ImGui::EndCombo();
		}
	}
	if (state.Target().kind == AssetPreviewKind::Texture)
	{
		const bool hasSource = active && bool(active->SourceTexture());
		const bool hasImported = active && bool(active->Texture());
		ImGui::BeginDisabled(!hasSource);
		if (ImGui::RadioButton("Source", source))
		{
			source = true;
			mip = 0;
			pan = {};
			state.Invalidate();
		}
		ImGui::EndDisabled();
		Icons::SameLine(110);
		ImGui::BeginDisabled(!hasImported);
		if (ImGui::RadioButton("Imported", !source))
		{
			source = false;
			mip = 0;
			pan = {};
			state.Invalidate();
		}
		ImGui::EndDisabled();
		int value = static_cast<int>(channel);
		ImGui::SetNextItemWidth(100);
		if (ImGui::Combo("##Channel", &value, "RGBA\0RGB\0R\0G\0B\0Alpha\0"))
		{
			channel = static_cast<TexturePreviewChannel>(value);
			state.Invalidate();
		}
		Icons::SameLine(110);
		const auto selected = active ? (source ? active->SourceTexture() : active->Texture()) : nullptr;
		int level = static_cast<int>(mip);
		const int levels = selected ? static_cast<int>(selected->GetMipLevels()) : 1;
		ImGui::SetNextItemWidth(90);
		ImGui::BeginDisabled(levels < 2);
		if (ImGui::SliderInt("Mip", &level, 0, levels - 1))
		{
			mip = level;
			pan = {};
			state.Invalidate();
		}
		ImGui::EndDisabled();
		if (Icons::Button("Fit"))
		{
			fit = true;
			pan = {};
		}
		Icons::SameLine(110);
		if (ImGui::Button("100%"))
		{
			fit = false;
			pan = {};
		}
		if (!hasSource)
		{
			ImGui::TextDisabled("Source unavailable; viewing imported data.");
		}
	}
	else
	{
		if (Icons::Button("Reset view"))
		{
			ResetView();
			state.Invalidate();
		}
		if (active && active->MeshAsset())
		{
			const auto snapshot = active->MeshAsset()->Snapshot();
			ImGui::Text("%zu vertices | %zu triangles | %zu submeshes", snapshot->data.positions.size(),
			            snapshot->data.indices.size() / 3, snapshot->data.subMeshes.size());
		}
		if (active && !active->Clips().empty())
		{
			String label = "Bind pose";
			for (const auto& clip : active->Clips())
			{
				if (clip.localFileId == active->SelectedClip())
				{
					label = clip.name;
				}
			}
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##AnimationClip", label.c_str()))
			{
				if (ImGui::Selectable("Bind pose", !active->SelectedClip()))
				{
					auto cleared = active->SelectClip(0);
					if (!cleared)
					{
						state.Fail(state.Generation(), cleared.error);
					}
					clock = {};
					state.Invalidate();
				}
				for (const auto& clip : active->Clips())
				{
					const bool valid = std::isfinite(clip.duration) && clip.duration > 0;
					ImGui::BeginDisabled(!valid);
					ImGui::PushID(static_cast<int>(clip.localFileId));
					if (ImGui::Selectable(clip.name.c_str(), active->SelectedClip() == clip.localFileId))
					{
						auto assigned = active->SelectClip(clip.localFileId);
						if (assigned)
						{
							(void)clock.SetDuration(clip.duration);
							state.Invalidate();
						}
						else
						{
							state.Fail(state.Generation(), assigned.error);
						}
					}
					ImGui::PopID();
					ImGui::EndDisabled();
				}
				ImGui::EndCombo();
			}
			if (active->SelectedClip())
			{
				if (Icons::Button(clock.Playing() ? "Pause" : "Play"))
				{
					if (clock.Playing())
					{
						clock.Pause();
					}
					else
					{
						(void)clock.Play();
					}
				}
				Icons::SameLine(110);
				bool loop = clock.Loop();
				if (ImGui::Checkbox("Loop", &loop))
				{
					clock.SetLoop(loop);
				}
				Icons::SameLine(110);
				float speed = static_cast<float>(clock.Speed());
				ImGui::SetNextItemWidth(65);
				if (ImGui::DragFloat("Speed", &speed, .05f, .01f, 100.f, "%.2fx"))
				{
					(void)clock.SetSpeed(speed);
				}
				Float64 time = clock.Time();
				const Float64 beginning = 0, ending = clock.Duration();
				ImGui::SetNextItemWidth(-1);
				if (ImGui::SliderScalar("##AnimationTime", ImGuiDataType_Double, &time, &beginning, &ending, "%.3fs",
				                        ImGuiSliderFlags_NoRoundToFormat))
				{
					(void)clock.Seek(time);
					state.Invalidate();
				}
				const auto minimum = ImGui::GetItemRectMin();
				const auto maximum = ImGui::GetItemRectMax();
				timelineRect = {minimum.x, minimum.y, maximum.x - minimum.x, maximum.y - minimum.y};
			}
		}
	}
}

void AssetPreviewPanel::Draw(ProjectWorkspace& workspace, ComponentRegistry& registry,
                             const AssetPreviewTextureInput* textureInput, Float32 maximumHeight)
{
	ImGui::PushID("AssetPreview");
	const auto effectiveHeight = ImGui::GetContentRegionAvail().y;
	ImGui::InvisibleButton("Resize", ImVec2(-1, DividerHeight));
	const auto dividerMin = ImGui::GetItemRectMin(), dividerMax = ImGui::GetItemRectMax();
	dividerRect = {dividerMin.x, dividerMin.y, dividerMax.x - dividerMin.x, dividerMax.y - dividerMin.y};
	if (ImGui::IsItemActivated())
	{
		resizeStartHeight = effectiveHeight;
		resizeStartMouseY = ImGui::GetIO().MousePos.y;
		resizeDirty = false;
	}
	const bool dividerHovered = ImGui::IsItemHovered();
	const bool resizing = expanded && ImGui::IsItemActive();
	if (resizing && ImGui::GetIO().MouseDelta.y != 0)
	{
		const auto limit = std::clamp(maximumHeight, 120.f, 1200.f);
		const auto nextHeight =
		    std::clamp(resizeStartHeight + resizeStartMouseY - ImGui::GetIO().MousePos.y, 120.f, limit);
		resizeDirty = resizeDirty || nextHeight != height;
		height = nextHeight;
	}
	if (ImGui::IsItemDeactivated() && resizeDirty)
	{
		SaveSettings();
		resizeDirty = false;
	}
	if (expanded && (dividerHovered || resizing))
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	const auto color = ImGui::GetColorU32(resizing         ? ImGuiCol_SeparatorActive
	                                      : dividerHovered ? ImGuiCol_SeparatorHovered
	                                                       : ImGuiCol_Separator);
	const auto middleY = (dividerMin.y + dividerMax.y) * .5f;
	const auto middleX = (dividerMin.x + dividerMax.x) * .5f;
	auto* dividerDraw = ImGui::GetWindowDrawList();
	dividerDraw->AddLine({dividerMin.x, middleY}, {dividerMax.x, middleY}, color);
	const auto gripWidth = std::min(16.f, dividerRect.width * .25f);
	dividerDraw->AddLine({middleX - gripWidth, middleY}, {middleX + gripWidth, middleY}, color, 3.f);
	if (ImGui::Button(expanded ? "v Preview" : "> Preview", ImVec2(-1, 0)))
	{
		expanded = !expanded;
		SaveSettings();
	}
	if (!expanded)
	{
		ImGui::PopID();
		return;
	}
	if (!ImGui::BeginChild("Content", ImVec2(0, 0), ImGuiChildFlags_None))
	{
		ImGui::EndChild();
		ImGui::PopID();
		return;
	}
	if (ImGui::GetContentRegionAvail().x > 0 && ImGui::GetContentRegionAvail().y > ImGui::GetFrameHeightWithSpacing() &&
	    loadPending)
	{
		auto loaded = LoadContent(workspace, registry, textureInput);
		if (!loaded)
		{
			candidate.reset();
			contentError = loaded.error;
			state.Fail(state.Generation(), loaded.error);
		}
		else if (state.Target().kind == AssetPreviewKind::Texture && candidate->SourceTexture() &&
		         !candidate->Texture())
		{
			source = true;
		}
	}
	DrawControls(workspace);
	const auto* active = candidate ? candidate.get() : content.get();
	if (active && !active->Diagnostic().empty())
	{
		ImGui::TextWrapped("%s", active->Diagnostic().c_str());
		if (!active->ImportVersion().empty())
		{
			ImGui::TextWrapped("Imported version: %s", active->ImportVersion().c_str());
		}
	}
	if (!state.Diagnostic().empty())
	{
		if (state.HasImage())
		{
			const auto& displayed = state.DisplayedTarget();
			ImGui::TextWrapped("Last successful preview (content %llu, pipeline %llu)",
			                   static_cast<unsigned long long>(displayed.contentVersion),
			                   static_cast<unsigned long long>(displayed.pipelineVersion));
			if (!state.DisplayedImportVersion().empty())
			{
				ImGui::TextWrapped("Imported version: %s", state.DisplayedImportVersion().c_str());
			}
		}
		ImGui::TextWrapped("%s", state.Diagnostic().c_str());
		if (ImGui::SmallButton("Retry"))
		{
			Reload();
		}
	}
	if (!settingsDiagnostic.empty())
	{
		ImGui::TextWrapped("Preview settings: %s", settingsDiagnostic.c_str());
	}
	const auto pos = ImGui::GetCursorScreenPos();
	const auto area = ImGui::GetContentRegionAvail();
	imageRect = {pos.x, pos.y, area.x, area.y};
	visible = area.x >= 1 && area.y >= 1 && ImGui::IsRectVisible(area);
	if (visible)
	{
		const auto dpi = std::max(.01f, ImGui::GetIO().DisplayFramebufferScale.x);
		const ViewportSize nextSize = {static_cast<UInt32>(std::max(1.f, area.x * dpi)),
		                               static_cast<UInt32>(std::max(1.f, area.y * dpi))};
		if (nextSize.width != targetSize.width || nextSize.height != targetSize.height)
		{
			targetSize = nextSize;
			state.Invalidate();
		}
		ImGui::InvisibleButton("Image", area, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		if (hovered && state.Target().kind != AssetPreviewKind::Texture)
		{
			const auto& io = ImGui::GetIO();
			if (io.MouseWheel != 0)
			{
				distance = std::clamp(distance * std::exp(-io.MouseWheel * .12f), radius * .05f, radius * 10000.f);
				state.Invalidate();
			}
		}
		if (ImGui::IsItemActive())
		{
			const auto delta = ImGui::GetIO().MouseDelta;
			if (state.Target().kind == AssetPreviewKind::Texture && !fit)
			{
				pan.x += delta.x;
				pan.y += delta.y;
			}
			else if (state.Target().kind != AssetPreviewKind::Texture && (delta.x || delta.y))
			{
				yaw += delta.x * .01f;
				pitch = std::clamp(pitch + delta.y * .01f, -1.5f, 1.5f);
				state.Invalidate();
			}
		}
		auto* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(pos, ImVec2(pos.x + area.x, pos.y + area.y), true);
		draw->AddRectFilled(pos, ImVec2(pos.x + area.x, pos.y + area.y), IM_COL32(22, 25, 32, 255));
		if (state.Target().kind == AssetPreviewKind::Texture && state.HasImage() && textureId != ImTextureID_Invalid)
		{
			if (!fit)
			{
				pan = ClampViewportPan(
				    pan, {static_cast<UInt32>(textureSize.width / dpi), static_cast<UInt32>(textureSize.height / dpi)},
				    {static_cast<UInt32>(area.x), static_cast<UInt32>(area.y)});
			}
			const auto layout = ComputeViewportLayout(imageRect, dpi, textureSize, 1, pan, fit);
			const auto& r = layout.displayRect;
			for (float y = pos.y; y < pos.y + area.y; y += 12)
			{
				for (float x = pos.x; x < pos.x + area.x; x += 12)
				{
					const int shade =
					    (static_cast<int>((x - pos.x) / 12) + static_cast<int>((y - pos.y) / 12)) % 2 ? 70 : 48;
					draw->AddRectFilled(ImVec2(x, y),
					                    ImVec2(std::min(x + 12, pos.x + area.x), std::min(y + 12, pos.y + area.y)),
					                    IM_COL32(shade, shade, shade, 255));
				}
			}
			draw->AddImage(textureId, ImVec2(r.x, r.y), ImVec2(r.x + r.width, r.y + r.height));
		}
		else if (state.HasImage() && front)
		{
			const auto layout = ComputeViewportLayout(
			    imageRect, dpi, {front->ColorTexture()->desc.width, front->ColorTexture()->desc.height}, 1, {}, true);
			const auto& r = layout.displayRect;
			draw->AddImage(front->ImageId(), ImVec2(r.x, r.y), ImVec2(r.x + r.width, r.y + r.height));
		}
		draw->PopClipRect();
	}
	ImGui::EndChild();
	ImGui::PopID();
}

Result<void> AssetPreviewPanel::UploadTexture()
{
	auto* active = candidate ? candidate.get() : content.get();
	if (!active)
	{
		return Result<void>::Failure("Texture preview is unavailable");
	}
	auto selected = source ? active->SourceTexture() : active->Texture();
	auto display = CreateTexturePreviewView(selected, mip, channel);
	if (!display)
	{
		return Result<void>::Failure(display.error);
	}
	ImTextureID nextId = ImTextureID_Invalid;
	Rhi::TexturePtr nextTexture;
	Rhi::SamplerPtr nextSampler;
	auto uploaded = UploadTexturePreview(device, gui, display.value, nextId, nextTexture, nextSampler);
	if (!uploaded)
	{
		return uploaded;
	}
	if (textureId != ImTextureID_Invalid)
	{
		retired.push_back(textureId);
	}
	textureId = nextId;
	texture = std::move(nextTexture);
	sampler = std::move(nextSampler);
	textureSize = {display.value->GetWidth(), display.value->GetHeight()};
	return {};
}

Result<void> AssetPreviewPanel::RenderPending(Float64 deltaSeconds, const EditorRenderViewport& render)
{
	const bool failedContent = !contentError.empty() && !candidate;
	auto advanced = clock.Advance(deltaSeconds, visible && expanded && !loadPending && !failedContent);
	if (!advanced)
	{
		clock.Pause();
		state.Fail(state.Generation(), advanced.error);
		return {};
	}
	if (failedContent)
	{
		clock.Pause();
		state.Fail(state.Generation(), contentError);
		return {};
	}
	if (!visible || !expanded || !Supported() || loadPending)
	{
		return {};
	}
	if (advanced && advanced.value)
	{
		state.Invalidate();
	}
	if (!state.NeedsRender())
	{
		return {};
	}
	const auto renderingGeneration = state.Generation();
	auto* active = candidate ? candidate.get() : content.get();
	Result<void> result;
	if (!active)
	{
		result = Result<void>::Failure("Preview content is unavailable");
	}
	else if (state.Target().kind == AssetPreviewKind::Texture)
	{
		result = UploadTexture();
	}
	else
	{
		if (resetPending)
		{
			ResetView();
		}
		if (active->SelectedClip())
		{
			result = active->Sample(clock.Time());
		}
		if (result && !back)
		{
			auto created = MaterialPreview::Create(device, gui);
			if (created)
			{
				back = std::move(created.value);
			}
			else
			{
				result = Result<void>::Failure(created.error);
			}
		}
		if (result)
		{
			result = back->Resize(targetSize.width, targetSize.height);
		}
		if (result && active->Material())
		{
			result = back->SetShape(shape);
			if (result)
			{
				result = back->Render(active->Material(), PreviewCamera(), render, cameraIdentity);
			}
		}
		else if (result && active->RenderScene())
		{
			auto bounds = active->GeometryBounds();
			if (!bounds || bounds.value.empty)
			{
				result = Result<void>::Failure(bounds ? String("No enabled renderable geometry") : bounds.error);
			}
			else
			{
				result = back->RenderScene(*active->RenderScene(), PreviewCamera(), render, cameraIdentity);
			}
		}
		if (result && state.Generation() == renderingGeneration)
		{
			front.swap(back);
		}
	}
	if (state.Generation() != renderingGeneration)
	{
		return {};
	}
	if (result)
	{
		if (candidate)
		{
			if (content)
			{
				retiredContents.push_back(std::move(content));
			}
			content = std::move(candidate);
			contentError.clear();
		}
		state.Complete(renderingGeneration, content ? content->ImportVersion() : String{});
		if (!contentError.empty())
		{
			state.Fail(state.Generation(), contentError);
		}
		++renderCount;
	}
	else
	{
		if (candidate)
		{
			contentError = result.error;
		}
		candidate.reset();
		clock.Pause();
		state.Fail(renderingGeneration, result.error);
	}
	return {};
}
} // namespace Ursa::Editor
