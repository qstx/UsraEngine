#include <Assets/MaterialDraft.hpp>
#include <Serialization/TextureResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
}

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
		VirtualFileSystem files;
		Done(files.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const auto textureGuid = ResourceId::Generate();
		const auto texture = Take(MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {255, 0, 0, 255}));
		Done(SaveTextureResource(files, Take(VirtualPath::Parse("prj:Texture.ursatex")), textureGuid, *texture));
		auto session =
		    Take(ResourceSession::Open(files,
		                               [&](ResourceId guid, UInt64 localFileId)
		                               {
			                               return guid == textureGuid && localFileId == 0
			                                          ? VirtualPath::Parse("prj:Texture.ursatex")
			                                          : Result<VirtualPath>::Failure("Unknown fixture resource");
		                               }));
		auto shader = std::make_shared<Shader::ShaderAsset>();
		shader->id = ResourceId::Generate();
		Shader::PropertyDeclaration tint;
		tint.identifier = "_Color";
		tint.type = Shader::PropertyType::Color;
		tint.defaultVector = {1, 1, 1, 1};
		shader->properties.push_back(tint);
		Shader::PropertyDeclaration strength;
		strength.identifier = "_Strength";
		strength.type = Shader::PropertyType::Range;
		strength.defaultFloat = 0.5f;
		shader->properties.push_back(strength);
		Shader::PropertyDeclaration image;
		image.identifier = "_Texture";
		image.type = Shader::PropertyType::Texture2D;
		image.defaultTexture = "white";
		shader->properties.push_back(image);
		auto draft = std::make_shared<MaterialDraft>(MaterialType::Unlit);
		Done(draft->Material().SetShaderAsset(shader));
		const auto localIdentity = draft->Material().LocalIdentity();
		bool live = true;
		auto properties = Take(SerializedObject::Create(draft->Properties(
		    *session, true,
		    [&] { return live ? Result<void>{} : Result<void>::Failure("Material document was closed"); })));
		Done(Take(properties->FindProperty("/Fields/_Color")).SetValue({0.2, 0.4, 0.6, 1.0}));
		Done(Take(properties->FindProperty("/Fields/_Strength")).SetValue(2.0));
		const auto previous = draft->MaterialPtr();
		Expect(!properties->Apply() && draft->MaterialPtr() == previous &&
		           Take(draft->Material().PropertyValue("_Color", *shader)).vectorValue[0] == 1,
		       "A Shader range failure partially committed the earlier color edit");
		Done(Take(properties->FindProperty("/Fields/_Strength")).SetValue(0.75));
		Done(
		    Take(properties->FindProperty("/Fields/_Texture"))
		        .SetValue({{"$ursaReference",
		                    {{"Kind", "Texture"}, {"Id", ""}, {"Resource", AssetReferenceToJson({textureGuid, 0})}}}}));
		Done(properties->Apply());
		Expect(draft->Material().LocalIdentity() == localIdentity &&
		           Take(draft->Material().PropertyValue("_Strength", *shader)).floatValue == 0.75f &&
		           Take(draft->Material().PropertyValue("_Texture", *shader)).textureId == textureGuid,
		       "Typed material transaction lost its resource identity, Shader range or texture reference");
		Done(Take(properties->FindProperty("/Fields/_Texture")).SetValue(nullptr));
		Done(properties->Apply());
		Expect(draft->Material().PropertyOverrides().find("_Texture") == draft->Material().PropertyOverrides().end(),
		       "Clearing a texture did not restore the Shader default");
		Done(draft->Material().SetPropertyValue("_Strength", *shader, MaterialPropertyValue::Range(0.25f)));
		Done(Take(properties->FindProperty("/Fields/_Strength")).SetValue(0.8));
		Expect(!properties->Apply(), "An old material property snapshot overwrote an external edit");
		auto locked = Take(SerializedObject::Create(draft->Properties(*session, false)));
		Done(Take(locked->FindProperty("/Fields/_Strength")).SetValue(0.9));
		Expect(!locked->Apply(), "A read-only material accepted a property write");

		auto multiShader = std::make_shared<Shader::ShaderAsset>(*shader);
		auto multiFirst = std::make_shared<MaterialDraft>(MaterialType::Unlit);
		auto multiSecond = std::make_shared<MaterialDraft>(MaterialType::Unlit);
		Done(multiFirst->Material().SetShaderAsset(multiShader));
		Done(multiSecond->Material().SetShaderAsset(multiShader));
		auto firstProperties = Take(SerializedObject::Create(multiFirst->Properties(*session)));
		auto secondProperties = Take(SerializedObject::Create(multiSecond->Properties(*session)));
		auto sameShaderGroup = Take(SerializedObjectGroup::Create({firstProperties.get(), secondProperties.get()}));
		const auto sameShaderPaths = sameShaderGroup.CommonPropertyPaths();
		Expect(std::find(sameShaderPaths.begin(), sameShaderPaths.end(), "/Fields/_Strength") != sameShaderPaths.end(),
		       "Materials with the same Shader did not expose their shared declared property");
		Done(sameShaderGroup.SetValue("/Fields/_Strength", 0.8));
		Done(sameShaderGroup.Apply());
		Expect(Take(multiFirst->Material().PropertyValue("_Strength", *multiShader)).floatValue == 0.8f &&
		           Take(multiSecond->Material().PropertyValue("_Strength", *multiShader)).floatValue == 0.8f,
		       "A common Shader property did not commit to every material target");
		auto unrelatedShader = std::make_shared<Shader::ShaderAsset>(*multiShader);
		unrelatedShader->id = ResourceId::Generate();
		auto otherMaterial = std::make_shared<MaterialDraft>(MaterialType::Unlit);
		Done(otherMaterial->Material().SetShaderAsset(unrelatedShader));
		auto otherProperties = Take(SerializedObject::Create(otherMaterial->Properties(*session)));
		auto mixedShaderGroup = Take(SerializedObjectGroup::Create({firstProperties.get(), otherProperties.get()}));
		const auto mixedShaderPaths = mixedShaderGroup.CommonPropertyPaths();
		Expect(std::find(mixedShaderPaths.begin(), mixedShaderPaths.end(), "/Fields/_Strength") ==
		           mixedShaderPaths.end(),
		       "Same-name properties from different Shaders were incorrectly treated as semantically common");
		live = false;
		Expect(!properties->Update(), "A closed document's material adapter stayed active");
		draft.reset();
		Expect(!locked->Update(), "An expired material draft stayed writable");
		std::cout << "Material Shader properties use atomic drafts, stable identities and scoped write access\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
