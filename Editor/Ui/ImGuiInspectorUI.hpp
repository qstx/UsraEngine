#pragma once

#include <Scene/ComponentEditors.hpp>

namespace Ursa::Editor
{
class ImGuiInspectorUI final : public InspectorUI
{
public:
	Result<bool> Checkbox(const String& label, bool& value) override;

	Result<bool> Float(const String& label, Float32& value, Float32 minimum, Float32 maximum, Float32 speed) override;

	Result<bool> Color(const String& label, Ursa::Color& value) override;

	Result<bool> Select(const String& label, const Vector<InspectorOption>& options, ResourceId& selected) override;

	void Text(const String& message) override;
};
} // namespace Ursa::Editor
