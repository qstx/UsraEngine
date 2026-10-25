#include <Assets/AssetPreviewPanel.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/AssetImportRules.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Serialization/PrefabAsset.hpp>
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

Float32 CurrentViewportFramebufferScale()
{
	const Float32 viewportScale = ImGui::GetWindowViewport()->FramebufferScale.x;
	return std::max(.01f, viewportScale > 0.f ? viewportScale : ImGui::GetIO().DisplayFramebufferScale.x);
}
} // namespace

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
	if (extension || !retired.empty() || !retiredContents.empty())
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
	if (resetExtension)
	{
		extension.reset();
		extensionPreview = false;
		extensionDiagnostic.clear();
		extensionInfo.clear();
		extensionDrawPending = false;
		extensionWasVisible = false;
		resetExtension = false;
	}
	return {};
}

Result<void> AssetPreviewPanel::Prepare(ProjectWorkspace& workspace, ProjectBrowserModel& browser,
                                        EditorSelectionContext& selection, const EditorDocument* document,
                                        const String& projectKey)
{
	if (browser.NeedsRefresh(workspace))
	{
		auto refreshed = browser.Refresh(workspace);
		if (!refreshed)
		{
			return refreshed;
		}
	}
	browser.ReconcileSelection(selection);
	selectionContext = &selection;
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
	const auto* entry = selection.kind == EditorSelectionKind::Asset && !selection.assetMissing
	                        ? browser.Find(selection.assetPath)
	                        : nullptr;
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
		selectedObjectId = 0;
		subAssetKind.clear();
	}
	if (entry && selectedObjectId != selection.assetLocalFileId)
	{
		selectedObjectId = selection.assetLocalFileId;
		subAssetId = selectedObjectId;
		subAssetKind.clear();
	}
	UInt64 version = workspace.Database() ? workspace.Database()->Revision() : 0;
	version ^= (workspace.BuiltinDatabase() ? workspace.BuiltinDatabase()->Revision() : 0) << 32;
	std::optional<ResourceKind> selectedKind;
	String artifactKind;
	hasSubAssets = false;
	if (entry)
	{
		for (const auto* database : {workspace.Database(), workspace.BuiltinDatabase()})
		{
			if (!database)
			{
				continue;
			}
			for (const auto& artifact : database->ImportedArtifacts())
			{
				if (artifact.guid != entry->guid)
				{
					continue;
				}
				if (!artifact.isMainObject && artifact.visibleInProject)
				{
					hasSubAssets = true;
				}
				if ((subAssetId && artifact.localFileId == subAssetId) || (!subAssetId && artifact.isMainObject))
				{
					artifactKind = artifact.kind;
					if (subAssetId)
					{
						subAssetKind = artifact.kind;
					}
				}
			}
		}
	}
	const auto kind = subAssetId ? subAssetKind : entry && !entry->kind ? artifactKind : String{};
	if (kind == "Mesh")
	{
		selectedKind = ResourceKind::Mesh;
	}
	else if (kind == "Material")
	{
		selectedKind = ResourceKind::Material;
	}
	else if (kind == "Texture")
	{
		selectedKind = ResourceKind::Texture;
	}
	else if (kind == "Prefab")
	{
		selectedKind = ResourceKind::Prefab;
	}
	auto selected = ResolveAssetPreviewTarget(selection, entry, document, version, subAssetId, selectedKind);
	if (kind == "Model")
	{
		selected.kind = AssetPreviewKind::Model;
	}
	else if (kind == "AnimationClip")
	{
		selected.kind = AssetPreviewKind::Animation;
	}
	else if (subAssetId && !selectedKind)
	{
		selected.kind = AssetPreviewKind::None;
	}
	if (entry && subAssetId && artifactKind.empty())
	{
		selected.kind = AssetPreviewKind::None;
	}
	selected.projectSession = projectSession;
	selected.pipelineVersion = pipelineVersion;
	selected.custom = extension && state.Target().SameIdentity(selected) && extensionPreview;
	if (selected.draft)
	{
		selected.contentVersion ^= selected.draft->Revision() * 0x9e3779b97f4a7c15ULL;
		if (auto material = selected.draft->Payload<MaterialDraft>())
		{
			selected.contentVersion ^= material->MaterialPtr()->Version();
		}
	}
	const bool identityChanged = !state.Target().SameIdentity(selected);
	const bool contentChanged = state.Target().contentVersion != selected.contentVersion;
	if (state.Select(std::move(selected)))
	{
		loadPending = state.Target().Supported();
	}
	if (identityChanged)
	{
		extension.reset();
		extensionLoadContext.reset();
		extensionResources.reset();
		extensionResourceKey.clear();
		extensionPreview = false;
		extensionDiagnostic.clear();
		extensionInfo.clear();
		extensionTime = 0;
		extensionWasVisible = false;
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
	else if (contentChanged && extension)
	{
		const auto closed = CloseExtension();
		if (!closed)
		{
			return closed;
		}
	}
	if (entry && extensionResolver)
	{
		const auto descriptors = workspace.ImportRuleDescriptions();
		const auto importer =
		    descriptors->FindImporter(PathToUrsaString(PathFromUrsaString(entry->path.relative).extension()));
		const bool importerTarget = !subAssetId && importer &&
		                            (importer.value->scriptType || importer.value->identity == "ursa.Texture" ||
		                             importer.value->identity == "ursa.Model");
		if (!importerTarget)
		{
			const auto resourceKind = !artifactKind.empty()                   ? artifactKind
			                          : entry->kind == ResourceKind::Mesh     ? String("Mesh")
			                          : entry->kind == ResourceKind::Texture  ? String("Texture")
			                          : entry->kind == ResourceKind::Material ? String("Material")
			                          : entry->kind == ResourceKind::Prefab   ? String("Prefab")
			                                                                  : String("ScriptableObject");
			PrepareResourceExtension(workspace, resourceKind);
		}
	}
	return {};
}

