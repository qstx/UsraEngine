#include <Assets/MaterialInspectors.hpp>
#include <Core/StringInterop.hpp>

#include <stdexcept>

int main()
{
	Ursa::Editor::MaterialInspectors inspectors;
	auto registered = inspectors.Register(
	    "Example/MaterialInspector",
	    [](Ursa::Editor::MaterialInspectorContext& context, Ursa::Editor::InspectorUI& controls) -> Ursa::Result<bool>
	    {
		    const auto shader = context.draft.Material().Shader();
		    if (!shader)
		    {
			    return Ursa::Result<bool>::Failure("Material Shader is not loaded");
		    }
		    auto current = context.draft.Material().PropertyValue("_Roughness", *shader);
		    if (!current)
		    {
			    return Ursa::Result<bool>::Failure(current.error);
		    }
		    Ursa::Float32 roughness = current.value.floatValue;
		    auto changed = controls.Float("Roughness", roughness, 0.0f, 1.0f, 0.01f);
		    if (!changed)
		    {
			    return Ursa::Result<bool>::Failure(changed.error);
		    }
		    if (!changed.value)
		    {
			    return Ursa::Result<bool>::Success(false);
		    }
		    auto set = Ursa::Editor::SetShaderMaterialProperty(context.document, "_Roughness",
		                                                       Ursa::MaterialPropertyValue::Range(roughness));
		    return set ? Ursa::Result<bool>::Success(true) : Ursa::Result<bool>::Failure(set.error);
	    });
	if (!registered)
	{
		throw std::runtime_error(Ursa::ToStdString(registered.error));
	}
	return inspectors.Find("Example/MaterialInspector") ? 0 : 1;
}
