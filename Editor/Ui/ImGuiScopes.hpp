#pragma once

#include <imgui.h>

namespace Ursa::Editor::Ui
{
class DisabledScope final
{
public:
	explicit DisabledScope(bool disabled)
	{
		ImGui::BeginDisabled(disabled);
	}

	~DisabledScope()
	{
		ImGui::EndDisabled();
	}

	DisabledScope(const DisabledScope&) = delete;
	DisabledScope& operator=(const DisabledScope&) = delete;
};
} // namespace Ursa::Editor::Ui
