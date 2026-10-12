#include <ResourceFixtures.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Material/Material.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderSourceLibrary.hpp>
#include <Vfs/MemoryFileProvider.hpp>

#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Shader;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Path(const char* value)
{
	auto parsed = VirtualPath::Parse(value);
	Expect(static_cast<bool>(parsed), "Invalid test VFS path");
	return parsed.value;
}

void WriteText(VirtualFileSystem& filesystem, const char* path, const char* text)
{
	auto virtualPath = VirtualPath::Parse(path);
	Expect(static_cast<bool>(virtualPath), "Invalid test virtual path");
	Vector<UInt8> bytes;
	for (const char* character = text; *character != '\0'; ++character)
	{
		bytes.push_back(static_cast<UInt8>(*character));
	}
	Expect(static_cast<bool>(filesystem.WriteAtomic(virtualPath.value, bytes)), "Cannot write test Shader source");
	Expect(bool(WriteAssetMetadata(filesystem, virtualPath.value, ResourceId::Generate())),
	       "Cannot write Shader metadata");
}

CompiledShaderStage MakeStage(ProgramStage stage, const char* entryPoint)
{
	return {stage, entryPoint, {0x03, 0x02, 0x23, 0x07}, "{\"entryPoints\":[]}", "DXC test", {}};
}
} // namespace

