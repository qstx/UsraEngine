#pragma once

#include <Core/Types.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace Ursa::Editor::Bindings
{
// Dear ImGui reports a numeric edit only when the representative value changes.
// A mixed field also needs to recognize explicitly entering that same value.
inline bool ExplicitMixedFloatInput(ImGuiID id, Float32 value)
{
	const ImGuiInputTextState* input = ImGui::GetInputTextState(id);
	if (!input || !input->EditedBefore || !input->TextA.Data)
	{
		return false;
	}
	char* end = nullptr;
	const Float32 entered = std::strtof(input->TextA.Data, &end);
	while (end && std::isspace(static_cast<unsigned char>(*end)))
	{
		++end;
	}
	return end && end != input->TextA.Data && *end == '\0' && std::isfinite(entered) && entered == value;
}

inline bool MixedFloatInputCommitted(bool explicitSameValue)
{
	return explicitSameValue && ImGui::IsItemDeactivated() && !ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}
} // namespace Ursa::Editor::Bindings