void AssetPreviewPanel::PrepareResourceExtension(ProjectWorkspace& workspace, const String& kind)
{
	try
	{
		const auto& target = state.Target();
		if (!target.identity.guid)
		{
			return;
		}
		const auto key = target.identity.guid.ToString() + ":" +
		                 ToUrsaString(std::to_string(target.identity.localFileId)) + ":" +
		                 ToUrsaString(std::to_string(target.contentVersion));
		if (key != extensionResourceKey)
		{
			extensionLoadContext.reset();
			extensionResources = Scripting::Bindings::TakeBindingResult(
			    ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver()));
			extensionLoadContext = std::make_unique<ComponentLoadContext>(*extensionResources);
			extensionResourceKey = key;
		}
		ManagedEditorContext context;
		context.workspace = &workspace;
		context.targetUsesDocument = false;
		context.canEditTarget = false;
		context.assetSelection = target.identity;
		context.selection.loadContext = extensionLoadContext.get();
		nlohmann::json payload{{"Fields", nlohmann::json::object()}, {"FieldTypes", nlohmann::json::object()}};
		if (kind == "Mesh")
		{
			context.targetIdentity = "asset:ursa.Mesh";
			context.targetMesh = Scripting::Bindings::TakeBindingResult(
			    extensionResources->LoadMesh(target.identity.guid, target.identity.localFileId));
		}
		else if (kind == "Texture")
		{
			context.targetIdentity = "asset:ursa.Texture";
			context.targetTexture = Scripting::Bindings::TakeBindingResult(
			    extensionResources->LoadTexture(target.identity.guid, target.identity.localFileId));
		}
		else if (kind == "Material")
		{
			context.targetIdentity = "asset:ursa.Material";
			context.targetMaterial = Scripting::Bindings::TakeBindingResult(
			    extensionResources->LoadMaterial(target.identity.guid, target.identity.localFileId));
			payload =
			    Scripting::Bindings::TakeBindingResult(MaterialDraft::DescribeProperties(*context.targetMaterial));
		}
		else if (kind == "ScriptableObject" || kind == "ScriptableAsset")
		{
			if (!target.identity.localFileId && PathFromUrsaString(target.path.relative).extension() == ".ursaasset")
			{
				const auto asset = Scripting::Bindings::TakeBindingResult(
				    LoadScriptableAssetSource(workspace.Index().Filesystem(), target.path, target.identity.guid));
				context.assetSelection = AssetReference{asset.SourceGuid(), asset.LocalFileId()};
				context.targetIdentity = "asset:" + asset.TypeId();
				payload = asset.Payload();
			}
			else
			{
				const auto graph = Scripting::Bindings::TakeBindingResult(
				    LoadScriptableAssetGraph(*extensionResources, target.identity));
				const auto found = std::find_if(
				    graph.assets.begin(), graph.assets.end(), [&](const auto& item)
				    { return item.SourceGuid() == graph.root.guid && item.LocalFileId() == graph.root.localFileId; });
				if (found == graph.assets.end())
				{
					return;
				}
				context.assetSelection = graph.root;
				context.targetIdentity = "asset:" + found->TypeId();
				payload = found->Payload();
			}
		}
		else if (kind == "Prefab")
		{
			ComponentRegistry registry;
			Scripting::Bindings::CompleteBindingResult(RegisterBuiltinComponents(registry));
			const auto path = Scripting::Bindings::TakeBindingResult(
			    extensionResources->ResolveAsset(target.identity.guid, target.identity.localFileId));
			const auto asset = Scripting::Bindings::TakeBindingResult(LoadPrefabAssetResource(
			    extensionResources->Index().Filesystem(), path, registry, *extensionLoadContext));
			context.targetIdentity = "asset:ursa.Prefab";
			context.selection.object = asset.Root();
			context.selection.roots = {asset.Root()};
		}
		else
		{
			return;
		}
		SerializedTargetAdapter adapter;
		adapter.resources = extensionResources.get();
		adapter.capture = [payload] { return Result<nlohmann::json>::Success(payload); };
		context.serializedTarget = std::move(adapter);
		extensionResolver(context);
	}
	catch (const std::exception& error)
	{
		// Default content loading owns diagnostics when no matching custom extension is available.
		if (extensionPreview)
		{
			extensionDiagnostic = ToUrsaString(error.what());
		}
	}
}

