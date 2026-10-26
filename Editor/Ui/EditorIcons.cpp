#include <Ui/EditorIcons.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/Lighting.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <functional>

namespace Ursa::Editor::Icons
{
namespace
{
std::function<void()> panelResetAction;
constexpr const char* names[]{"Play",
                              "Stop",
                              "Pause",
                              "Step",
                              "Move",
                              "Rotate",
                              "Scale",
                              "Fit",
                              "Folder",
                              "File",
                              "Scene",
                              "Prefab",
                              "Material",
                              "Texture",
                              "Shader",
                              "Mesh",
                              "Model",
                              "Script",
                              "ScriptableObject",
                              "GameObject",
                              "Camera",
                              "Light",
                              "PointLight",
                              "Renderer",
                              "SkinnedMesh",
                              "Component",
                              "Add",
                              "Remove",
                              "More",
                              "Refresh",
                              "Search",
                              "Rename",
                              "Save",
                              "Delete",
                              "Close",
                              "Check",
                              "Revert",
                              "Up",
                              "Down",
                              "Locate",
                              "Trace",
                              "Debug",
                              "Info",
                              "Warning",
                              "Error",
                              "Import",
                              "Build",
                              "Settings",
                              "Project",
                              "Hierarchy",
                              "Inspector",
                              "Console",
                              "Link",
                              "Copy",
                              "Pin",
                              "Favorite"};
static_assert(sizeof(names) / sizeof(*names) == static_cast<Size>(Icon::Count));

struct Region
{
	ImVec2 first;
	ImVec2 last;
};

struct Atlas
{
	ImTextureID texture = 0;
	Array<Region, static_cast<Size>(Icon::Count)> regions;
};

UnorderedMap<ImGuiContext*, Atlas> atlases;

Float32 Extent()
{
	return std::max(16.f, ImGui::GetFontSize());
}

void Hint(const char* label)
{
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("%s%s", label,
		                  (GImGui->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) ? " (unavailable)" : "");
	}
}

void RowImage(Icon icon, ImVec2 origin, Float32 offset)
{
	Draw(icon, {origin.x + offset, origin.y + (ImGui::GetTextLineHeight() - Extent()) * 0.5f}, Extent(), Color(icon));
}

String Padded(const char* label)
{
	const Float32 spaces = std::ceil((Extent() + ImGui::GetStyle().ItemInnerSpacing.x) / ImGui::CalcTextSize(" ").x);
	return String(static_cast<Size>(spaces), ' ') + label;
}
} // namespace

const char* Name(Icon icon)
{
	const Size index = static_cast<Size>(icon);
	return index < static_cast<Size>(Icon::Count) ? names[index] : "File";
}

Result<void> SetAtlas(ImGuiContext* context, ImTextureID texture, const String& manifest)
{
	ClearAtlas(context);
	if (!context || !texture)
	{
		return Result<void>::Failure("Editor icon atlas requires a context and texture");
	}
	try
	{
		const auto document = nlohmann::json::parse(manifest.begin(), manifest.end());
		if (document.at("version") != 1 || document.at("width").get<UInt32>() == 0 ||
		    document.at("height").get<UInt32>() == 0)
		{
			return Result<void>::Failure("Unsupported Editor icon manifest");
		}
		Atlas atlas;
		atlas.texture = texture;
		for (Size index = 0; index < static_cast<Size>(Icon::Count); ++index)
		{
			const auto& entry = document.at("icons").at(names[index]);
			auto& region = atlas.regions[index];
			region.first = {entry.at("uv0").at(0).get<Float32>(), entry.at("uv0").at(1).get<Float32>()};
			region.last = {entry.at("uv1").at(0).get<Float32>(), entry.at("uv1").at(1).get<Float32>()};
			if (!std::isfinite(region.first.x) || !std::isfinite(region.first.y) || !std::isfinite(region.last.x) ||
			    !std::isfinite(region.last.y) || region.first.x < 0 || region.first.y < 0 || region.last.x > 1 ||
			    region.last.y > 1 || region.first.x >= region.last.x || region.first.y >= region.last.y)
			{
				return Result<void>::Failure("Invalid Editor icon UV coordinates");
			}
			for (Size previous = 0; previous < index; ++previous)
			{
				const auto& other = atlas.regions[previous];
				if (region.first.x < other.last.x && region.last.x > other.first.x && region.first.y < other.last.y &&
				    region.last.y > other.first.y)
				{
					return Result<void>::Failure("Overlapping Editor icon UV coordinates");
				}
			}
		}
		atlases.emplace(context, std::move(atlas));
		return {};
	}
	catch (const std::exception&)
	{
		return Result<void>::Failure("Malformed Editor icon manifest");
	}
}