int main()
{
	try
	{
		VirtualFileSystem filesystem;
		auto provider = std::make_shared<MemoryFileProvider>();
		Expect(static_cast<bool>(filesystem.MountProvider("prj", provider, MountAccess::ReadWrite)),
		       "Cannot mount the in-memory shader project");
		WriteText(filesystem, "prj:Shaders/Asset.ursashader",
		          "Shader \"Ursa/Asset\" { ID \"c6f1e600-b5c6-4c56-a5e1-5bba91570141\" Version 1 "
		          "Properties { _Tint(\"Tint\", Color)=(1,1,1,1) _MainTex(\"Base Map\", 2D)=\"white\" {} } "
		          "CustomEditor \"Tests/CustomMaterialInspector\" "
		          "SubShader { Tags { \"LightMode\" = \"Forward\" } "
		          "Pass { Name \"Forward\" HLSLPROGRAM\n#pragma vertex VSMain\n#pragma fragment PSMain\nENDHLSL } } }");
		auto library = ShaderSourceLibrary::Load(filesystem, "prj");
		Expect(static_cast<bool>(library), "Source Shader did not load");
		const ShaderSourceAsset* source = library.value.Find("Ursa/Asset");
		Expect(source != nullptr, "Source Shader was not indexed by name");
		auto dependencies = library.value.ResolveDependencies(filesystem, source->source.name);
		Expect(static_cast<bool>(dependencies), "Source Shader dependencies did not resolve");
		ShaderPassCompilation passCompilation;
		passCompilation.stages = {MakeStage(ProgramStage::Vertex, "VSMain"),
		                          MakeStage(ProgramStage::Fragment, "PSMain")};
		ShaderPassCompilation optionalCompilation = passCompilation;
		optionalCompilation.keywords = {"USE_FEATURE"};
		optionalCompilation.required = false;
		auto asset = BuildShaderAsset(*source, library.value, dependencies.value, "source-hash-1", "DXC test",
		                              {passCompilation, optionalCompilation});
		Expect(static_cast<bool>(asset), "Compiled Shader asset metadata could not be built");
		const auto assetPath = VirtualPath::Parse("prj:Imported/Asset.ursashaderasset").value;
		Expect(static_cast<bool>(SaveShaderAssetResource(filesystem, assetPath, asset.value)),
		       "Shader asset could not be saved through VFS");
		Expect(static_cast<bool>(WriteAssetMetadata(filesystem, assetPath, source->source.id)),
		       "Shader asset GUID metadata could not be saved through VFS");
		auto header = ReadResourceHeader(filesystem, assetPath);
		Expect(static_cast<bool>(header) && header.value.kind == ResourceKind::Shader &&
		           header.value.id == source->source.id,
		       "Shader asset resource header lost its stable identity");
		const ResourceId materialId = ResourceId::Generate();
		MaterialInstance material(MaterialType::Unlit);
		Expect(static_cast<bool>(material.SetShader(source->source.id)), "Cannot assign a Shader to a material");
		Expect(static_cast<bool>(material.SetPropertyValue(
		           "_Tint", asset.value, MaterialPropertyValue::ColorValue({0.2f, 0.4f, 0.6f, 1.0f}))),
		       "Cannot set a Shader-backed material property");
		Expect(static_cast<bool>(
		           SaveMaterialResource(filesystem, Path("prj:Imported/Material.ursamat"), materialId, material, {})),
		       "Cannot save a Shader-backed material");
		Expect(static_cast<bool>(WriteAssetMetadata(filesystem, Path("prj:Imported/Material.ursamat"), materialId)),
		       "Cannot save Shader-backed material GUID metadata");
		auto session = ResourceSession::Open(filesystem);
		Expect(static_cast<bool>(session), "Resource session could not index a compiled Shader asset");
		auto loaded = session.value->LoadShader(source->source.id);
		Expect(static_cast<bool>(loaded), "Shader asset could not load through ResourceSession");
		Expect(loaded.value->id == source->source.id && loaded.value->name == "Ursa/Asset" &&
		           loaded.value->properties.size() == 2 && loaded.value->subShaders.size() == 1 &&
		           loaded.value->customEditor == "Tests/CustomMaterialInspector",
		       "Shader source identity or metadata was not restored");
		Expect(loaded.value->subShaders[0].passes[0].variants.size() == 2 &&
		           loaded.value->subShaders[0].passes[0].variants[0].stages.size() == 2 &&
		           loaded.value->subShaders[0].passes[0].variants[0].required &&
		           !loaded.value->subShaders[0].passes[0].variants[1].required &&
		           loaded.value->subShaders[0].passes[0].variants[0].stages[1].reflectionJson == "{\"entryPoints\":[]}",
		       "Shader variants, backend bytecode, or reflection metadata were not restored");
		auto cached = session.value->LoadShader(source->source.id);
		Expect(static_cast<bool>(cached) && cached.value == loaded.value,
		       "Resource session did not cache the loaded Shader by stable ID");
		auto loadedMaterial = session.value->LoadMaterial(materialId);
		Expect(loadedMaterial && loadedMaterial.value->ShaderId() == source->source.id &&
		           loadedMaterial.value->ValidateAgainstShader(*loaded.value),
		       "Resource session did not validate a Shader-backed material reference");

		MaterialInstance missingShaderMaterial(MaterialType::Unlit);
		const ResourceId missingShaderId = ResourceId::Generate();
		Expect(static_cast<bool>(missingShaderMaterial.SetShader(missingShaderId)),
		       "Cannot create missing-Shader fixture");
		const ResourceId missingShaderMaterialId = ResourceId::Generate();
		Expect(static_cast<bool>(SaveMaterialResource(filesystem, Path("prj:Imported/MissingShader.ursamat"),
		                                              missingShaderMaterialId, missingShaderMaterial, {})),
		       "Cannot save missing-Shader fixture");
		Expect(static_cast<bool>(
		           WriteAssetMetadata(filesystem, Path("prj:Imported/MissingShader.ursamat"), missingShaderMaterialId)),
		       "Cannot save missing-Shader metadata");
		auto refreshed = ResourceSession::Open(filesystem);
		Expect(static_cast<bool>(refreshed), "Cannot refresh resource index for missing-Shader test");
		auto rejectedShader = refreshed.value->LoadMaterial(missingShaderMaterialId);
		Expect(!rejectedShader && rejectedShader.error.find(missingShaderId.ToString()) != String::npos,
		       "Material with a missing Shader ID was accepted");

		MaterialInstance missingTextureMaterial(MaterialType::Unlit);
		Expect(static_cast<bool>(missingTextureMaterial.SetShader(source->source.id)), "Cannot create texture fixture");
		const ResourceId missingTextureId = ResourceId::Generate();
		Expect(static_cast<bool>(missingTextureMaterial.SetPropertyValue(
		           "_MainTex", asset.value, MaterialPropertyValue::Texture2D(missingTextureId))),
		       "Cannot set missing-texture fixture");
		const ResourceId missingTextureMaterialId = ResourceId::Generate();
		Expect(static_cast<bool>(SaveMaterialResource(filesystem, Path("prj:Imported/MissingTexture.ursamat"),
		                                              missingTextureMaterialId, missingTextureMaterial, {})),
		       "Cannot save missing-texture fixture");
		Expect(static_cast<bool>(WriteAssetMetadata(filesystem, Path("prj:Imported/MissingTexture.ursamat"),
		                                            missingTextureMaterialId)),
		       "Cannot save missing-texture metadata");
		refreshed = ResourceSession::Open(filesystem);
		Expect(static_cast<bool>(refreshed), "Cannot refresh resource index for missing-texture test");
		auto rejectedTexture = refreshed.value->LoadMaterial(missingTextureMaterialId);
		Expect(!rejectedTexture && rejectedTexture.error.find(missingTextureId.ToString()) != String::npos,
		       "Material with a missing texture ID was accepted");

		VirtualFileSystem readonlyFilesystem;
		Expect(static_cast<bool>(readonlyFilesystem.MountProvider("prj", provider, MountAccess::ReadOnly)),
		       "Cannot mount read-only shader package fixture");
		auto readonlySession = ResourceSession::Open(readonlyFilesystem);
		Expect(static_cast<bool>(readonlySession), "Read-only resource session could not index Shader assets");
		Expect(!SaveShaderAssetResource(readonlyFilesystem, assetPath, asset.value),
		       "Read-only package accepted a Shader asset write");
		Expect(
		    !SaveMaterialResource(readonlyFilesystem, Path("prj:Imported/Material.ursamat"), materialId, material, {}),
		    "Read-only package accepted a Material write");

		Expect(static_cast<bool>(
		           SaveShaderAssetResource(filesystem, Path("prj:Imported/Duplicate.ursashaderasset"), asset.value)),
		       "Cannot write duplicate Shader ID fixture");
		Expect(static_cast<bool>(
		           WriteAssetMetadata(filesystem, Path("prj:Imported/Duplicate.ursashaderasset"), source->source.id)),
		       "Cannot write duplicate Shader ID metadata fixture");
		auto duplicates = ResourceSession::Open(filesystem);
		Expect(!duplicates && duplicates.error.find("Duplicate asset GUID") != String::npos,
		       "Resource index accepted duplicate Shader IDs");
		std::cout << "Shader asset tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
