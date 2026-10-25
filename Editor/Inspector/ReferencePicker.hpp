#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Material/Material.hpp>
#include <Scene/EditorRenderViewport.hpp>
#include <Scene/GameObject.hpp>
#include <Scripting/ScriptTypeDescriptor.hpp>
#include <Workspace/EditorDragSession.hpp>

#include <functional>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <optional>

struct ImGuiContext;

namespace Ursa::Editor
{
class AssetPreviewContent;
class AssetPreviewExtension;

struct ReferencePickerOption
{
	String key;
	String name;
	String type;
	String source;
	bool enabled = true;
	std::function<Result<MaterialTexturePtr>()> loadTexturePreview;
	std::function<Result<std::shared_ptr<MaterialInstance>>()> loadMaterialPreview;
	std::function<Result<std::unique_ptr<AssetPreviewContent>>()> loadAssetPreviewContent;
	std::function<Result<std::shared_ptr<AssetPreviewExtension>>()> createCustomPreview;
	// Optional per-candidate custom preview lifecycle. The picker pairs each successful begin
	// with exactly one end, independently of the Inspector footer preview.
	std::function<Result<void>()> beginPreview;
	std::function<void()> endPreview;
};

enum class ReferencePickerSourceFilter : Int32
{
	All,
	Assets,
	Scene
};

bool IsReferencePickerCandidateVisible(const ReferencePickerOption& option, const String& query,
                                       ReferencePickerSourceFilter sourceFilter, bool isNone = false);

Vector<Component*> CompatibleComponentsForReference(const GameObject& object, const String& expectedType,
                                                    const Vector<ScriptTypeDescriptor>* scriptTypes);
bool IsScriptReferenceTypeCompatible(const Vector<ScriptTypeDescriptor>& scriptTypes, const String& scriptType,
                                     const String& expectedType, ScriptTypeKind expectedKind);

// Per-frame transaction hooks for a picker-backed property. The callbacks are deliberately supplied
// by the caller on every frame; only the JSON checkpoint is retained by the picker session.
struct ReferencePickerTransaction
{
	std::function<Result<nlohmann::json>()> capture;
	std::function<Result<void>(Size)> preview;
	std::function<Result<void>(const nlohmann::json&)> previewCandidate;
	std::function<Result<bool>(const nlohmann::json&, const nlohmann::json&)> applyCandidate;
	std::function<Result<bool>(const Vector<nlohmann::json>&)> appendReferences;
	std::function<Result<void>(const nlohmann::json&)> restore;
};

struct ReferencePickerContextActionIds
{
	ImVec2 anchorCenter{};
	ImGuiID select = 0;
	ImGuiID clear = 0;
	ImGuiID locate = 0;
};

// Draws a field and its host-owned searchable picker. The caller supplies a stable control ID and
// receives the accepted option index on a later frame. Option zero should represent "None".
std::optional<Size> DrawReferencePickerButton(const String& controlId, const String& currentName,
                                              const String& expectedType, const Vector<ReferencePickerOption>& options,
                                              const String& currentKey = {}, const String& sessionOwner = {},
                                              const ReferencePickerTransaction* transaction = nullptr,
                                              nlohmann::json* acceptedCheckpoint = nullptr,
                                              std::function<Result<void>()> locate = {},
                                              ReferencePickerContextActionIds* contextActionIds = nullptr);

void ReleaseReferencePickerContext(ImGuiContext* context, class ImGuiBridge* bridge);
void CancelReferencePickerSessions(ImGuiContext* context);
bool HasActiveReferencePickerSession(ImGuiContext* context);
bool RequestCancelReferencePickerSessions(ImGuiContext* context, std::function<void()> afterCancellation = {});
bool RequestCancelReferencePickerForExternalClick(ImGuiContext* context);
void ProcessCompletedReferencePickerCancellations(ImGuiContext* context);
Result<void> RenderReferencePickerPreviews(Float64 deltaSeconds, const EditorRenderViewport& render);
} // namespace Ursa::Editor
