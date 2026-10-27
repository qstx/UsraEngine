#include <Core/StringInterop.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Ui/EditorIcons.hpp>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;
constexpr ImTextureID iconTexture = 42;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

struct Context
{
	ImGuiContext* value = ImGui::CreateContext();
	std::vector<std::string> errors;

	Context()
	{
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = {1000, 900};
		io.DeltaTime = 1.f / 60.f;
		io.ConfigErrorRecoveryEnableAssert = false;
		io.ConfigErrorRecoveryEnableTooltip = false;
		io.ConfigErrorRecoveryEnableDebugLog = false;
		value->ErrorCallback = [](ImGuiContext*, void* user, const char* message)
		{ static_cast<Context*>(user)->errors.emplace_back(message); };
		value->ErrorCallbackUserData = this;
		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		io.Fonts->SetTexID(1);
		SetIcons();
	}

	~Context()
	{
		Icons::ClearAtlas(value);
		ImGui::DestroyContext(value);
	}

	void SetIcons()
	{
		nlohmann::json manifest = {{"version", 1}, {"width", 64}, {"height", 64}};
		const int count = static_cast<int>(Icons::Icon::Count);
		for (int index = 0; index < count; ++index)
		{
			manifest["icons"][Icons::Name(static_cast<Icons::Icon>(index))] = {
			    {"uv0", {0.f, static_cast<float>(index) / count}},
			    {"uv1", {1.f, static_cast<float>(index + 1) / count}}};
		}
		Done(Icons::SetAtlas(value, iconTexture, ToUrsaString(manifest.dump())));
	}

	void CheckErrors() const
	{
		if (!errors.empty())
		{
			throw std::runtime_error("ImGui layout error: " + errors.front());
		}
	}
};

std::vector<ImRect> IconRects(const ImDrawList& list, Icons::Icon icon)
{
	std::vector<ImRect> result;
	const float count = static_cast<float>(Icons::Icon::Count);
	const float minimumV = static_cast<float>(icon) / count;
	const float maximumV = (static_cast<float>(icon) + 1.f) / count;
	for (const auto& command : list.CmdBuffer)
	{
		if (command.TexRef._TexData != nullptr || command.GetTexID() != iconTexture)
		{
			continue;
		}
		for (unsigned int offset = command.IdxOffset; offset + 5 < command.IdxOffset + command.ElemCount; offset += 6)
		{
			ImRect bounds;
			bounds.Min = {FLT_MAX, FLT_MAX};
			bounds.Max = {-FLT_MAX, -FLT_MAX};
			float minimum = FLT_MAX;
			float maximum = -FLT_MAX;
			for (unsigned int triangle = 0; triangle < 6; ++triangle)
			{
				const auto& vertex = list.VtxBuffer[command.VtxOffset + list.IdxBuffer[offset + triangle]];
				bounds.Add(vertex.pos);
				minimum = std::min(minimum, vertex.uv.y);
				maximum = std::max(maximum, vertex.uv.y);
			}
			if (std::abs(minimum - minimumV) < 0.0001f && std::abs(maximum - maximumV) < 0.0001f)
			{
				result.push_back(bounds);
			}
		}
	}
	return result;
}

