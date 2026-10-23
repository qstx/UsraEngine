#include <Core/StringInterop.hpp>
#include <Rendering/Pipeline/NativeForwardPipeline.hpp>
#include <Scene/RenderingComponents.hpp>
#include <MeshFixtures.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Shader/ShaderSourceLibrary.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/DirectoryFileProvider.hpp>

#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Rhi;

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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

const Ursa::Shader::PropertyDeclaration* FindProperty(const Ursa::Vector<Ursa::Shader::PropertyDeclaration>& properties,
                                                      const Ursa::String& identifier)
{
	for (const auto& property : properties)
	{
		if (property.identifier == identifier)
		{
			return &property;
		}
	}
	return nullptr;
}

void ExpectImportedDefaultsMatchSource(const Ursa::Shader::ShaderSourceDocument& source,
                                       const Ursa::Shader::ShaderAsset& imported)
{
	Expect(source.id == imported.id && source.name == imported.name &&
	           source.properties.size() == imported.properties.size(),
	       "Built-in Shader import identity or property count differs from its source metadata");
	Ursa::MaterialInstance material(Ursa::MaterialType::Unlit);
	Done(material.SetShader(imported.id));
	for (const auto& declaration : source.properties)
	{
		const auto* importedDeclaration = FindProperty(imported.properties, declaration.identifier);
		Expect(importedDeclaration != nullptr && importedDeclaration->type == declaration.type &&
		           importedDeclaration->displayName == declaration.displayName &&
		           importedDeclaration->rangeMinimum == declaration.rangeMinimum &&
		           importedDeclaration->rangeMaximum == declaration.rangeMaximum &&
		           importedDeclaration->defaultFloat == declaration.defaultFloat &&
		           importedDeclaration->defaultInt == declaration.defaultInt &&
		           importedDeclaration->defaultVector == declaration.defaultVector &&
		           importedDeclaration->defaultTexture == declaration.defaultTexture &&
		           importedDeclaration->textureOptions == declaration.textureOptions &&
		           importedDeclaration->attributes == declaration.attributes,
		       "Imported built-in Shader property metadata differs from its source declaration");
		auto value = material.PropertyValue(declaration.identifier, imported);
		Expect(static_cast<bool>(value), "Built-in material defaults could not be read without a rendering device");
		switch (declaration.type)
		{
			case Ursa::Shader::PropertyType::Float:
			case Ursa::Shader::PropertyType::Range:
				Expect(value.value.floatValue == declaration.defaultFloat,
				       "Built-in material scalar default differs from Shader source");
				break;
			case Ursa::Shader::PropertyType::Int:
				Expect(value.value.intValue == declaration.defaultInt,
				       "Built-in material integer default differs from Shader source");
				break;
			case Ursa::Shader::PropertyType::Color:
			case Ursa::Shader::PropertyType::Vector:
				Expect(value.value.vectorValue == declaration.defaultVector,
				       "Built-in material vector default differs from Shader source");
				break;
			case Ursa::Shader::PropertyType::Texture2D:
				Expect(value.value.textureDefault == declaration.defaultTexture,
				       "Built-in material texture default differs from Shader source");
				break;
		}
	}
}
} // namespace