void ClearAtlas(ImGuiContext* context)
{
	atlases.erase(context);
}

bool Available()
{
	return atlases.find(ImGui::GetCurrentContext()) != atlases.end();
}

ImVec4 Color(Icon icon)
{
	ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	switch (icon)
	{
		case Icon::Folder:
			color = {0.94f, 0.75f, 0.38f, 1.f};
			break;
		case Icon::Prefab:
			color = {0.38f, 0.69f, 1.f, 1.f};
			break;
		case Icon::Script:
		case Icon::ScriptableObject:
			color = {0.52f, 0.82f, 0.65f, 1.f};
			break;
		case Icon::Light:
		case Icon::PointLight:
		case Icon::Warning:
			color = {1.f, 0.78f, 0.3f, 1.f};
			break;
		case Icon::Error:
			color = {1.f, 0.4f, 0.4f, 1.f};
			break;
		case Icon::Debug:
			color = {0.55f, 0.75f, 0.95f, 1.f};
			break;
		case Icon::Trace:
			color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
			break;
		default:
			break;
	}
	color.w *= ImGui::GetStyle().Alpha;
	return color;
}

Icon ForCommand(const String& label)
{
	if (label == "Directional Light")
	{
		return Icon::Light;
	}
	if (label == "Point Light")
	{
		return Icon::PointLight;
	}
	if (label == "Mesh Filter")
	{
		return Icon::Mesh;
	}
	if (label == "Mesh Renderer")
	{
		return Icon::Renderer;
	}
	if (label == "Skinned Mesh Renderer")
	{
		return Icon::SkinnedMesh;
	}
	if (label == "Unlit Material" || label == "PBR Material")
	{
		return Icon::Material;
	}
	if (label.find("Save") == 0)
	{
		return Icon::Save;
	}
	if (label.find("Reimport") == 0 || label == "Import")
	{
		return Icon::Import;
	}
	if (label.find("Refresh") != String::npos || label == "Auto Refresh")
	{
		return Icon::Refresh;
	}
	if (label.find("Rename") == 0 || label == "Edit")
	{
		return Icon::Rename;
	}
	if (label.find("Delete") == 0 || label == "Discard")
	{
		return Icon::Delete;
	}
	if (label.find("Remove") == 0 || label == "-")
	{
		return Icon::Remove;
	}
	if (label.find("Add") == 0 || label.find("Create") == 0)
	{
		return Icon::Add;
	}
	if (label.find("Apply") == 0 || label == "Use as game camera")
	{
		return Icon::Check;
	}
	if (label.find("Revert") == 0 || label.find("Reset") == 0 || label == "Undo")
	{
		return Icon::Revert;
	}
	if (label.find("Copy") == 0 || label == "Make Local Copy")
	{
		return Icon::Copy;
	}
	if (label.find("Pin") == 0)
	{
		return Icon::Pin;
	}
	if (label == "Up" || label == "Move up")
	{
		return Icon::Up;
	}
	if (label == "Down" || label == "Move down")
	{
		return Icon::Down;
	}
	if (label == "Cancel" || label.find("Close") == 0 || label == "Exit")
	{
		return Icon::Close;
	}
	if (label == "Clear")
	{
		return Icon::Delete;
	}
	if (label == "Locate")
	{
		return Icon::Locate;
	}
	if (label.find("Compile") == 0 || label.find("Build") != String::npos)
	{
		return Icon::Build;
	}
	if (label.find("Open") == 0 || label == "External Tools")
	{
		return Icon::Link;
	}
	if (label.find("Settings") != String::npos || label == "Tools")
	{
		return Icon::Settings;
	}
	for (Size index = 0; index < static_cast<Size>(Icon::Count); ++index)
	{
		if (label == names[index])
		{
			return static_cast<Icon>(index);
		}
	}
	return Icon::Component;
}