void TestIconAlignment()
{
	Context context;
	for (const float fontSize : {13.f, 16.f, 24.f, 32.f})
	{
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({0, 0});
		ImGui::SetNextWindowSize({600, 400});
		ImGui::Begin("Icon alignment");
		ImGui::PushFont(nullptr, fontSize);
		Icons::TreeNode("##node", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen,
		                Icons::Icon::GameObject, "Object", 16.f);
		const ImRect row(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		const auto icons = IconRects(*ImGui::GetWindowDrawList(), Icons::Icon::GameObject);
		Expect(icons.size() == 1, "The tree row did not draw exactly one object icon");
		Expect(std::abs(icons.front().GetCenter().y - row.GetCenter().y) < 0.1f,
		       "The icon is not vertically centered in the actual tree row");
		Expect(row.GetHeight() >= std::max(16.f, fontSize), "The tree row does not contain its icon and font");
		ImGui::PopFont();
		ImGui::End();
		ImGui::Render();
		context.CheckErrors();
	}
	Icons::ClearAtlas(context.value);
	ImGui::NewFrame();
	ImGui::Begin("Fallback");
	Icons::TreeNode("##node", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen, Icons::Icon::GameObject,
	                "Fallback object", 16.f);
	Expect(ImGui::GetItemRectSize().y >= 16.f, "Fallback tree row lost its minimum height");
	ImGui::End();
	ImGui::Render();
	context.CheckErrors();
}

void TestSceneRows()
{
	Context context;
	Scene scene;
	const auto parent = Take(scene.CreateGameObject("Parent"));
	const auto child = Take(scene.CreateGameObject("Child"));
	const auto last = Take(scene.CreateGameObject("Last"));
	Done(scene.SetParent(child, parent));
	const auto document = ResourceId::Generate();
	const auto parentId = Take(scene.PersistentId(parent));
	const auto lastId = Take(scene.PersistentId(last));
	EditorHierarchy hierarchy;
	EditorSelectionContext selection;
	selection.SelectGameObject(document, parentId);
	bool open = true;
	const auto frame = [&]
	{
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({0, 0});
		ImGui::SetNextWindowSize({520, 600});
		const bool changed =
		    Take(hierarchy.Draw(scene, &open, nullptr, &selection, document, false, 0, {}, {}, {}, "Demo.ursascene"));
		Expect(!changed, "Viewing the hierarchy or toggling view controls changed authoring data");
		ImGui::Render();
	};
	frame();
	auto* window = ImGui::FindWindowByName("Hierarchy");
	const auto sceneIcons = IconRects(*window->DrawList, Icons::Icon::Scene);
	const auto roots = IconRects(*window->DrawList, Icons::Icon::GameObject);
	Expect(sceneIcons.size() == 1 && roots.size() == 2, "Scene and root rows were not initially visible");
	Expect(roots.front().Min.x >= sceneIcons.front().Min.x + 13.f,
	       "Scene roots were drawn beside the scene header instead of below it with one indent");
	Expect(hierarchy.VisibleSelectionRows().size() == 2, "The visible selection rows disagree with the scene rows");
	context.CheckErrors();
	const String groupKey = document.ToString();
	const ImGuiID groupSeed = ImHashStr(groupKey.c_str(), 0, window->ID);
	const ImGuiID headerId = ImHashStr("##sceneGroup", 0, groupSeed);
	window->StateStorage.SetInt(headerId, 0);
	frame();
	Expect(hierarchy.VisibleSelectionRows().empty() && IconRects(*window->DrawList, Icons::Icon::GameObject).empty(),
	       "A collapsed scene still exposed object or keyboard selection rows");
	window->StateStorage.SetInt(headerId, 1);
	frame();
	Expect(IconRects(*window->DrawList, Icons::Icon::GameObject).size() == 2, "The scene did not reopen its roots");
	const String parentKey = parentId.ToString();
	const ImGuiID parentSeed = ImHashStr(parentKey.c_str(), 0, window->ID);
	window->StateStorage.SetInt(ImHashStr("##node", 0, parentSeed), 1);
	frame();
	Expect(hierarchy.VisibleSelectionRows().size() == 3, "Expanding a root did not reveal its child");
	window->StateStorage.SetInt(headerId, 0);
	frame();
	window->StateStorage.SetInt(headerId, 1);
	frame();
	Expect(hierarchy.VisibleSelectionRows().size() == 3, "Scene reopen changed the object's expansion identity");
	const auto objects = IconRects(*window->DrawList, Icons::Icon::GameObject);
	const float lastY = objects.back().GetCenter().y;
	auto& io = ImGui::GetIO();
	const auto click = [&](float x)
	{
		io.AddMousePosEvent(x, lastY);
		frame();
		frame();
		context.CheckErrors();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		frame();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		frame();
		context.CheckErrors();
	};
	click(window->WorkRect.Max.x - 9.f);
	Expect(!hierarchy.SceneObjectPickable(lastId), "The last row's pickability control did not respond");
	click(window->WorkRect.Max.x - 26.f);
	Expect(!hierarchy.SceneObjectVisible(lastId), "The last row's visibility control did not respond");
	Expect(selection.objectId == parentId && Take(scene.ActiveSelf(last)),
	       "A row overlay changed object selection or Active state");
	click(objects.back().GetCenter().x + 60.f);
	Expect(selection.objectId == lastId, "A regular hierarchy row click stopped selecting its object");
	window->StateStorage.SetInt(headerId, 0);
	std::strcpy(hierarchy.SearchText().data(), "Child");
	frame();
	Expect(hierarchy.VisibleSelectionRows().size() == 2 &&
	           IconRects(*window->DrawList, Icons::Icon::GameObject).size() == 2,
	       "Search did not consistently reopen the scene and show its matching branch");
	hierarchy.SearchText().fill(0);
	frame();
	context.CheckErrors();
	Scene empty;
	ImGui::NewFrame();
	Take(hierarchy.Draw(empty, &open, nullptr, nullptr, ResourceId::Generate(), false, 0, {}, {}, {},
	                    "Empty.ursascene"));
	ImGui::Render();
	context.CheckErrors();
}

void TestLastRowLayout()
{
	Context context;
	Scene scene;
	Take(scene.CreateGameObject("Last"));
	EditorHierarchy hierarchy;
	const auto document = ResourceId::Generate();
	const auto frame = [&]
	{
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({0, 0});
		ImGui::SetNextWindowSize({520, 400});
		Take(hierarchy.Draw(scene, nullptr, nullptr, nullptr, document, false, 0, {}, {}, {}, "LastRow"));
		ImGui::Render();
	};
	frame();
	const auto icons = IconRects(*ImGui::FindWindowByName("Hierarchy")->DrawList, Icons::Icon::GameObject);
	Expect(icons.size() == 1, "The last-row regression did not draw its object");
	const auto center = icons.front().GetCenter();
	ImGui::GetIO().AddMousePosEvent(center.x + 60.f, center.y);
	frame();
	frame();
	context.CheckErrors();
}
} // namespace

int main(int argc, const char* const* argv)
{
	try
	{
		const std::string mode = argc > 1 ? argv[1] : "all";
		if (mode == "all" || mode == "--scene")
		{
			TestSceneRows();
		}
		if (mode == "all" || mode == "--icons")
		{
			TestIconAlignment();
		}
		if (mode == "all" || mode == "--overlay")
		{
			TestLastRowLayout();
		}
		std::cout << "Hierarchy scene grouping, stable expansion, overlays and scaled icon alignment passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
