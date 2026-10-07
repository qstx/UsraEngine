#include <Ui/ImGuiInspectorUI.hpp>
#include <imgui.h>

namespace Ursa::Editor
{
Result<bool> ImGuiInspectorUI::Checkbox(const String& label, bool& value)
{
	return Result<bool>::Success(ImGui::Checkbox(label.c_str(), &value));
}

Result<bool> ImGuiInspectorUI::Float(const String& label, Float32& value, Float32 minimum, Float32 maximum,
                                     Float32 speed)
{
	return Result<bool>::Success(ImGui::DragFloat(label.c_str(), &value, speed, minimum, maximum));
}

Result<bool> ImGuiInspectorUI::Color(const String& label, Ursa::Color& value)
{
	Float32 channels[4]{value.red, value.green, value.blue, value.alpha};
	if (!ImGui::ColorEdit4(label.c_str(), channels))
	{
		return Result<bool>::Success(false);
	}
	value = {channels[0], channels[1], channels[2], channels[3]};
	return Result<bool>::Success(true);
}

Result<bool> ImGuiInspectorUI::Select(const String& label, const Vector<InspectorOption>& options, ResourceId& selected)
{
	String current = "None";
	for (const auto& option : options)
	{
		if (option.id == selected)
		{
			current = option.label;
			break;
		}
	}
	if (!ImGui::BeginCombo(label.c_str(), current.c_str()))
	{
		return Result<bool>::Success(false);
	}
	bool changed = false;
	for (const auto& option : options)
	{
		if (ImGui::Selectable(option.label.c_str(), option.id == selected))
		{
			selected = option.id;
			changed = true;
		}
	}
	ImGui::EndCombo();
	return Result<bool>::Success(changed);
}

void ImGuiInspectorUI::Text(const String& message)
{
	ImGui::TextDisabled("%s", message.c_str());
}
} // namespace Ursa::Editor