Icon ForAsset(bool folder, std::optional<ResourceKind> kind, const String& importer, const String& path)
{
	if (folder)
	{
		return Icon::Folder;
	}
	if (kind)
	{
		switch (*kind)
		{
			case ResourceKind::Texture:
				return Icon::Texture;
			case ResourceKind::Material:
				return Icon::Material;
			case ResourceKind::Scene:
				return Icon::Scene;
			case ResourceKind::Prefab:
				return Icon::Prefab;
			case ResourceKind::Shader:
				return Icon::Shader;
			case ResourceKind::Mesh:
				return Icon::Mesh;
		}
	}
	if (importer == "ursa.Model")
	{
		return Icon::Model;
	}
	if (importer == "ursa.Texture")
	{
		return Icon::Texture;
	}
	if (importer.find("Scriptable") != String::npos)
	{
		return Icon::ScriptableObject;
	}
	if (importer.find("Script") != String::npos)
	{
		return Icon::Script;
	}
	const auto separator = path.find_last_of('.');
	String extension = separator == String::npos ? String{} : path.substr(separator);
	std::transform(extension.begin(), extension.end(), extension.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (extension == ".cs")
	{
		return Icon::Script;
	}
	if (extension == ".ursaasset")
	{
		return Icon::ScriptableObject;
	}
	for (const char* suffix : {".fbx", ".obj", ".gltf", ".glb", ".dae"})
	{
		if (extension == suffix)
		{
			return Icon::Model;
		}
	}
	for (const char* suffix : {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"})
	{
		if (extension == suffix)
		{
			return Icon::Texture;
		}
	}
	return Icon::File;
}

Icon ForComponent(const String& type, bool script)
{
	if (script)
	{
		return Icon::Script;
	}
	if (type == "ursa.Transform" || type == "Transform")
	{
		return Icon::Move;
	}
	if (type == "ursa.Camera")
	{
		return Icon::Camera;
	}
	if (type == "ursa.MeshFilter")
	{
		return Icon::Mesh;
	}
	if (type == "ursa.MeshRenderer")
	{
		return Icon::Renderer;
	}
	if (type == "ursa.SkinnedMeshRenderer")
	{
		return Icon::SkinnedMesh;
	}
	if (type == "ursa.DirectionalLight")
	{
		return Icon::Light;
	}
	if (type == "ursa.PointLight")
	{
		return Icon::PointLight;
	}
	return Icon::Component;
}

Icon ForObject(const GameObject::Ptr& object, bool prefab)
{
	if (prefab)
	{
		return Icon::Prefab;
	}
	Icon result = Icon::GameObject;
	if (!object)
	{
		return result;
	}
	for (const auto* component : object->Components())
	{
		if (dynamic_cast<const CameraComponent*>(component))
		{
			return Icon::Camera;
		}
		if (dynamic_cast<const DirectionalLightComponent*>(component))
		{
			result = Icon::Light;
		}
		else if (dynamic_cast<const PointLightComponent*>(component) && result != Icon::Light)
		{
			result = Icon::PointLight;
		}
		else if (dynamic_cast<const RendererComponent*>(component) && result == Icon::GameObject)
		{
			result = Icon::Renderer;
		}
	}
	return result;
}

void Paint(ImDrawList* list, Icon icon, ImVec2 position, Float32 size, ImVec4 color)
{
	const auto found = atlases.find(ImGui::GetCurrentContext());
	if (found == atlases.end())
	{
		return;
	}
	const Size index = static_cast<Size>(icon) < static_cast<Size>(Icon::Count) ? static_cast<Size>(icon)
	                                                                            : static_cast<Size>(Icon::File);
	const auto& region = found->second.regions[index];
	// The artwork has six pixels of padding per cell. Include it outside the requested visible extent.
	const Float32 pad = size / 6.f;
	list->AddImage(found->second.texture, {position.x - pad, position.y - pad},
	               {position.x + size + pad, position.y + size + pad}, region.first, region.last,
	               ImGui::ColorConvertFloat4ToU32(color));
}

void Draw(Icon icon, ImVec2 position, Float32 size, ImVec4 color)
{
	Paint(ImGui::GetWindowDrawList(), icon, position, size, color);
}

void Inline(Icon icon)
{
	const auto position = ImGui::GetCursorScreenPos();
	ImGui::Dummy({Extent(), ImGui::GetTextLineHeight()});
	RowImage(icon, position, 0);
	ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
}

void Text(Icon icon, const char* label)
{
	if (Available())
	{
		Inline(icon);
	}
	ImGui::TextUnformatted(label);
}

void Section(Icon icon, const char* label)
{
	ImGui::Separator();
	ImGui::AlignTextToFramePadding();
	Text(icon, label);
}

void SameLine(Float32 requiredWidth)
{
	const auto* window = ImGui::GetCurrentWindow();
	if (window->DC.CursorPosPrevLine.x + ImGui::GetStyle().ItemSpacing.x + requiredWidth < window->WorkRect.Max.x)
	{
		ImGui::SameLine();
	}
}

namespace
{
bool ActionButton(const char* id, Icon icon, const char* label, bool selected, bool showText, ImVec2 requested)
{
	const bool image = Available();
	const bool textVisible = showText || !image;
	const auto& style = ImGui::GetStyle();
	const Float32 height = requested.y > 0.f ? requested.y : std::max(28.f, ImGui::GetFrameHeight());
	const Float32 iconWidth = image && textVisible ? Extent() + style.ItemInnerSpacing.x : 0.f;
	const Float32 natural =
	    textVisible ? ImGui::CalcTextSize(label, nullptr, true).x + iconWidth + style.FramePadding.x * 2.f : height;
	const Float32 width = requested.x > 0.f ? requested.x : natural;
	if (ImGui::GetCursorPosX() > style.WindowPadding.x && ImGui::GetContentRegionAvail().x < width)
	{
		ImGui::NewLine();
	}
	const auto origin = ImGui::GetCursorScreenPos();
	if (selected)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	}
	// Empty-label Button hashes to its override seed, preserving the caller's existing native command ID.
	ImGui::PushOverrideID(ImGui::GetID(id));
	const bool clicked = ImGui::Button("", {width, height});
	ImGui::PopID();
	if (selected)
	{
		ImGui::PopStyleColor();
	}
	if (image)
	{
		const Float32 extent = textVisible ? Extent() : std::max(18.f, ImGui::GetFontSize());
		Draw(icon,
		     {origin.x + (textVisible ? style.FramePadding.x : (width - extent) * 0.5f),
		      origin.y + (height - extent) * 0.5f},
		     extent, Color(icon));
	}
	if (textVisible)
	{
		ImGui::RenderTextClipped({origin.x + style.FramePadding.x + iconWidth, origin.y},
		                         {origin.x + width - style.FramePadding.x, origin.y + height}, label,
		                         ImGui::FindRenderedTextEnd(label), nullptr, {0.f, 0.5f});
	}
	Hint(label);
	return clicked;
}
} // namespace

bool Button(const char* id, Icon icon, const char* label, bool selected, bool showText)
{
	return ActionButton(id, icon, label, selected, showText, {});
}

bool Button(const char* label, const ImVec2& size)
{
	return ActionButton(label, ForCommand(label), label, false, true, size);
}

bool SmallButton(const char* label)
{
	return Button(label);
}

bool Selectable(Icon icon, const char* label, bool selected, ImGuiSelectableFlags flags, ImVec2 size)
{
	if (!Available())
	{
		return ImGui::Selectable(label, selected, flags, size);
	}
	const auto origin = ImGui::GetCursorScreenPos();
	ImGui::PushOverrideID(ImGui::GetID(label));
	const bool clicked = ImGui::Selectable("", selected, flags, size);
	ImGui::PopID();
	const Float32 rowHeight = size.y > 0.f ? size.y : ImGui::GetTextLineHeight();
	Draw(icon, {origin.x, origin.y + (rowHeight - Extent()) * 0.5f}, Extent(), Color(icon));
	const ImVec2 position{origin.x + Extent() + ImGui::GetStyle().ItemInnerSpacing.x,
	                      origin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f};
	const auto maximum = ImGui::GetItemRectMax();
	ImGui::RenderTextClipped(position, maximum, label, ImGui::FindRenderedTextEnd(label), nullptr);
	return clicked;
}

bool TreeNode(const char* id, ImGuiTreeNodeFlags flags, Icon icon, const char* label)
{
	const auto origin = ImGui::GetCursorScreenPos();
	const String padded = Available() ? Padded(label) : String(label);
	const bool open = ImGui::TreeNodeEx(id, flags, "%s", padded.c_str());
	if (Available())
	{
		RowImage(icon, origin, ImGui::GetTreeNodeToLabelSpacing());
	}
	return open;
}

bool MenuItem(const char* label, const char* shortcut, bool selected, bool enabled)
{
	const auto origin = ImGui::GetCursorScreenPos();
	const String iconColumn = Available() ? Padded("") : String{};
	const bool clicked = ImGui::MenuItemEx(label, iconColumn.c_str(), shortcut, selected, enabled);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
	                    ImGui::GetStyle().Alpha * (enabled ? 1.f : ImGui::GetStyle().DisabledAlpha));
	if (Available())
	{
		RowImage(ForCommand(label), origin, 0);
	}
	ImGui::PopStyleVar();
	return clicked;
}

bool MenuItem(const char* label, const char* shortcut, bool* selected, bool enabled)
{
	if (MenuItem(label, shortcut, *selected, enabled))
	{
		*selected = !*selected;
		return true;
	}
	return false;
}

bool BeginMenu(const char* label, bool enabled)
{
	if (!Available() || ImGui::GetCurrentWindow()->DC.LayoutType == ImGuiLayoutType_Horizontal)
	{
		return ImGui::BeginMenu(label, enabled);
	}
	const auto origin = ImGui::GetCursorScreenPos();
	auto* parentDrawList = ImGui::GetWindowDrawList();
	const auto icon = ForCommand(label);
	auto color = Color(icon);
	color.w *= enabled ? 1.f : ImGui::GetStyle().DisabledAlpha;
	const Float32 extent = Extent();
	const Float32 offset = (ImGui::GetTextLineHeight() - extent) * 0.5f;
	const bool open = ImGui::BeginMenuEx(label, Padded("").c_str(), enabled);
	Paint(parentDrawList, icon, {origin.x, origin.y + offset}, extent, color);
	return open;
}

bool ComponentMenu()
{
	if (Button("componentActions", Icon::More, "Component actions"))
	{
		ImGui::OpenPopup("ComponentActions");
	}
	bool remove = false;
	if (ImGui::BeginPopup("ComponentActions"))
	{
		remove = MenuItem("Remove component");
		ImGui::EndPopup();
	}
	return remove;
}

void SetPanelResetAction(std::function<void()> action)
{
	panelResetAction = std::move(action);
}

bool DrawPanelContextMenu(const char* popupId, bool* open, ImGuiID* closeItemId)
{
	if (!popupId || !open || !ImGui::BeginPopupContextWindow(popupId, ImGuiPopupFlags_NoOpenOverItems))
	{
		return false;
	}
	const bool close = ImGui::MenuItem("Close Panel");
	if (closeItemId)
	{
		*closeItemId = ImGui::GetItemID();
	}
	const bool reset = ImGui::MenuItem("Reset Layout", nullptr, false, static_cast<bool>(panelResetAction));
	ImGui::EndPopup();
	if (reset && panelResetAction)
	{
		panelResetAction();
	}
	if (close)
	{
		*open = false;
	}
	return close;
}
} // namespace Ursa::Editor::Icons
