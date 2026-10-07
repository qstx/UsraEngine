#include <Assets/MaterialInspectors.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

class MockInspectorUI final : public InspectorUI
{
public:
	Result<bool> Checkbox(const String&, bool&) override
	{
		return Result<bool>::Success(false);
	}

	Result<bool> Float(const String&, Float32&, Float32, Float32, Float32) override
	{
		return Result<bool>::Success(false);
	}

	Result<bool> Color(const String&, Ursa::Color&) override
	{
		return Result<bool>::Success(false);
	}

	Result<bool> Select(const String&, const Vector<InspectorOption>&, ResourceId&) override
	{
		return Result<bool>::Success(false);
	}

	void Text(const String& value) override
	{
		lastText = value;
	}

	String lastText;
};

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		VirtualFileSystem filesystem;
		auto resources = ResourceSession::Open(filesystem);
		Expect(static_cast<bool>(resources), "Cannot create mock resource session");
		auto parsedPath = VirtualPath::Parse("prj:Materials/Custom.ursamat");
		Expect(static_cast<bool>(parsedPath), "Cannot create mock material path");
		auto document = EditorDocument::Create(ResourceKind::Material, ResourceId::Generate(), parsedPath.value,
		                                       [](VirtualFileSystem&, const VirtualPath&, ResourceId) -> Result<void>
		                                       { return {}; });
		Expect(static_cast<bool>(document), "Cannot create material Inspector document");
		auto draft = std::make_shared<MaterialDraft>(MaterialType::Unlit);
		Shader::ShaderAsset shader;
		shader.id = ResourceId::Generate();
		shader.name = "Tests/CustomInspector";
		shader.customEditor = "Tests/CustomMaterialInspector";
		Shader::PropertyDeclaration strength;
		strength.identifier = "_Strength";
		strength.displayName = "Strength";
		strength.type = Shader::PropertyType::Float;
		strength.defaultFloat = 0.25f;
		shader.properties.push_back(strength);
		Expect(static_cast<bool>(draft->Material().SetShaderAsset(std::make_shared<Shader::ShaderAsset>(shader))),
		       "Cannot bind mock Shader asset to material draft");
		document.value->SetPayload(draft);
		MaterialInspectorContext context{*document.value, *draft, *resources.value};
		MaterialInspectors registry;
		MockInspectorUI ui;
		bool called = false;
		auto registered =
		    registry.Register(shader.customEditor,
		                      [&](MaterialInspectorContext& selected, InspectorUI& selectedUi) -> Result<bool>
		                      {
			                      called = true;
			                      selectedUi.Text("Native mock custom Inspector");
			                      auto edited =
			                          selected.draft.SetPropertyValue("_Strength", MaterialPropertyValue::Float(0.75f));
			                      if (!edited)
			                      {
				                      return Result<bool>::Failure(edited.error);
			                      }
			                      selected.document.MarkDirty();
			                      return Result<bool>::Success(true);
		                      });
		Expect(static_cast<bool>(registered), "Native Material Inspector registration failed");
		auto duplicate = registry.Register(shader.customEditor, [](MaterialInspectorContext&, InspectorUI&)
		                                   { return Result<bool>::Success(false); });
		Expect(!duplicate, "Duplicate Material Inspector registration was accepted");
		auto drawn = registry.Draw(shader.customEditor, context, ui);
		Expect(drawn && drawn.value && called && ui.lastText == "Native mock custom Inspector" &&
		           document.value->Dirty(),
		       "Registered Material Inspector was not called or did not dirty the document");
		auto missing = registry.Draw("Tests/MissingInspector", context, ui);
		Expect(!missing && missing.error.find("not registered") != String::npos,
		       "Missing custom Inspector did not report an error for the generic fallback path");
		auto genericFallback =
		    SetShaderMaterialProperty(*document.value, "_Strength", MaterialPropertyValue::Float(0.5f));
		auto strengthValue = draft->Material().PropertyValue("_Strength", shader);
		Expect(genericFallback && strengthValue && strengthValue.value.floatValue == 0.5f,
		       "Generic property editing was unavailable after a missing custom Inspector");
		std::cout << "Editor Material Inspector tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
