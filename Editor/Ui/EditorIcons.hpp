#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Scene/GameObject.hpp>
#include <imgui.h>
#include <optional>

namespace Ursa::Editor::Icons
{
enum class Icon
{
	Play,
	Stop,
	Pause,
	Step,
	Move,
	Rotate,
	Scale,
	Fit,
	Folder,
	File,
	Scene,
	Prefab,
	Material,
	Texture,
	Shader,
	Mesh,
	Model,
	Script,
	ScriptableObject,
	GameObject,
	Camera,
	Light,
	PointLight,
	Renderer,
	SkinnedMesh,
	Component,
	Add,
	Remove,
	More,
	Refresh,
	Search,
	Rename,
	Save,
	Delete,
	Close,
	Check,
	Revert,
	Up,
	Down,
	Locate,
	Trace,
	Debug,
	Info,
	Warning,
	Error,
	Import,
	Build,
	Settings,
	Project,
	Hierarchy,
	Inspector,
	Console,
	Link,
	Copy,
	Pin,
	Favorite,
	Count
};

const char* Name(Icon icon);
Result<void> SetAtlas(ImGuiContext* context, ImTextureID texture, const String& manifest);
void ClearAtlas(ImGuiContext* context);
bool Available();
ImVec4 Color(Icon icon);
Icon ForCommand(const String& label);
Icon ForAsset(bool folder, std::optional<ResourceKind> kind, const String& importer, const String& path);
Icon ForComponent(const String& type, bool script = false);
Icon ForObject(const GameObject::Ptr& object, bool prefab = false);

void Draw(Icon icon, ImVec2 position, Float32 size, ImVec4 color);
void Inline(Icon icon);
void Text(Icon icon, const char* label);
void Section(Icon icon, const char* label);
void SameLine(Float32 requiredWidth = 36.f);
bool Button(const char* id, Icon icon, const char* label, bool selected = false, bool showText = false);
bool Button(const char* label, const ImVec2& size = {});
bool SmallButton(const char* label);
bool Selectable(Icon icon, const char* label, bool selected = false, ImGuiSelectableFlags flags = 0,
                ImVec2 size = {});
bool TreeNode(const char* id, ImGuiTreeNodeFlags flags, Icon icon, const char* label, Float32 rowHeight = 0.f);
bool MenuItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);
bool MenuItem(const char* label, const char* shortcut, bool* selected, bool enabled = true);
bool BeginMenu(const char* label, bool enabled = true);
bool ComponentMenu();
void SetPanelResetAction(std::function<void()> action);
bool DrawPanelContextMenu(const char* popupId, bool* open, ImGuiID* closeItemId = nullptr);
} // namespace Ursa::Editor::Icons