int main()
{
	try
	{
		auto filesystem = Take(OpenEngineFileSystem(std::filesystem::u8path(URSA_ENGINE_SHADER_PAK)));
		auto sourcePath = Take(VirtualPath::Parse("ursa:Shaders/Universal/Unlit.ursashader"));
		Expect(!filesystem.CheckWritable(sourcePath), "Shader source inside a pak was writable");
		VirtualFileSystem sourceFilesystem;
		Done(sourceFilesystem.MountProvider(
		    "src", Take(DirectoryFileProvider::Open(std::filesystem::u8path(URSA_ENGINE_ASSET_SOURCE))),
		    MountAccess::ReadOnly));
		auto sourceLibrary = Take(Ursa::Shader::ShaderSourceLibrary::Load(sourceFilesystem, "src"));
		const Ursa::Array<Ursa::ResourceId, 3> builtinIds{Ursa::Shader::BuiltinUnlitShaderId(),
		                                                  Ursa::Shader::BuiltinPbrShaderId(),
		                                                  Ursa::Shader::BuiltinBlitShaderId()};
		for (const Ursa::ResourceId id : builtinIds)
		{
			const char* assetPathText = Ursa::Shader::BuiltinShaderAssetPath(id);
			Expect(assetPathText != nullptr, "Built-in Shader does not have an imported asset path");
			const auto assetPath = Take(VirtualPath::Parse(assetPathText));
			const Ursa::String sourcePathText = assetPath.ToString().substr(0, assetPath.ToString().size() - 5);
			const auto expectedSourcePath = Take(VirtualPath::Parse(sourcePathText));
			const Ursa::Shader::ShaderSourceAsset* matchingSource = nullptr;
			Ursa::Size matchingSources = 0;
			for (const auto* candidate : sourceLibrary.Sources())
			{
				if (candidate->source.id == id)
				{
					matchingSource = candidate;
					++matchingSources;
				}
				Expect(candidate->path.ToString().find("Shaders/Renderer3D/") == String::npos,
				       "Legacy Renderer3D Shader source was packaged for runtime loading");
			}
			Expect(matchingSources == 1 && matchingSource->path.relative == expectedSourcePath.relative,
			       "Built-in Shader GUID does not resolve to exactly one source at its imported path");
		}
		auto resources = Take(ResourceSession::Open(filesystem));
		auto shader = Take(resources->LoadShader(Ursa::Shader::BuiltinUnlitShaderId()));
		auto litShader = Take(resources->LoadShader(Ursa::Shader::BuiltinPbrShaderId()));
		auto blitShader = Take(resources->LoadShader(Ursa::Shader::BuiltinBlitShaderId()));
		const auto errorShaderId = Take(ResourceId::Parse("c6f1e600-b5c6-4c56-a5e1-5bba91570112"));
		auto errorShader = Take(resources->LoadShader(errorShaderId));
		const auto* unlitSource = sourceLibrary.Find("Ursa/Universal/Unlit");
		const auto* litSource = sourceLibrary.Find("Ursa/Universal/Lit");
		Expect(unlitSource != nullptr && litSource != nullptr,
		       "Imported built-in Shader sources were not indexed by their declared names");
		ExpectImportedDefaultsMatchSource(unlitSource->source, *shader);
		ExpectImportedDefaultsMatchSource(litSource->source, *litShader);
		Expect(blitShader->name == "Ursa/Internal/Blit" && !blitShader->subShaders.empty() &&
		           !blitShader->subShaders.front().passes.empty() &&
		           !blitShader->subShaders.front().passes.front().variants.empty() &&
		           !errorShader->subShaders.empty() && !errorShader->subShaders.front().passes.empty() &&
		           !errorShader->subShaders.front().passes.front().variants.empty() &&
		           !errorShader->subShaders.front().passes.front().variants.front().stages.empty(),
		       "Built-in Blit or Error Shader did not import as a valid program");
		for (const char* legacyPath : {"ursa:Shaders/Renderer3D/Unlit.ursashader",
		                               "ursa:Shaders/Renderer3D/Pbr.ursashader", "ursa:Shaders/Renderer3D/Pbr.frag"})
		{
			Expect(
			    !Take(sourceFilesystem.Exists(Take(VirtualPath::Parse(String("src:") + String(legacyPath).substr(5))))),
			    "Legacy built-in Shader source path is still present in the engine asset tree");
		}
		for (const char* legacyAssetPath :
		     {"ursa:Shaders/Renderer3D/Unlit.ursashaderasset", "ursa:Shaders/Renderer3D/Pbr.ursashaderasset"})
		{
			Expect(!Take(filesystem.Exists(Take(VirtualPath::Parse(legacyAssetPath)))),
			       "Legacy built-in Shader artifact path is still present in the runtime package");
		}
		Expect(shader->customEditor.empty() && !shader->subShaders.empty() &&
		           !shader->subShaders[0].passes[0].variants.empty() &&
		           !shader->subShaders[0].passes[0].variants[0].stages[0].spirv.empty(),
		       "Compiled Shader variant did not load from the pak resource index");
		ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(filesystem));
		DeviceDesc description;
#ifndef NDEBUG
		description.validation = true;
#endif
		auto device = Take(CreateDevice(description));
		auto renderer = Take(Rendering::NativeForwardPipeline::Create(device));
		auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		Done(material->SetShaderAsset(shader));
		Done(material->SetPropertyValue("_BaseColor", *shader,
		                                MaterialPropertyValue::ColorValue({0.1f, 1.0f, 0.2f, 1.0f})));
		Scene scene;
		auto cameraObject = Take(scene.CreateGameObject("Pak Camera"));
		LocalTransform cameraTransform;
		cameraTransform.position = {0, 0, 4};
		Done(scene.SetLocalTransform(cameraObject, cameraTransform));
		auto* camera = Take(cameraObject->AddComponent<CameraComponent>());
		Done(scene.SetActiveCamera(camera->Id()));
		auto object = Take(scene.CreateGameObject("Pak Unlit Geometry"));
		Take(Test::AddRenderer(scene, object));
		Done(object->FindComponent<MeshRendererComponent>()->SetMaterial(material));
		auto color = Take(Rendering::CreateRenderTexture(*device, {64, 64, 0, 32}));
		Done(renderer->RenderOffscreen(scene, color, ResourceState::TransferSource));
		const auto pixels = Take(Rendering::ReadRenderTexture(*device, *color));
		const Size center = (32 * 64 + 32) * 4;
		Expect(pixels[center] > 60 && pixels[center + 1] > 180 && pixels[center + 2] > 100,
		       "Pak-backed compiled Shader did not render without invoking the authoring compiler");
		Done(device->WaitIdle());
		ConfigureEngineFileSystem({});
		std::cout << "Pak Shader runtime tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		ConfigureEngineFileSystem({});
		std::cerr << error.what() << '\n';
		return 1;
	}
}
