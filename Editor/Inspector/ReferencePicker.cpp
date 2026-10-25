#include <Inspector/ReferencePicker.hpp>
#include <Assets/AssetPreviewContent.hpp>
#include <Assets/AssetPreviewExtension.hpp>
#include <Assets/AssetPreviewTexture.hpp>
#include <Assets/MaterialPreview.hpp>
#include <Scene/Component.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Ui/ImGuiBridge.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

namespace Ursa::Editor
{
namespace
{
struct Session
{
	std::array<char, 256> search{};
	String selectedKey;
	String owner;
	String previewKey;
	String previewDiagnostic;
	std::function<void()> endPreview;
	String transactionDiagnostic;
	nlohmann::json transactionCheckpoint;
	MaterialTexturePtr previewSource;
	Rhi::TexturePtr previewTexture;
	Rhi::SamplerPtr previewSampler;
	std::shared_ptr<MaterialInstance> previewMaterial;
	std::unique_ptr<AssetPreviewContent> previewContent;
	std::shared_ptr<AssetPreviewExtension> customPreview;
	std::unique_ptr<MaterialPreview> materialPreview;
	Camera previewCamera;
	Float64 previewTime = 0;
	ResourceId previewIdentity = ResourceId::Generate();
	ImTextureID previewId = ImTextureID_Invalid;
	ImGuiContext* context = nullptr;
	ImGuiBridge* bridge = nullptr;
	bool wasOpen = false;
	bool cancelled = false;
	bool hasTransactionCheckpoint = false;
	bool transactionRequired = false;
	bool cancelRequested = false;
	Vector<std::function<void()>> afterCancellation;
	ReferencePickerSourceFilter sourceFilter = ReferencePickerSourceFilter::All;
	Int32 lastFrame = -1;
};

struct CompletedCancellation
{
	ImGuiContext* context = nullptr;
	std::function<void()> callback;
};

struct ImGuiIdScope
{
	~ImGuiIdScope()
	{
		ImGui::PopID();
	}
};

std::unordered_map<ImGuiID, Session> sessions;
Vector<CompletedCancellation> completedCancellations;

String Fold(String value)
{
	std::transform(value.begin(), value.end(), value.begin(),
	               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return value;
}

bool Matches(const ReferencePickerOption& option, const String& query)
{
	if (query.empty())
	{
		return true;
	}
	const String folded = Fold(query);
	return Fold(option.name).find(folded) != String::npos || Fold(option.type).find(folded) != String::npos ||
	       Fold(option.source).find(folded) != String::npos;
}

String UnqualifiedTypeName(const String& typeName)
{
	const Size separator = typeName.find(',');
	return typeName.substr(0, separator);
}

String NativeComponentTypeName(const String& expectedType)
{
	const Size separator = expectedType.rfind('.');
	String name = expectedType.substr(separator == String::npos ? 0 : separator + 1);
	if (name.size() >= 9 && name.substr(name.size() - 9) == "Component")
	{
		name.resize(name.size() - 9);
	}
	return "ursa." + name;
}

bool IsCompatibleComponentType(const Component& component, const String& expectedType,
                               const Vector<ScriptTypeDescriptor>* scriptTypes)
{
	if (expectedType.empty())
	{
		return true;
	}
	const String expected = UnqualifiedTypeName(expectedType);
	if (const auto* script = dynamic_cast<const Scripting::ScriptBehaviourComponent*>(&component))
	{
		if (expected == "Ursa.Component" || expected == "Ursa.Behaviour")
		{
			return true;
		}
		if (scriptTypes)
		{
			return IsScriptReferenceTypeCompatible(*scriptTypes, script->TypeName(), expected,
			                                       ScriptTypeKind::Behaviour);
		}
		return script->TypeName() == expected;
	}
	if (expected == "Ursa.Component")
	{
		return true;
	}
	return component.SerializationType() == NativeComponentTypeName(expected);
}

void RetirePreview(Session& session)
{
	if (session.endPreview)
	{
		auto end = std::move(session.endPreview);
		end();
	}
	if (session.previewId != ImTextureID_Invalid && session.bridge)
	{
		session.bridge->RetireTexture(session.previewId);
	}
	session.previewId = ImTextureID_Invalid;
	session.previewTexture.reset();
	session.previewSampler.reset();
	session.previewSource.reset();
	session.previewMaterial.reset();
	session.previewContent.reset();
	session.materialPreview.reset();
	session.customPreview.reset();
	session.previewKey.clear();
	session.previewDiagnostic.clear();
}

void PreparePreview(Session& session, const ReferencePickerOption* option)
{
	const String key = option ? option->key : String{};
	if (session.previewKey == key)
	{
		return;
	}
	RetirePreview(session);
	session.previewKey = key;
	if (!option)
	{
		return;
	}
	if (option->beginPreview)
	{
		auto begun = option->beginPreview();
		if (!begun)
		{
			session.previewDiagnostic = begun.error;
			return;
		}
		session.endPreview = option->endPreview;
	}
	if (option->createCustomPreview)
	{
		auto created = option->createCustomPreview();
		if (!created)
		{
			session.previewDiagnostic = created.error;
			return;
		}
		if (created.value)
		{
			session.customPreview = std::move(created.value);
			session.previewTime = 0;
			session.bridge = ImGuiBridge::Current();
			return;
		}
	}
	if (option->loadAssetPreviewContent)
	{
		auto loaded = option->loadAssetPreviewContent();
		if (!loaded)
		{
			session.previewDiagnostic = loaded.error;
			return;
		}
		session.previewContent = std::move(loaded.value);
		if (!session.previewContent)
		{
			session.previewDiagnostic = "Asset preview returned no content.";
			return;
		}
		session.bridge = ImGuiBridge::Current();
		if (!session.bridge)
		{
			session.previewDiagnostic = "GPU preview is unavailable in this editor context.";
			return;
		}
		if (session.previewContent->Texture() || session.previewContent->SourceTexture())
		{
			session.previewSource = session.previewContent->Texture() ? session.previewContent->Texture()
			                                                          : session.previewContent->SourceTexture();
			auto uploaded = UploadTexturePreview(session.bridge->Device(), *session.bridge, session.previewSource,
			                                     session.previewId, session.previewTexture, session.previewSampler);
			if (!uploaded)
			{
				session.previewDiagnostic = uploaded.error;
				session.previewSource.reset();
			}
			return;
		}
		if (!session.previewContent->Material() && !session.previewContent->RenderScene())
		{
			session.previewDiagnostic = session.previewContent->Diagnostic().empty()
			                                ? "Asset has no renderable preview content."
			                                : session.previewContent->Diagnostic();
			return;
		}
		session.previewCamera = {};
		Float32 radius = 0.8f;
		Vec3 center{};
		if (session.previewContent->RenderScene())
		{
			auto bounds = session.previewContent->GeometryBounds();
			if (bounds && !bounds.value.empty)
			{
				center = {(bounds.value.minimum.x + bounds.value.maximum.x) * 0.5f,
				          (bounds.value.minimum.y + bounds.value.maximum.y) * 0.5f,
				          (bounds.value.minimum.z + bounds.value.maximum.z) * 0.5f};
				const auto extent = bounds.value.maximum - bounds.value.minimum;
				radius = std::max(
				    0.01f,
				    static_cast<Float32>(std::hypot(Float64(extent.x), Float64(extent.y), Float64(extent.z)) * 0.5));
			}
		}
		const Float32 distance = std::max(1.25f, radius * 3.5f);
		session.previewCamera.target = center;
		session.previewCamera.position = {center.x + distance * 0.2f, center.y + distance * 0.15f, center.z + distance};
		session.previewCamera.nearPlane = std::max(0.0001f, radius * 0.001f);
		session.previewCamera.farPlane = std::max(100.f, distance + radius * 10.f);
		auto created = MaterialPreview::Create(session.bridge->Device(), *session.bridge, 160);
		if (!created)
		{
			session.previewDiagnostic = created.error;
			return;
		}
		session.materialPreview = std::move(created.value);
		session.previewMaterial = session.previewContent->Material();
		return;
	}
	if (option->loadMaterialPreview)
	{
		auto loaded = option->loadMaterialPreview();
		if (!loaded)
		{
			session.previewDiagnostic = loaded.error;
			return;
		}
		session.previewMaterial = std::move(loaded.value);
		session.bridge = ImGuiBridge::Current();
		if (!session.bridge || !session.previewMaterial)
		{
			session.previewMaterial.reset();
			session.previewDiagnostic = "Material preview is unavailable in this editor context.";
			return;
		}
		auto created = MaterialPreview::Create(session.bridge->Device(), *session.bridge, 160);
		if (!created)
		{
			session.previewMaterial.reset();
			session.previewDiagnostic = created.error;
			return;
		}
		session.materialPreview = std::move(created.value);
		return;
	}
	if (!option->loadTexturePreview)
	{
		session.previewDiagnostic = "No preview is available for this candidate.";
		return;
	}
	session.bridge = ImGuiBridge::Current();
	if (!session.bridge)
	{
		session.previewDiagnostic = "GPU preview is unavailable in this editor context.";
		if (session.endPreview)
		{
			session.endPreview();
			session.endPreview = {};
		}
		return;
	}
	auto loaded = option->loadTexturePreview();
	if (!loaded)
	{
		session.previewDiagnostic = loaded.error;
		return;
	}
	session.previewSource = std::move(loaded.value);
	auto uploaded = UploadTexturePreview(session.bridge->Device(), *session.bridge, session.previewSource,
	                                     session.previewId, session.previewTexture, session.previewSampler);
	if (!uploaded)
	{
		session.previewDiagnostic = uploaded.error;
		session.previewSource.reset();
	}
}
} // namespace

bool IsReferencePickerCandidateVisible(const ReferencePickerOption& option, const String& query,
                                       ReferencePickerSourceFilter sourceFilter, bool isNone)
{
	if (isNone)
	{
		return true;
	}
	const bool sourceMatches = sourceFilter == ReferencePickerSourceFilter::All ||
	                           (sourceFilter == ReferencePickerSourceFilter::Assets && option.source != "Scene") ||
	                           (sourceFilter == ReferencePickerSourceFilter::Scene && option.source == "Scene");
	return sourceMatches && Matches(option, query);
}

bool IsScriptReferenceTypeCompatible(const Vector<ScriptTypeDescriptor>& scriptTypes, const String& scriptType,
                                     const String& expectedType, ScriptTypeKind expectedKind)
{
	const String expected = UnqualifiedTypeName(expectedType);
	const auto descriptor = std::find_if(scriptTypes.begin(), scriptTypes.end(), [&](const ScriptTypeDescriptor& type)
	                                     { return type.scriptType == scriptType && type.kind == expectedKind; });
	return descriptor != scriptTypes.end() &&
	       (UnqualifiedTypeName(descriptor->clrTypeName) == expected ||
	        std::any_of(descriptor->baseTypeNames.begin(), descriptor->baseTypeNames.end(),
	                    [&](const String& baseType) { return UnqualifiedTypeName(baseType) == expected; }));
}

Vector<Component*> CompatibleComponentsForReference(const GameObject& object, const String& expectedType,
                                                    const Vector<ScriptTypeDescriptor>* scriptTypes)
{
	Vector<Component*> result;
	for (Component* component : object.Components())
	{
		if (IsCompatibleComponentType(*component, expectedType, scriptTypes))
		{
			result.push_back(component);
		}
	}
	return result;
}

std::optional<Size> DrawReferencePickerButton(const String& controlId, const String& currentName,
                                              const String& expectedType, const Vector<ReferencePickerOption>& options,
                                              const String& currentKey, const String& sessionOwner,
                                              const ReferencePickerTransaction* transaction,
                                              nlohmann::json* acceptedCheckpoint, std::function<Result<void>()> locate,
                                              ReferencePickerContextActionIds* contextActionIds)
{
	ImGui::PushID(controlId.c_str());
	ImGuiIdScope popId;

	const ImGuiID sessionId = ImGui::GetID("ReferencePicker");
	const String owner =
	    sessionOwner.empty() ? String(std::to_string(ImGui::GetCurrentWindow()->ID).c_str()) : sessionOwner;
	const auto frame = ImGui::GetFrameCount();
	for (Int32 index = ImGui::GetCurrentContext()->OpenPopupStack.Size - 1; index >= 0; --index)
	{
		const auto found = sessions.find(ImGui::GetCurrentContext()->OpenPopupStack[index].PopupId);
		if (found != sessions.end() && found->first != sessionId &&
		    found->second.context == ImGui::GetCurrentContext() && found->second.wasOpen &&
		    found->second.owner != owner)
		{
			found->second.cancelRequested = true;
			break;
		}
	}
	for (auto iterator = sessions.begin(); iterator != sessions.end();)
	{
		if (iterator->first != sessionId && iterator->second.context == ImGui::GetCurrentContext() &&
		    iterator->second.wasOpen && iterator->second.owner != owner)
		{
			iterator->second.cancelRequested = true;
			++iterator;
		}
		else
		{
			++iterator;
		}
	}
	for (auto iterator = sessions.begin(); iterator != sessions.end();)
	{
		if (iterator->first != sessionId && iterator->second.context == ImGui::GetCurrentContext() &&
		    iterator->second.lastFrame < frame - 1 && !iterator->second.wasOpen)
		{
			RetirePreview(iterator->second);
			iterator = sessions.erase(iterator);
		}
		else
		{
			++iterator;
		}
	}
	auto& session = sessions[sessionId];
	if (session.context != ImGui::GetCurrentContext())
	{
		RetirePreview(session);
		session = {};
		session.context = ImGui::GetCurrentContext();
	}
	session.lastFrame = frame;
	session.owner = owner;
	ImGui::BeginGroup();
	bool sessionDeferredByConflict = false;
	const auto beginSession = [&]
	{
		bool previousSessionOpen = false;
		for (auto& entry : sessions)
		{
			if (entry.first != sessionId && entry.second.context == ImGui::GetCurrentContext() && entry.second.wasOpen)
			{
				entry.second.cancelRequested = true;
				previousSessionOpen = true;
			}
		}
		if (previousSessionOpen)
		{
			sessionDeferredByConflict = true;
			return;
		}
		for (auto iterator = sessions.begin(); iterator != sessions.end();)
		{
			if (iterator->first != sessionId && iterator->second.context == ImGui::GetCurrentContext())
			{
				RetirePreview(iterator->second);
				iterator = sessions.erase(iterator);
			}
			else
			{
				++iterator;
			}
		}
		ImGui::OpenPopup("ReferencePicker");
		session.wasOpen = false;
		session.cancelled = false;
		session.hasTransactionCheckpoint = false;
		session.transactionRequired = transaction != nullptr;
		session.transactionDiagnostic.clear();
		if (transaction && transaction->capture && transaction->preview && transaction->restore)
		{
			auto captured = transaction->capture();
			if (captured)
			{
				session.transactionCheckpoint = std::move(captured.value);
				session.hasTransactionCheckpoint = true;
			}
			else
			{
				session.transactionDiagnostic = captured.error;
			}
		}
		session.sourceFilter = ReferencePickerSourceFilter::All;
		session.selectedKey.clear();
		auto current = options.end();
		if (!currentKey.empty())
		{
			current = std::find_if(options.begin(), options.end(),
			                       [&](const auto& option) { return option.key == currentKey; });
		}
		if (currentKey.empty())
		{
			current = std::find_if(options.begin(), options.end(),
			                       [&](const auto& option) { return option.name == currentName; });
			if (current != options.end() && std::count_if(options.begin(), options.end(), [&](const auto& option)
			                                              { return option.name == currentName; }) != 1)
			{
				current = options.end();
			}
		}
		if (current != options.end())
		{
			session.selectedKey = current->key;
		}
	};
	std::optional<Size> accepted;
	ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 34.0f));
	if (ImGui::Button(currentName.c_str()))
	{
		beginSession();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("..."))
	{
		beginSession();
	}
	if (contextActionIds)
	{
		const auto minimum = ImGui::GetItemRectMin();
		const auto maximum = ImGui::GetItemRectMax();
		contextActionIds->anchorCenter = {(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f};
	}
	bool contextOpenPicker = false;
	if (ImGui::BeginPopupContextItem("ReferencePickerActions"))
	{
		const bool select = ImGui::MenuItem("Select...");
		if (contextActionIds)
		{
			contextActionIds->select = ImGui::GetItemID();
		}
		if (select)
		{
			contextOpenPicker = true;
		}
		const bool clear = ImGui::MenuItem("Clear", nullptr, false, !currentKey.empty());
		if (contextActionIds)
		{
			contextActionIds->clear = ImGui::GetItemID();
		}
		if (clear)
		{
			if (acceptedCheckpoint && transaction && transaction->capture)
			{
				auto captured = transaction->capture();
				if (captured)
				{
					*acceptedCheckpoint = std::move(captured.value);
				}
			}
			accepted = 0;
		}
		const bool locateSelected = locate && ImGui::MenuItem("Locate");
		if (contextActionIds && locate)
		{
			contextActionIds->locate = ImGui::GetItemID();
		}
		if (locateSelected)
		{
			auto located = locate();
			if (!located)
			{
				ImGui::SetTooltip("%s", located.error.c_str());
			}
		}
		ImGui::EndPopup();
	}
	if (contextOpenPicker)
	{
		beginSession();
	}
	if (sessionDeferredByConflict && ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("The previous picker was cancelled. Click this field again to open it.");
	}
	ImGui::EndGroup();
	ImGui::SetNextWindowSize(ImVec2(560.0f, 390.0f), ImGuiCond_Appearing);
	if (ImGui::BeginPopup("ReferencePicker"))
	{
		session.wasOpen = true;
		if (session.cancelRequested)
		{
			if (session.hasTransactionCheckpoint)
			{
				if (!transaction || !transaction->restore)
				{
					session.transactionDiagnostic =
					    "Cannot cancel the picker because its original target is unavailable.";
					ImGui::TextWrapped("Selection transaction: %s", session.transactionDiagnostic.c_str());
					ImGui::EndPopup();
					return accepted;
				}
				auto restored = transaction->restore(session.transactionCheckpoint);
				if (!restored)
				{
					session.transactionDiagnostic = "Cannot cancel the picker: " + restored.error;
					ImGui::TextWrapped("Selection transaction: %s", session.transactionDiagnostic.c_str());
					ImGui::EndPopup();
					return accepted;
				}
			}
			ImGui::CloseCurrentPopup();
			RetirePreview(session);
			auto callbacks = std::move(session.afterCancellation);
			sessions.erase(sessionId);
			ImGui::EndPopup();
			for (auto& callback : callbacks)
			{
				if (callback)
				{
					completedCancellations.push_back({ImGui::GetCurrentContext(), std::move(callback)});
				}
			}
			return accepted;
		}
		ImGui::Text("Select %s", expectedType.c_str());
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputText("Search", session.search.data(), session.search.size());
		const bool searchHasFocus = ImGui::IsItemActive();
		const bool hasAssetCandidates = std::any_of(options.begin() + std::min<Size>(1, options.size()), options.end(),
		                                            [](const auto& option) { return option.source != "Scene"; });
		const bool hasSceneCandidates = std::any_of(options.begin() + std::min<Size>(1, options.size()), options.end(),
		                                            [](const auto& option) { return option.source == "Scene"; });
		if (session.sourceFilter == ReferencePickerSourceFilter::Assets && !hasAssetCandidates)
		{
			session.sourceFilter = ReferencePickerSourceFilter::All;
		}
		if (session.sourceFilter == ReferencePickerSourceFilter::Scene && !hasSceneCandidates)
		{
			session.sourceFilter = ReferencePickerSourceFilter::All;
		}
		if (ImGui::RadioButton("All", session.sourceFilter == ReferencePickerSourceFilter::All))
		{
			session.sourceFilter = ReferencePickerSourceFilter::All;
		}
		if (hasAssetCandidates)
		{
			ImGui::SameLine();
			if (ImGui::RadioButton("Assets", session.sourceFilter == ReferencePickerSourceFilter::Assets))
			{
				session.sourceFilter = ReferencePickerSourceFilter::Assets;
			}
		}
		if (hasSceneCandidates)
		{
			ImGui::SameLine();
			if (ImGui::RadioButton("Scene", session.sourceFilter == ReferencePickerSourceFilter::Scene))
			{
				session.sourceFilter = ReferencePickerSourceFilter::Scene;
			}
		}
		Vector<Size> visible;
		for (Size index = 0; index < options.size(); ++index)
		{
			if (IsReferencePickerCandidateVisible(options[index], session.search.data(), session.sourceFilter,
			                                      index == 0))
			{
				visible.push_back(index);
			}
		}
		if (!session.selectedKey.empty() && std::none_of(options.begin(), options.end(), [&](const auto& option)
		                                                 { return option.key == session.selectedKey; }))
		{
			if (session.hasTransactionCheckpoint && transaction && transaction->restore)
			{
				auto restored = transaction->restore(session.transactionCheckpoint);
				if (!restored)
				{
					session.transactionDiagnostic =
					    "The selected candidate disappeared; rollback failed: " + restored.error;
				}
				else
				{
					session.transactionDiagnostic =
					    "The selected candidate disappeared; the original values were restored.";
				}
				session.hasTransactionCheckpoint = false;
			}
			session.cancelled = true;
			session.selectedKey.clear();
			ImGui::CloseCurrentPopup();
		}
		bool candidateSelectedByInput = false;
		if ((!searchHasFocus || session.search.front() == '\0') && !visible.empty() &&
		    (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
		{
			auto selected = std::find_if(visible.begin(), visible.end(),
			                             [&](Size index) { return options[index].key == session.selectedKey; });
			Int32 position =
			    selected == visible.end() ? 0 : static_cast<Int32>(std::distance(visible.begin(), selected));
			position += ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : -1;
			position = std::clamp(position, 0, static_cast<Int32>(visible.size()) - 1);
			session.selectedKey = options[visible[static_cast<Size>(position)]].key;
			candidateSelectedByInput = true;
		}
		bool candidateClicked = false;
		ImGui::BeginChild("PickerCandidates", ImVec2(300.0f, 252.0f), true);
		for (Size index : visible)
		{
			const auto& option = options[index];
			ImGui::PushID(option.key.c_str());
			ImGui::BeginDisabled(!option.enabled);
			const bool selected = option.key == session.selectedKey;
			if (ImGui::Selectable(option.name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick) &&
			    option.enabled)
			{
				session.selectedKey = option.key;
				candidateClicked = true;
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
				    (!session.transactionRequired || session.hasTransactionCheckpoint))
				{
					accepted = index;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		ImGui::EndChild();
		if ((candidateClicked || candidateSelectedByInput) && session.hasTransactionCheckpoint && transaction &&
		    transaction->preview)
		{
			const auto candidate = std::find_if(options.begin(), options.end(),
			                                    [&](const auto& option) { return option.key == session.selectedKey; });
			if (candidate != options.end())
			{
				auto previewed = transaction->preview(static_cast<Size>(std::distance(options.begin(), candidate)));
				if (!previewed)
				{
					session.transactionDiagnostic = previewed.error;
					auto restored = transaction->restore(session.transactionCheckpoint);
					if (!restored)
					{
						session.transactionDiagnostic += "; rollback failed: " + restored.error;
					}
					session.hasTransactionCheckpoint = false;
					session.cancelled = true;
					ImGui::CloseCurrentPopup();
				}
			}
		}
		ImGui::SameLine();
		ImGui::BeginChild("PickerPreview", ImVec2(0.0f, 252.0f), true);
		const auto selected = std::find_if(options.begin(), options.end(),
		                                   [&](const auto& option) { return option.key == session.selectedKey; });
		PreparePreview(session, selected == options.end() ? nullptr : &*selected);
		if (selected == options.end())
		{
			ImGui::TextUnformatted("No candidate selected");
		}
		else
		{
			ImGui::TextUnformatted(selected->name.c_str());
			ImGui::Separator();
			ImGui::Text("Type: %s", selected->type.c_str());
			ImGui::TextWrapped("Source: %s", selected->source.c_str());
			if (!selected->enabled)
			{
				ImGui::TextDisabled("This candidate is not assignable.");
			}
			if (session.previewId != ImTextureID_Invalid && session.previewSource)
			{
				const Float32 width = static_cast<Float32>(session.previewSource->GetWidth());
				const Float32 height = static_cast<Float32>(session.previewSource->GetHeight());
				const Float32 scale = std::min(160.0f / std::max(width, 1.0f), 160.0f / std::max(height, 1.0f));
				ImGui::Image(session.previewId, {width * scale, height * scale});
			}
			else if (session.materialPreview)
			{
				ImGui::Image(session.materialPreview->ImageId(), {160, 160});
			}
			else if (session.customPreview || session.previewContent)
			{
				ImGui::TextDisabled("Preparing preview…");
			}
			else if (!session.previewDiagnostic.empty())
			{
				ImGui::TextWrapped("Preview unavailable: %s", session.previewDiagnostic.c_str());
			}
			else
			{
				ImGui::TextDisabled("No preview is available for this candidate.");
			}
		}
		ImGui::EndChild();
		if (visible.size() == 1 && !session.search.empty() && session.search.front() != '\0')
		{
			ImGui::TextDisabled("No matching objects.");
		}
		const auto selectedIndex = std::find_if(visible.begin(), visible.end(),
		                                        [&](Size index) { return options[index].key == session.selectedKey; });
		if (ImGui::Button("Select") && selectedIndex != visible.end() && options[*selectedIndex].enabled &&
		    (!session.transactionRequired || session.hasTransactionCheckpoint))
		{
			accepted = *selectedIndex;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			session.cancelled = true;
			if (session.hasTransactionCheckpoint && transaction && transaction->restore)
			{
				auto restored = transaction->restore(session.transactionCheckpoint);
				if (!restored)
				{
					session.transactionDiagnostic = restored.error;
				}
				session.hasTransactionCheckpoint = false;
			}
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			if (searchHasFocus && session.search.front() != '\0')
			{
				session.search.front() = '\0';
			}
			else
			{
				session.cancelled = true;
				if (session.hasTransactionCheckpoint && transaction && transaction->restore)
				{
					auto restored = transaction->restore(session.transactionCheckpoint);
					if (!restored)
					{
						session.transactionDiagnostic = restored.error;
					}
					session.hasTransactionCheckpoint = false;
				}
				ImGui::CloseCurrentPopup();
			}
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Enter) && selectedIndex != visible.end() &&
		         options[*selectedIndex].enabled && (!session.transactionRequired || session.hasTransactionCheckpoint))
		{
			accepted = *selectedIndex;
			ImGui::CloseCurrentPopup();
		}
		if (!session.transactionDiagnostic.empty())
		{
			ImGui::TextWrapped("Selection transaction: %s", session.transactionDiagnostic.c_str());
		}
		ImGui::EndPopup();
	}
	else if (session.wasOpen)
	{
		if (session.cancelRequested)
		{
			if (session.hasTransactionCheckpoint)
			{
				if (!transaction || !transaction->restore)
				{
					session.transactionDiagnostic =
					    "Cannot cancel the picker because its original target is unavailable.";
					return accepted;
				}
				auto restored = transaction->restore(session.transactionCheckpoint);
				if (!restored)
				{
					session.transactionDiagnostic = "Cannot cancel the picker: " + restored.error;
					return accepted;
				}
			}
			auto callbacks = std::move(session.afterCancellation);
			RetirePreview(session);
			sessions.erase(sessionId);
			for (auto& callback : callbacks)
			{
				if (callback)
				{
					completedCancellations.push_back({ImGui::GetCurrentContext(), std::move(callback)});
				}
			}
			return accepted;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			session.cancelled = true;
		}
		if (!session.cancelled && !session.selectedKey.empty() &&
		    (!session.transactionRequired || session.hasTransactionCheckpoint))
		{
			const auto selected = std::find_if(options.begin(), options.end(),
			                                   [&](const auto& option) { return option.key == session.selectedKey; });
			if (selected != options.end() && selected->enabled)
			{
				accepted = static_cast<Size>(std::distance(options.begin(), selected));
				if (acceptedCheckpoint && session.hasTransactionCheckpoint)
				{
					*acceptedCheckpoint = session.transactionCheckpoint;
				}
			}
		}
		else if (session.transactionRequired && !session.hasTransactionCheckpoint)
		{
			session.cancelled = true;
		}
		else if (session.cancelled && session.hasTransactionCheckpoint && transaction && transaction->restore)
		{
			auto restored = transaction->restore(session.transactionCheckpoint);
			if (!restored)
			{
				session.transactionDiagnostic = restored.error;
			}
		}
		session.hasTransactionCheckpoint = false;
		session.wasOpen = false;
		session.cancelled = false;
		session.selectedKey.clear();
		session.search.front() = '\0';
	}
	if (accepted || (!ImGui::IsPopupOpen("ReferencePicker") && !session.wasOpen))
	{
		if (accepted && acceptedCheckpoint && session.hasTransactionCheckpoint)
		{
			*acceptedCheckpoint = session.transactionCheckpoint;
		}
		RetirePreview(session);
		sessions.erase(sessionId);
	}
	return accepted;
}

void ReleaseReferencePickerContext(ImGuiContext* context, ImGuiBridge* bridge)
{
	for (auto iterator = sessions.begin(); iterator != sessions.end();)
	{
		if (iterator->second.context != context)
		{
			++iterator;
			continue;
		}
		if (iterator->second.previewId != ImTextureID_Invalid && bridge)
		{
			bridge->RetireTexture(iterator->second.previewId);
		}
		if (iterator->second.endPreview)
		{
			auto end = std::move(iterator->second.endPreview);
			end();
		}
		iterator->second.previewId = ImTextureID_Invalid;
		iterator->second.previewTexture.reset();
		iterator->second.previewSampler.reset();
		iterator = sessions.erase(iterator);
	}
}

void CancelReferencePickerSessions(ImGuiContext* context)
{
	if (!context)
	{
		return;
	}
	ImGuiContext* previous = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(context);
	for (Int32 index = context->OpenPopupStack.Size - 1; index >= 0; --index)
	{
		const auto found = sessions.find(context->OpenPopupStack[index].PopupId);
		if (found != sessions.end() && found->second.context == context)
		{
			ImGui::ClosePopupToLevel(index, true);
			break;
		}
	}
	for (auto iterator = sessions.begin(); iterator != sessions.end();)
	{
		if (iterator->second.context == context)
		{
			RetirePreview(iterator->second);
			iterator = sessions.erase(iterator);
		}
		else
		{
			++iterator;
		}
	}
	ImGui::SetCurrentContext(previous);
}

bool HasActiveReferencePickerSession(ImGuiContext* context)
{
	return context && std::any_of(sessions.begin(), sessions.end(), [&](const auto& entry)
	                              { return entry.second.context == context && entry.second.wasOpen; });
}

bool RequestCancelReferencePickerSessions(ImGuiContext* context, std::function<void()> afterCancellation)
{
	if (!context)
	{
		if (afterCancellation)
		{
			afterCancellation();
		}
		return false;
	}
	for (auto& entry : sessions)
	{
		auto& session = entry.second;
		if (session.context != context || !session.wasOpen)
		{
			continue;
		}
		session.cancelRequested = true;
		if (afterCancellation)
		{
			session.afterCancellation.push_back(std::move(afterCancellation));
		}
		return true;
	}
	if (afterCancellation)
	{
		afterCancellation();
	}
	return false;
}

bool RequestCancelReferencePickerForExternalClick(ImGuiContext* context)
{
	if (!context || context->OpenPopupStack.empty() ||
	    (!context->IO.MouseClicked[ImGuiMouseButton_Left] && !context->IO.MouseClicked[ImGuiMouseButton_Right]))
	{
		return false;
	}
	auto found = sessions.find(context->OpenPopupStack.back().PopupId);
	if (found == sessions.end() || found->second.context != context || !found->second.wasOpen)
	{
		return false;
	}
	ImGuiWindow* popup = context->OpenPopupStack.back().Window;
	if (!popup)
	{
		return false;
	}
	const ImVec2 mouse = context->IO.MousePos;
	const bool insidePopup = mouse.x >= popup->Pos.x && mouse.y >= popup->Pos.y &&
	                         mouse.x < popup->Pos.x + popup->Size.x && mouse.y < popup->Pos.y + popup->Size.y;
	if (insidePopup)
	{
		return false;
	}
	const bool insideEditor = std::any_of(context->Viewports.begin(), context->Viewports.end(),
	                                      [&](const auto* viewport)
	                                      {
		                                      return mouse.x >= viewport->Pos.x && mouse.y >= viewport->Pos.y &&
		                                             mouse.x < viewport->Pos.x + viewport->Size.x &&
		                                             mouse.y < viewport->Pos.y + viewport->Size.y;
	                                      });
	if (insideEditor)
	{
		found->second.cancelRequested = true;
		return true;
	}
	return false;
}

void ProcessCompletedReferencePickerCancellations(ImGuiContext* context)
{
	if (!context)
	{
		return;
	}
	Vector<std::function<void()>> callbacks;
	for (auto iterator = completedCancellations.begin(); iterator != completedCancellations.end();)
	{
		if (iterator->context == context)
		{
			callbacks.push_back(std::move(iterator->callback));
			iterator = completedCancellations.erase(iterator);
		}
		else
		{
			++iterator;
		}
	}
	for (auto& callback : callbacks)
	{
		if (callback)
		{
			callback();
		}
	}
}

Result<void> RenderReferencePickerPreviews(Float64 deltaSeconds, const EditorRenderViewport& render)
{
	for (auto& [id, session] : sessions)
	{
		(void)id;
		if (!session.wasOpen)
		{
			continue;
		}
		const auto clearTexture = [&]
		{
			if (session.previewId != ImTextureID_Invalid && session.bridge)
			{
				session.bridge->RetireTexture(session.previewId);
			}
			session.previewId = ImTextureID_Invalid;
			session.previewTexture.reset();
			session.previewSampler.reset();
			session.previewSource.reset();
		};
		const auto clearScene = [&] { session.materialPreview.reset(); };
		if (session.customPreview)
		{
			session.previewTime += std::max(0.0, deltaSeconds);
			session.customPreview->SetInput({}, {}, 0, false, session.previewTime, deltaSeconds);
			const ViewportRect bounds{0, 0, 160, 160};
			auto draw = session.customPreview->Invoke(Scripting::ManagedPreviewPhase::Draw, bounds);
			if (!draw)
			{
				session.previewDiagnostic = draw.error;
				clearTexture();
				clearScene();
				continue;
			}
			if (session.customPreview->Image())
			{
				clearScene();
				if (!session.bridge)
				{
					clearTexture();
					session.previewDiagnostic = "Custom Preview GPU upload is unavailable in this editor context.";
					continue;
				}
				clearTexture();
				auto uploaded =
				    UploadTexturePreview(session.bridge->Device(), *session.bridge, session.customPreview->Image(),
				                         session.previewId, session.previewTexture, session.previewSampler);
				if (!uploaded)
				{
					session.previewDiagnostic = uploaded.error;
					session.previewSource.reset();
				}
				else
				{
					session.previewSource = session.customPreview->Image();
					session.previewDiagnostic.clear();
				}
				continue;
			}
			if (session.customPreview->RenderScene())
			{
				if (!session.bridge)
				{
					clearTexture();
					clearScene();
					session.previewDiagnostic = "Custom Preview rendering is unavailable in this editor context.";
					continue;
				}
				clearTexture();
				if (!session.materialPreview)
				{
					auto created = MaterialPreview::Create(session.bridge->Device(), *session.bridge, 160);
					if (!created)
					{
						session.previewDiagnostic = created.error;
						continue;
					}
					session.materialPreview = std::move(created.value);
				}
				auto rendered = session.materialPreview->RenderScene(*session.customPreview->RenderScene(),
				                                                     session.customPreview->RenderCamera(), render,
				                                                     session.previewIdentity);
				if (!rendered)
				{
					session.previewDiagnostic = rendered.error;
					clearScene();
				}
				else
				{
					session.previewDiagnostic.clear();
				}
				continue;
			}
			session.previewDiagnostic = "Custom Preview did not publish an image or render scene.";
			clearTexture();
			clearScene();
			continue;
		}
		if (!session.materialPreview ||
		    (!session.previewMaterial && (!session.previewContent || !session.previewContent->RenderScene())))
		{
			continue;
		}
		Result<void> rendered =
		    session.previewMaterial
		        ? session.materialPreview->Render(session.previewMaterial, session.previewCamera, render,
		                                          session.previewIdentity)
		        : session.materialPreview->RenderScene(*session.previewContent->RenderScene(), session.previewCamera,
		                                               render, session.previewIdentity);
		if (!rendered)
		{
			session.previewDiagnostic = rendered.error;
			session.previewMaterial.reset();
			session.materialPreview.reset();
		}
	}
	return {};
}
} // namespace Ursa::Editor