void AssetPreviewPanel::ConfigureExtension(const String& id, AssetPreviewExtension::Callback callback,
                                           const ManagedEditorContext& context)
{
	if (!context.assetSelection || context.assetSelection->guid != state.Target().identity.guid ||
	    state.Target().identity.localFileId &&
	        context.assetSelection->localFileId != state.Target().identity.localFileId)
	{
		return;
	}
	if (!extension || extension->Id() != id)
	{
		extension = std::make_shared<AssetPreviewExtension>(id, std::move(callback));
		extensionDiagnostic.clear();
	}
	extension->Bind(context);
	if (!extensionDiagnostic.empty())
	{
		return;
	}
	const auto result = extension->Invoke(Scripting::ManagedPreviewPhase::Query);
	extensionPreview = !result || result.value.hasPreview;
	if (!result)
	{
		extensionDiagnostic = result.error;
	}
	else
	{
		extensionInfo = result.value.info;
		extensionConstantRepaint = result.value.constantRepaint;
	}
	auto selected = state.Target();
	selected.custom = extensionPreview;
	state.Select(std::move(selected));
	if (extensionPreview)
	{
		loadPending = false;
	}
	else if (!content && state.Target().Supported())
	{
		loadPending = true;
	}
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

Result<void> AssetPreviewPanel::CloseExtension()
{
	const auto idle = device->WaitIdle();
	if (!idle)
	{
		return idle;
	}
	extension.reset();
	extensionPreview = false;
	extensionDiagnostic.clear();
	extensionInfo.clear();
	extensionDrawPending = false;
	extensionWasVisible = false;
	return {};
}

std::optional<Result<bool>> AssetPreviewPanel::DrawExtensionInspector(const String& id, ManagedEditorContext& context)
{
	if (!extension || extension->Id() != id || !context.assetSelection ||
	    context.assetSelection->guid != state.Target().identity.guid ||
	    (state.Target().identity.localFileId &&
	     context.assetSelection->localFileId != state.Target().identity.localFileId))
	{
		return std::nullopt;
	}
	return extension->DrawInspector(context);
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
	resetExtension = true;
	if (pipelineChanged)
	{
		++pipelineVersion;
		auto selected = state.Target();
		selected.pipelineVersion = pipelineVersion;
		state.Select(std::move(selected));
	}
	state.Invalidate();
	loadPending = state.Target().Supported();
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
	const auto oldClip = state.Target().kind == AssetPreviewKind::Animation ? state.Target().identity.localFileId
	                     : content                                          ? content->SelectedClip()
	                                                                        : 0;
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

void AssetPreviewPanel::DrawSubAssets(ProjectWorkspace& workspace)
{
	if (hasSubAssets || subAssetId)
	{
		String label = subAssetId                                       ? subAssetKind
		               : state.Target().kind == AssetPreviewKind::Model ? String("Whole model")
		                                                                : String("Main object");
		for (const auto* database : {workspace.Database(), workspace.BuiltinDatabase()})
		{
			if (database && subAssetId)
			{
				const auto object = database->ImportedObject(sourceGuid, subAssetId);
				if (object)
				{
					label = object.value.Label();
				}
			}
		}
		ImGui::SetNextItemWidth(-1);
		if (ImGui::BeginCombo("##Subasset", label.c_str()))
		{
			if (ImGui::Selectable(state.Target().kind == AssetPreviewKind::Model ? "Whole model" : "Main object",
			                      !subAssetId))
			{
				if (selectionContext)
				{
					selectionContext->SelectAsset(state.Target().path, 0, {}, sourceGuid);
				}
			}
			for (const auto* database : {workspace.Database(), workspace.BuiltinDatabase()})
			{
				if (!database)
				{
					continue;
				}
				for (const auto& artifact : database->VisibleImportedObjects())
				{
					if (artifact.guid != sourceGuid || artifact.isMainObject)
					{
						continue;
					}
					const auto name = artifact.Label();
					if (ImGui::Selectable(name.c_str(), subAssetId == artifact.localFileId))
					{
						if (selectionContext)
						{
							selectionContext->SelectAsset(artifact.sourcePath, artifact.localFileId, artifact.kind,
							                              artifact.guid);
						}
					}
				}
			}
			ImGui::EndCombo();
		}
	}
}

void AssetPreviewPanel::DrawControls(ProjectWorkspace&)
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
				if (ImGui::Selectable("Bind pose", !active->SelectedClip()) && selectionContext &&
				    selectionContext->SelectAsset(state.Target().path, 0, {}, sourceGuid))
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
					if (ImGui::Selectable(clip.name.c_str(), active->SelectedClip() == clip.localFileId) &&
					    selectionContext &&
					    selectionContext->SelectAsset(state.Target().path, clip.localFileId, "AnimationClip",
					                                  sourceGuid))
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
	DrawSubAssets(workspace);
	if (!state.Target().Supported())
	{
		ImGui::TextDisabled("No preview is available for this object.");
		ImGui::EndChild();
		ImGui::PopID();
		return;
	}
	if (extensionPreview && extension)
	{
		if (extensionDiagnostic.empty())
		{
			const auto settings = extension->Invoke(Scripting::ManagedPreviewPhase::Settings);
			if (!settings)
			{
				extensionDiagnostic = settings.error;
			}
		}
		if (!extensionInfo.empty())
		{
			ImGui::TextWrapped("%s", extensionInfo.c_str());
		}
		if (!extensionDiagnostic.empty())
		{
			state.Fail(state.Generation(), extensionDiagnostic);
			if (state.HasImage())
			{
				ImGui::TextWrapped("Last successful preview (content %llu, pipeline %llu)",
				                   static_cast<unsigned long long>(state.DisplayedTarget().contentVersion),
				                   static_cast<unsigned long long>(state.DisplayedTarget().pipelineVersion));
			}
			ImGui::TextWrapped("%s", extensionDiagnostic.c_str());
		}
		const auto pos = ImGui::GetCursorScreenPos();
		const auto area = ImGui::GetContentRegionAvail();
		imageRect = {pos.x, pos.y, area.x, area.y};
		visible = area.x >= 1 && area.y >= 1 && ImGui::IsRectVisible(area);
		if (visible)
		{
			const auto dpi = CurrentViewportFramebufferScale();
			const ViewportSize nextSize{static_cast<UInt32>(std::max(1.f, area.x * dpi)),
			                            static_cast<UInt32>(std::max(1.f, area.y * dpi))};
			if (nextSize.width != targetSize.width || nextSize.height != targetSize.height)
			{
				targetSize = nextSize;
				state.Invalidate();
			}
			ImGui::InvisibleButton("Image", area,
			                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
			const auto& io = ImGui::GetIO();
			const bool hovered = ImGui::IsItemHovered(), dragging = ImGui::IsItemActive();
			const auto deltaTime = extensionWasVisible ? std::min(.1, Float64(io.DeltaTime)) : 0.;
			extensionTime += deltaTime;
			extensionWasVisible = true;
			extension->SetInput({io.MousePos.x - pos.x, io.MousePos.y - pos.y},
			                    dragging ? ViewportPoint{io.MouseDelta.x, io.MouseDelta.y} : ViewportPoint{},
			                    hovered ? io.MouseWheel : 0, dragging, extensionTime, deltaTime);
			if (dragging || hovered && io.MouseWheel != 0 || extensionConstantRepaint ||
			    extension->TakeRepaintRequest())
			{
				state.RequestRepaint();
			}
			if (extensionDiagnostic.empty() && state.NeedsRender())
			{
				// Prior rendering may still use the utility's private scene/resources.
				auto idle = device->WaitIdle();
				const auto drawn = idle
				                       ? extension->Invoke(Scripting::ManagedPreviewPhase::Draw, {0, 0, area.x, area.y})
				                       : Result<Scripting::ManagedPreviewResult>::Failure(idle.error);
				if (!drawn)
				{
					extensionDiagnostic = drawn.error;
					state.Fail(state.Generation(), drawn.error);
				}
				else
				{
					extensionDrawPending = true;
				}
			}
			auto* draw = ImGui::GetWindowDrawList();
			draw->PushClipRect(pos, {pos.x + area.x, pos.y + area.y}, true);
			draw->AddRectFilled(pos, {pos.x + area.x, pos.y + area.y}, IM_COL32(22, 25, 32, 255));
			if (state.HasImage())
			{
				const auto id = textureId != ImTextureID_Invalid ? textureId
				                : front                          ? front->ImageId()
				                                                 : ImTextureID_Invalid;
				if (id != ImTextureID_Invalid)
				{
					const auto& r = extensionOutput;
					draw->AddImage(id, {pos.x + r.x, pos.y + r.y}, {pos.x + r.x + r.width, pos.y + r.y + r.height});
				}
			}
			draw->PopClipRect();
		}
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
		const auto dpi = CurrentViewportFramebufferScale();
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
	if (extensionPreview)
	{
		if (!visible || !expanded)
		{
			extensionWasVisible = false;
			extensionDrawPending = false;
			return {};
		}
		if (!extensionDrawPending || !extension)
		{
			return {};
		}
		extensionDrawPending = false;
		const auto renderingGeneration = state.Generation();
		const auto activeExtension = extension;
		Result<void> result;
		if (activeExtension->Image())
		{
			ImTextureID nextId = ImTextureID_Invalid;
			Rhi::TexturePtr nextTexture;
			Rhi::SamplerPtr nextSampler;
			result = UploadTexturePreview(device, gui, activeExtension->Image(), nextId, nextTexture, nextSampler);
			if (result)
			{
				if (textureId != ImTextureID_Invalid)
				{
					retired.push_back(textureId);
				}
				textureId = nextId;
				texture = std::move(nextTexture);
				sampler = std::move(nextSampler);
			}
		}
		else if (activeExtension->RenderScene())
		{
			auto renderingSurface = std::move(back);
			if (!renderingSurface)
			{
				auto created = MaterialPreview::Create(device, gui);
				if (!created)
				{
					result = Result<void>::Failure(created.error);
				}
				else
				{
					renderingSurface = std::move(created.value);
				}
			}
			if (result)
			{
				const auto output = activeExtension->OutputRect();
				const auto scaleX = imageRect.width > 0 ? Float32(targetSize.width) / imageRect.width : 1.f;
				const auto scaleY = imageRect.height > 0 ? Float32(targetSize.height) / imageRect.height : 1.f;
				result = renderingSurface->Resize(std::max(1u, UInt32(std::round(output.width * scaleX))),
				                                  std::max(1u, UInt32(std::round(output.height * scaleY))));
			}
			if (result)
			{
				result = renderingSurface->RenderScene(*activeExtension->RenderScene(), activeExtension->RenderCamera(),
				                                       render, cameraIdentity);
			}
			if (result)
			{
				if (state.Generation() != renderingGeneration || extension != activeExtension)
				{
					return {};
				}
				back = std::move(front);
				front = std::move(renderingSurface);
				if (textureId != ImTextureID_Invalid)
				{
					retired.push_back(textureId);
				}
				textureId = ImTextureID_Invalid;
				texture.reset();
				sampler.reset();
			}
		}
		else
		{
			result = Result<void>::Failure("Custom Preview did not publish an image or EndPreview result");
		}
		if (state.Generation() != renderingGeneration || extension != activeExtension)
		{
			return {};
		}
		if (result)
		{
			extensionOutput = activeExtension->OutputRect();
			state.Complete(state.Generation());
			++renderCount;
		}
		else
		{
			extensionDiagnostic = result.error;
			state.Fail(state.Generation(), result.error);
		}
		return {};
	}
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
