#include <Assets/ModelImporter.hpp>
#include <Assets/ModelImportScene.hpp>
#include <Assets/TextureImporter.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Assets/AssetImportRules.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

void Expect(bool condition, const String& message)
{
	if (!condition)
	{
		throw std::runtime_error(message.c_str());
	}
}

VirtualPath Path(const char* path)
{
	return Take(VirtualPath::Parse(path));
}

VirtualFileSystem Files()
{
	VirtualFileSystem files;
	Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	return files;
}

void Text(VirtualFileSystem& files, const char* path, const std::string& text)
{
	Done(files.WriteAtomic(Path(path), Vector<UInt8>(text.data(), text.data() + text.size())));
}

Vector<UInt8> Fixture(const char* path)
{
	std::ifstream input(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
	Expect(bool(input) && input.tellg() > 0, "Model candidate fixture is missing");
	Vector<UInt8> bytes(static_cast<Size>(input.tellg()));
	input.seekg(0);
	input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Expect(bool(input), "Model candidate FBX fixture could not be read");
	return bytes;
}

std::string FixtureText(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	Expect(bool(input), "glTF candidate fixture is missing");
	return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void EditableGraph()
{
	auto files = Files();
	Text(files, "prj:Example.obj", "mtllib Example.mtl\no Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Red\nf 1 2 3\n");
	Text(files, "prj:Example.mtl", "newmtl Red\nKd 1 0 0\nd 0.5\n");
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	const auto source = Path("prj:Example.obj");
	auto candidate = Take(ModelImporter::BuildCandidate(*database, source, {}));
	auto rebuiltCandidate = Take(ModelImporter::BuildCandidate(*database, source, {}));
	Expect(!candidate.nodes.empty() && candidate.meshes.size() == 1 && !candidate.materials.empty() &&
	           !Take(files.Exists(Path("lib:AssetDatabase/prj.json"))),
	       "Model construction encoded or published the primary model before postprocessing");
	Expect(!rebuiltCandidate.materials.empty(), "Repeated model import lost all material outputs");
	Expect(candidate.materials.begin()->second.material && rebuiltCandidate.materials.begin()->second.material,
	       "Model import produced an empty material instance");
	const auto& importedMaterial = *candidate.materials.begin()->second.material;
	const auto& rebuiltMaterial = *rebuiltCandidate.materials.begin()->second.material;
	Expect(importedMaterial.IsShaderBacked() && importedMaterial.ShaderId() == Shader::BuiltinPbrShaderId() &&
	           importedMaterial.Parameters().roughness == 1.0f &&
	           importedMaterial.Parameters().alphaMode == AlphaMode::Blend &&
	           importedMaterial.PropertyOverrides().find("_BaseColor") != importedMaterial.PropertyOverrides().end() &&
	           importedMaterial.PropertyOverrides().find("_Roughness") != importedMaterial.PropertyOverrides().end() &&
	           importedMaterial.AssetIdentity().guid == candidate.guid &&
	           importedMaterial.AssetIdentity().localFileId == candidate.materials.begin()->first &&
	           rebuiltCandidate.materials.begin()->first == candidate.materials.begin()->first &&
	           rebuiltMaterial.AssetIdentity().guid == importedMaterial.AssetIdentity().guid &&
	           rebuiltMaterial.AssetIdentity().localFileId == importedMaterial.AssetIdentity().localFileId,
	       "Model material import lost the new Shader property layout, transparency or stable subasset identity");
	const auto meshId = candidate.meshOrder.front();
	const auto materialId = candidate.meshMaterialBindings.at(meshId).at(0);
	const auto originalNodeId = candidate.rootNodeLocalFileId;
	auto root = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
	                         [&](const auto& node) { return node.localFileId == originalNodeId; });
	root->name = "Renamed by postprocessor";
	root->transform.position.x = 3;
	const auto addedId = Take(StableImportObjectId("ursa.Model", "node:postprocessor-created"));
	root->children.push_back(addedId);
	ModelCandidateNode child;
	child.localFileId = addedId;
	child.parentLocalFileId = originalNodeId;
	child.name = "Created by postprocessor";
	candidate.outputKeys[addedId] = "node:postprocessor-created";
	candidate.displayNames[addedId] = child.name;
	candidate.nodes.push_back(std::move(child));
	auto positions = candidate.meshes.at(meshId)->Snapshot()->data.positions;
	positions[0].z = 2;
	Done(candidate.meshes.at(meshId)->SetVertices(std::move(positions)));
	Done(candidate.materials.at(materialId).material->SetBaseColor({.1f, .7f, .2f, 1}));
	const auto outputs = Take(candidate.Encode());
	Expect(candidate.displayNames.at(meshId) == "Triangle" && candidate.displayNames.at(materialId) == "Red",
	       "OBJ source names were replaced with stable keys");
	for (const auto& output : outputs)
	{
		Expect(!output.displayName.empty() && output.visibleInProject == (output.kind != "ModelNode"),
		       "Encoded model output lost its name or exposed an internal hierarchy node");
	}
	auto invalidNames = candidate;
	invalidNames.displayNames.erase(meshId);
	Expect(!invalidNames.Encode(), "An incomplete presentation directory was encoded");
	invalidNames = candidate;
	invalidNames.displayNames[1234] = "Dangling output";
	Expect(!invalidNames.Encode(), "A dangling presentation descriptor was encoded");
	Expect(!Take(files.Exists(Path("lib:AssetDatabase/prj.json"))), "Candidate encoding published to the real Library");
	const auto model = nlohmann::json::from_cbor(outputs.front().bytes.begin(), outputs.front().bytes.end());
	const auto edited =
	    std::find_if(model.at("nodes").begin(), model.at("nodes").end(), [originalNodeId](const nlohmann::json& node)
	                 { return node.at("localFileId").get<UInt64>() == originalNodeId; });
	Expect(edited != model.at("nodes").end() && edited->at("name") == "Renamed by postprocessor" &&
	           edited->at("localTransform").at("position")[0] == 3 &&
	           candidate.nodes.size() == model.at("nodes").size(),
	       "Node edits or display rename changed stable identity or were not encoded");
	Done(database->PublishImport(source, "ursa.Model", ModelImporter::Version, ModelImporter::ContentTarget,
	                             ModelImportSettings{}.ToJson(), candidate.dependencies, outputs,
	                             candidate.mainObjectLocalFileId));
	auto engineFiles = Take(OpenEngineFileSystem(std::filesystem::u8path(URSA_ENGINE_SHADER_PAK)));
	ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(engineFiles));

	struct EngineFileSystemCleanup
	{
		~EngineFileSystemCleanup()
		{
			ConfigureEngineFileSystem({});
		}
	} engineFileSystemCleanup;

	auto resources = Take(ResourceSession::Open(files, [&](ResourceId guid, UInt64 localId)
	                                            { return database->Resolve(guid, localId); }));
	const auto mesh = Take(resources->LoadMesh(candidate.guid, meshId));
	const auto material = Take(resources->LoadMaterial(candidate.guid, materialId));
	Expect(mesh->Snapshot()->data.positions[0].z == 2 && material->Parameters().baseColor.green == .7f &&
	           Take(database->CurrentImport(source)).mainObjectLocalFileId == candidate.mainObjectLocalFileId,
	       "Published Mesh or material did not contain postprocessed CPU values");
	candidate.meshMaterialBindings[meshId] = {123};
	Expect(!candidate.Encode(), "Dangling material binding was encoded");
	candidate.meshMaterialBindings[meshId] = {materialId};
	candidate.nodes.back().children.push_back(originalNodeId);
	Expect(!candidate.Encode(), "Cyclic hierarchy was encoded");
	candidate.nodes.back().children.clear();
	candidate.nodes.back().transform.position.x = std::numeric_limits<Float32>::quiet_NaN();
	Expect(!candidate.Encode(), "Nonfinite node transform was encoded");
}

void FbxCandidates(const char* fixture, const char* source, bool animated)
{
	auto files = Files();
	Done(files.WriteAtomic(Path(source), Fixture(fixture)));
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	auto candidate = Take(ModelImporter::BuildCandidate(*database, Path(source), {}));
	Expect(!candidate.meshes.empty(), "FBX CPU candidate lost its geometry");
	if (animated)
	{
		Expect(!candidate.animationOrder.empty() && !candidate.skeletons.empty(),
		       "Animated FBX lost skeleton or clip candidates");
		bool skinned = false;
		for (const auto& entry : candidate.meshes)
		{
			const auto data = entry.second->Snapshot();
			if (data->data.skeleton)
			{
				skinned = !data->data.skinInfluences.empty() && !data->data.skeleton->joints.empty() &&
				          !data->data.skeleton->joints[0].name.empty();
			}
		}
		Expect(skinned, "Skinned candidate used an empty placeholder skeleton");
	}
	else
	{
		Expect(!candidate.textures.empty(), "Embedded FBX texture was encoded too early or discarded");
	}
	const auto originalRoot = candidate.rootNodeLocalFileId;
	const auto originalClips = candidate.animationOrder;
	Scene privateScene;
	ModelImportScene editable(candidate, privateScene);
	const auto root = Take(editable.Build());
	Done(root->SetName("Processed FBX"));
	auto local = Take(root->GetLocalTransform());
	local.position.x = 4;
	Done(root->SetLocalTransform(local));
	const auto extra = Take(privateScene.CreateGameObject("Added by postprocessor", root));
	const auto discarded = Take(privateScene.CreateGameObject("Removed before publication", root));
	Done(discarded->Destroy());
	auto replacement = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(replacement->SetBaseColor({0, 1, 0, 1}));
	const auto originalMesh = candidate.meshes.at(candidate.meshOrder.front());
	Done(Take(extra->AddComponent<MeshFilterComponent>())->SetSharedMesh(originalMesh));
	Done(Take(extra->AddComponent<MeshRendererComponent>())->SetSharedMaterials({replacement, nullptr, replacement}));
	auto vertices = originalMesh->Snapshot()->data.positions;
	vertices.front().z += 2;
	Done(originalMesh->SetVertices(vertices));
	Done(editable.Apply());
	Expect(candidate.rootNodeLocalFileId == originalRoot && candidate.animationOrder == originalClips,
	       "Private model view changed original node or animation identities");
	Expect(candidate.displayNames.at(originalRoot) == "Processed FBX",
	       "Postprocessed node names did not follow their stable identity");
	Expect(std::none_of(candidate.nodes.begin(), candidate.nodes.end(),
	                    [](const auto& node) { return node.name == "Removed before publication"; }),
	       "Deleted temporary model node survived publication");
	const auto added = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
	                                [](const auto& node) { return node.name == "Added by postprocessor"; });
	Expect(added != candidate.nodes.end() && added->meshes.size() == 1 &&
	           candidate.meshMaterialBindings.at(added->meshes.front()).size() == 3 &&
	           candidate.meshMaterialBindings.at(added->meshes.front())[1] == 0,
	       "Model view lost a new node or its ordered material slots");
	const auto encoded = Take(candidate.Encode());
	Expect(encoded.size() > candidate.meshes.size() && !Take(files.Exists(Path("lib:AssetDatabase/prj.json"))),
	       "FBX candidate serialization did not preserve auxiliary resources or published prematurely");
}

void GltfPreflight()
{
	auto files = Files();
	Text(files, "prj:WrongVersion.gltf", R"({"asset":{"version":"1.0"},"scenes":[]})");
	Text(files, "prj:NoScene.gltf", R"({"asset":{"version":"2.0"},"scenes":[]})");
	Text(files, "prj:RequiredExtension.gltf",
	     R"({"asset":{"version":"2.0"},"scenes":[{}],"extensionsRequired":["KHR_draco_mesh_compression"]})");
	Done(files.WriteAtomic(Path("prj:Broken.glb"), Vector<UInt8>{'b', 'a', 'd', '!'}));
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	auto wrongVersion = ModelImporter::BuildCandidate(*database, Path("prj:WrongVersion.gltf"), {});
	auto noScene = ModelImporter::BuildCandidate(*database, Path("prj:NoScene.gltf"), {});
	auto requiredExtension = ModelImporter::BuildCandidate(*database, Path("prj:RequiredExtension.gltf"), {});
	auto brokenGlb = ModelImporter::BuildCandidate(*database, Path("prj:Broken.glb"), {});
	Expect(!wrongVersion && wrongVersion.error.find("glTF 2.0") != String::npos,
	       "glTF preflight did not reject an unsupported asset version");
	Expect(!noScene && noScene.error.find("selectable scene") != String::npos,
	       "glTF preflight did not reject a source without a scene");
	Expect(!requiredExtension && requiredExtension.error.find("KHR_draco_mesh_compression") != String::npos,
	       "glTF preflight did not report an unsupported required extension");
	Expect(!brokenGlb && brokenGlb.error.find("GLB") != String::npos,
	       "GLB preflight did not reject an invalid container header");
	Expect(ModelImporter::IsSupportedSource(Path("prj:Asset.GLTF")) &&
	           ModelImporter::IsSupportedSource(Path("prj:Asset.GLB")),
	       "Model importer extension routing is not case insensitive");
	const std::string duplicateNames =
	    R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1,2]}],"nodes":[{"name":"Twin"},{"name":"Twin"},{}]})";
	Text(files, "prj:DuplicateNames.gltf", duplicateNames);
	Done(database->Refresh());
	ModelImportSettings settings;
	settings.importMaterials = false;
	auto normalized = ModelImporter::BuildCandidate(*database, Path("prj:DuplicateNames.gltf"), settings);
	Expect(bool(normalized),
	       "glTF node names were not normalized for import: " + (normalized ? String{} : normalized.error));
	Vector<String> importedNames;
	Set<UInt64> importedIds;
	for (const auto& node : normalized.value.nodes)
	{
		importedNames.push_back(node.name);
		importedIds.insert(node.localFileId);
	}
	Expect(importedNames.size() >= 3 && std::count(importedNames.begin(), importedNames.end(), "Twin") == 2 &&
	           std::find(importedNames.begin(), importedNames.end(), "Node 2") != importedNames.end() &&
	           importedIds.size() == importedNames.size(),
	       "glTF duplicate or unnamed nodes lost their display names or stable identities");
	Expect(ToStdString(Take(files.ReadText(Path("prj:DuplicateNames.gltf")))) == duplicateNames,
	       "glTF node normalization modified the project source file");
	Vector<UInt8> jsonBytes;
	jsonBytes.reserve(duplicateNames.size());
	for (unsigned char character : duplicateNames)
	{
		jsonBytes.push_back(character);
	}
	while (jsonBytes.size() % 4 != 0)
	{
		jsonBytes.push_back(static_cast<UInt8>(' '));
	}
	Vector<UInt8> glb;
	auto appendU32 = [&glb](UInt32 value)
	{
		glb.push_back(static_cast<UInt8>(value & 0xff));
		glb.push_back(static_cast<UInt8>((value >> 8) & 0xff));
		glb.push_back(static_cast<UInt8>((value >> 16) & 0xff));
		glb.push_back(static_cast<UInt8>((value >> 24) & 0xff));
	};
	appendU32(0x46546C67);
	appendU32(2);
	appendU32(static_cast<UInt32>(12 + 8 + jsonBytes.size()));
	appendU32(static_cast<UInt32>(jsonBytes.size()));
	appendU32(0x4E4F534A);
	glb.insert(glb.end(), jsonBytes.begin(), jsonBytes.end());
	Done(files.WriteAtomic(Path("prj:DuplicateNames.glb"), glb));
	Done(database->Refresh());
	auto normalizedGlb = ModelImporter::BuildCandidate(*database, Path("prj:DuplicateNames.glb"), settings);
	Expect(bool(normalizedGlb) && normalizedGlb.value.nodes.size() >= 3,
	       "GLB node-name normalization failed: " + (normalizedGlb ? String{} : normalizedGlb.error));
	Done(files.WriteAtomic(
	    Path("prj:Uppercase.GLTF"),
	    Fixture((std::filesystem::u8path(URSA_CANDIDATE_GLTF_DATA_URI_DIR) / "BoxTextured.gltf").string().c_str())));
	Done(database->Refresh());
	ModelImportSettings fullSettings;
	auto uppercase = ModelImporter::BuildCandidate(*database, Path("prj:Uppercase.GLTF"), fullSettings);
	Expect(bool(uppercase) && !uppercase.value.meshes.empty(),
	       "Uppercase glTF extension did not follow the native model importer: " +
	           (uppercase ? String{} : uppercase.error));
	Done(files.WriteAtomic(Path("prj:Embedded.glb"), Fixture(URSA_CANDIDATE_GLB)));
	Done(database->Refresh());
	auto embeddedGlb = ModelImporter::BuildCandidate(*database, Path("prj:Embedded.glb"), fullSettings);
	Expect(bool(embeddedGlb) && !embeddedGlb.value.meshes.empty() && !embeddedGlb.value.textures.empty(),
	       "GLB embedded geometry or texture was not imported: " + (embeddedGlb ? String{} : embeddedGlb.error));
}

void GltfExternalResources()
{
	auto files = Files();
	const auto externalRoot = std::filesystem::u8path(URSA_CANDIDATE_GLTF_EXTERNAL_DIR);
	const auto dataUriRoot = std::filesystem::u8path(URSA_CANDIDATE_GLTF_DATA_URI_DIR);
	auto externalDocument = nlohmann::json::parse(FixtureText(externalRoot / "BoxTextured.gltf"));
	externalDocument["extensionsUsed"] = {"KHR_materials_variants", "KHR_texture_transform"};
	auto& pbr = externalDocument["materials"][0]["pbrMetallicRoughness"];
	pbr["baseColorFactor"] = {0.2, 0.4, 0.6, 0.8};
	pbr["metallicFactor"] = 0.37;
	pbr["roughnessFactor"] = 0.62;
	externalDocument["materials"][0]["emissiveFactor"] = {0.1, 0.2, 0.3};
	const UInt32 sharedTexture = externalDocument["textures"].size() - 1;
	pbr["metallicRoughnessTexture"] = {{"index", sharedTexture}};
	externalDocument["materials"][0]["occlusionTexture"] = {{"index", sharedTexture}};
	externalDocument["materials"][0]["emissiveTexture"] = {{"index", sharedTexture}};
	externalDocument["materials"][0]["alphaMode"] = "MASK";
	externalDocument["materials"][0]["alphaCutoff"] = 0.25;
	externalDocument["materials"][0]["doubleSided"] = true;
	const auto normalMapRoot = std::filesystem::u8path(URSA_CANDIDATE_GLTF_JPEG_DIR);
	externalDocument["images"].push_back({{"uri", "textures/normal%20map.jpg"}});
	externalDocument["textures"].push_back({{"source", externalDocument["images"].size() - 1}});
	externalDocument["materials"][0]["normalTexture"] = {{"index", externalDocument["textures"].size() - 1}};
	externalDocument["materials"][0]["normalTexture"]["texCoord"] = 1;
	externalDocument["materials"][0]["normalTexture"]["extensions"] = {
	    {"KHR_texture_transform", {{"offset", {0.2, 0.3}}, {"rotation", 0.5}, {"scale", {2.0, 2.0}}}}};
	externalDocument["samplers"].push_back(
	    {{"magFilter", 9728}, {"minFilter", 9728}, {"wrapS", 33071}, {"wrapT", 33648}});
	externalDocument["textures"].back()["sampler"] = 0;
	std::string externalSource = externalDocument.dump();
	const auto bufferUri = externalSource.find("BoxTextured0.bin");
	Expect(bufferUri != std::string::npos && externalSource.find("CesiumLogoFlat.png") != std::string::npos,
	       "glTF external-resource fixture has unexpected URIs");
	externalSource.replace(bufferUri, std::string("BoxTextured0.bin").size(), "mesh%20data.bin");
	const auto imageUri = externalSource.find("CesiumLogoFlat.png");
	externalSource.replace(imageUri, std::string("CesiumLogoFlat.png").size(), "textures/logo%20flat.png");
	Text(files, "prj:models/External.gltf", externalSource);
	Done(files.WriteAtomic(Path("prj:models/mesh data.bin"),
	                       Fixture((externalRoot / "BoxTextured0.bin").string().c_str())));
	Done(files.WriteAtomic(Path("prj:models/textures/logo flat.png"),
	                       Fixture((externalRoot / "CesiumLogoFlat.png").string().c_str())));
	Done(files.WriteAtomic(Path("prj:models/textures/normal map.jpg"),
	                       Fixture((normalMapRoot / "PlasticWrap_normals.jpg").string().c_str())));
	Text(files, "prj:models/DataUri.gltf", FixtureText(dataUriRoot / "BoxTextured.gltf"));
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	ModelImportSettings settings;
	auto multiScene = nlohmann::json::parse(externalSource);
	multiScene["nodes"].push_back({{"name", "DefaultSceneRoot"}, {"mesh", 0}});
	multiScene["scenes"] = nlohmann::json::array({{{"nodes", {0}}}, {{"nodes", {2}}}});
	multiScene["scene"] = 1;
	Text(files, "prj:models/DefaultScene.gltf", multiScene.dump());
	Done(database->Refresh());
	auto selectedScene = ModelImporter::BuildCandidate(*database, Path("prj:models/DefaultScene.gltf"), settings);
	Expect(bool(selectedScene) &&
	           std::any_of(selectedScene.value.nodes.begin(), selectedScene.value.nodes.end(),
	                       [](const ModelCandidateNode& node) { return node.name == "DefaultSceneRoot"; }) &&
	           std::none_of(selectedScene.value.nodes.begin(), selectedScene.value.nodes.end(),
	                        [](const ModelCandidateNode& node) { return node.name == "Node 0"; }),
	       "glTF import did not select its declared default scene");
	multiScene.erase("scene");
	Text(files, "prj:models/FirstScene.gltf", multiScene.dump());
	Done(database->Refresh());
	auto firstScene = ModelImporter::BuildCandidate(*database, Path("prj:models/FirstScene.gltf"), settings);
	Expect(bool(firstScene) &&
	           std::any_of(firstScene.value.nodes.begin(), firstScene.value.nodes.end(),
	                       [](const ModelCandidateNode& node) { return node.name == "Node 0"; }) &&
	           std::none_of(firstScene.value.nodes.begin(), firstScene.value.nodes.end(),
	                        [](const ModelCandidateNode& node) { return node.name == "DefaultSceneRoot"; }),
	       "glTF import without a declared scene did not choose the first scene");
	auto external = ModelImporter::BuildCandidate(*database, Path("prj:models/External.gltf"), settings);
	Expect(bool(external) && external.value.meshes.size() == 1,
	       "glTF percent-escaped external buffer was not imported: " + (external ? String{} : external.error));
	Expect(std::find(external.value.dependencies.begin(), external.value.dependencies.end(),
	                 Path("prj:models/mesh data.bin")) != external.value.dependencies.end() &&
	           std::find(external.value.dependencies.begin(), external.value.dependencies.end(),
	                     Path("prj:models/textures/logo flat.png")) != external.value.dependencies.end() &&
	           std::find(external.value.dependencies.begin(), external.value.dependencies.end(),
	                     Path("prj:models/textures/normal map.jpg")) != external.value.dependencies.end(),
	       "glTF external buffer, PNG or JPEG image was omitted from candidate dependencies");
	const auto baseColorMetadata = Take(database->Metadata(Path("prj:models/textures/logo flat.png")));
	const auto normalMetadata = Take(database->Metadata(Path("prj:models/textures/normal map.jpg")));
	const auto baseColorSettings =
	    Take(TextureImporter::Settings(*database, Path("prj:models/textures/logo flat.png")));
	const auto normalSettings = Take(TextureImporter::Settings(*database, Path("prj:models/textures/normal map.jpg")));
	Expect(baseColorMetadata.importer == "ursa.Texture" && baseColorSettings.sRgb &&
	           normalMetadata.importer == "ursa.Texture" && !normalSettings.sRgb &&
	           normalSettings.type == TextureImportType::NormalMap,
	       "glTF color and normal textures did not receive their expected texture color-space settings");
	bool hasPbrMaterial = false;
	bool hasBaseColorTexture = false;
	bool hasNormalTexture = false;
	bool hasMetallicRoughnessTexture = false;
	bool hasOcclusionTexture = false;
	bool hasEmissiveTexture = false;
	bool hasImportedPbrFactors = false;
	bool hasUnsupportedMaterialDiagnostic = false;
	bool hasOptionalExtensionDiagnostic = false;
	bool hasUvSetDiagnostic = false;
	bool hasTextureTransformDiagnostic = false;
	bool hasSamplerDiagnostic = false;
	for (const auto& entry : external.value.materials)
	{
		hasPbrMaterial = hasPbrMaterial || (entry.second.material->IsShaderBacked() &&
		                                    entry.second.material->ShaderId() == Shader::BuiltinPbrShaderId());
		const auto& parameters = entry.second.material->Parameters();
		hasImportedPbrFactors =
		    hasImportedPbrFactors ||
		    (std::abs(parameters.baseColor.red - 0.2f) < 0.01f && std::abs(parameters.baseColor.green - 0.4f) < 0.01f &&
		     std::abs(parameters.baseColor.blue - 0.6f) < 0.01f && std::abs(parameters.metallic - 0.37f) < 0.01f &&
		     std::abs(parameters.roughness - 0.62f) < 0.01f && std::abs(parameters.emissive.red - 0.1f) < 0.01f &&
		     std::abs(parameters.emissive.green - 0.2f) < 0.01f && std::abs(parameters.emissive.blue - 0.3f) < 0.01f);
		hasBaseColorTexture = hasBaseColorTexture ||
		                      entry.second.textures.find(MaterialTextureSlot::BaseColor) != entry.second.textures.end();
		hasNormalTexture =
		    hasNormalTexture || entry.second.textures.find(MaterialTextureSlot::Normal) != entry.second.textures.end();
		hasMetallicRoughnessTexture =
		    hasMetallicRoughnessTexture ||
		    entry.second.textures.find(MaterialTextureSlot::MetallicRoughness) != entry.second.textures.end();
		hasOcclusionTexture = hasOcclusionTexture ||
		                      entry.second.textures.find(MaterialTextureSlot::Occlusion) != entry.second.textures.end();
		hasEmissiveTexture = hasEmissiveTexture ||
		                     entry.second.textures.find(MaterialTextureSlot::Emissive) != entry.second.textures.end();
	}
	Expect(hasPbrMaterial && hasBaseColorTexture && hasNormalTexture,
	       "glTF base-color/normal textures or PBR material assignment were not preserved");
	Expect(hasImportedPbrFactors, "glTF base color, emissive, metallic or roughness factors were not imported");
	Expect(hasMetallicRoughnessTexture, "glTF metallic-roughness texture slot was not imported");
	Expect(hasOcclusionTexture, "glTF occlusion texture slot was not imported");
	Expect(hasEmissiveTexture, "glTF emissive texture slot was not imported");
	for (const String& diagnostic : external.value.diagnostics)
	{
		hasUnsupportedMaterialDiagnostic =
		    hasUnsupportedMaterialDiagnostic || diagnostic.find("alphaMode") != String::npos ||
		    diagnostic.find("alphaCutoff") != String::npos || diagnostic.find("twosided") != String::npos;
		hasOptionalExtensionDiagnostic =
		    hasOptionalExtensionDiagnostic || diagnostic.find("KHR_materials_variants") != String::npos;
		hasUvSetDiagnostic = hasUvSetDiagnostic || diagnostic.find("nonzero glTF texCoord") != String::npos;
		hasTextureTransformDiagnostic =
		    hasTextureTransformDiagnostic || diagnostic.find("KHR_texture_transform") != String::npos;
		hasSamplerDiagnostic =
		    hasSamplerDiagnostic || diagnostic.find("explicit glTF sampler settings") != String::npos;
	}
	Expect(hasUnsupportedMaterialDiagnostic,
	       "Unsupported glTF alpha or double-sided material state was not surfaced in diagnostics");
	Expect(hasOptionalExtensionDiagnostic,
	       "An optional unsupported glTF extension was not preserved as an import diagnostic");
	Expect(hasUvSetDiagnostic && hasTextureTransformDiagnostic && hasSamplerDiagnostic,
	       "Unsupported glTF UV set, texture transform or sampler setting was omitted from import diagnostics");
	auto dataUri = ModelImporter::BuildCandidate(*database, Path("prj:models/DataUri.gltf"), settings);
	Expect(bool(dataUri) && !dataUri.value.meshes.empty() && !dataUri.value.textures.empty(),
	       "glTF data URI buffer was not imported: " + (dataUri ? String{} : dataUri.error));
	const auto oldPositions = external.value.meshes.begin()->second->Snapshot()->data.positions;
	Vector<UInt8> changedBuffer = Fixture((externalRoot / "BoxTextured0.bin").string().c_str());
	Float32 changedCoordinate = 4.0f;
	UInt32 changedCoordinateBits = 0;
	std::memcpy(&changedCoordinateBits, &changedCoordinate, sizeof(changedCoordinateBits));
	for (UInt32 shift = 0; shift < 32; shift += 8)
	{
		changedBuffer[288 + shift / 8] = static_cast<UInt8>((changedCoordinateBits >> shift) & 0xff);
	}
	Done(files.WriteAtomic(Path("prj:models/mesh data.bin"), changedBuffer));
	Done(database->Refresh());
	auto changed = ModelImporter::BuildCandidate(*database, Path("prj:models/External.gltf"), settings);
	bool geometryChanged = false;
	if (changed && changed.value.meshes.begin()->second->Snapshot()->data.positions.size() == oldPositions.size())
	{
		const auto& newPositions = changed.value.meshes.begin()->second->Snapshot()->data.positions;
		for (Size index = 0; index < oldPositions.size(); ++index)
		{
			geometryChanged = geometryChanged || oldPositions[index].x != newPositions[index].x ||
			                  oldPositions[index].y != newPositions[index].y ||
			                  oldPositions[index].z != newPositions[index].z;
		}
	}
	Expect(bool(changed) && geometryChanged,
	       "Changing an external glTF buffer did not change the rebuilt candidate geometry");
	Done(files.Remove(Path("prj:models/mesh data.bin")));
	Done(files.Remove(Path("prj:models/mesh data.bin.meta")));
	Done(database->Refresh());
	auto missing = ModelImporter::BuildCandidate(*database, Path("prj:models/External.gltf"), settings);
	Expect(!missing && missing.error.find("mesh data.bin") != String::npos,
	       "Missing external glTF buffer did not produce a path-specific diagnostic");
	std::string escapingSource = externalSource;
	const auto originalUri = escapingSource.find("mesh%20data.bin");
	Expect(originalUri != std::string::npos, "glTF external buffer URI disappeared");
	escapingSource.replace(originalUri, std::string("mesh%20data.bin").size(), "..%2F..%2Foutside.bin");
	Text(files, "prj:models/Escaping.gltf", escapingSource);
	Done(database->Refresh());
	auto escaping = ModelImporter::BuildCandidate(*database, Path("prj:models/Escaping.gltf"), settings);
	Expect(!escaping && escaping.error.find("escapes the VFS root") != String::npos,
	       "glTF dependency escaping the project VFS root was not rejected");
	Text(files, "prj:Animated.gltf", FixtureText(std::filesystem::u8path(URSA_CANDIDATE_ANIMATED_GLTF)));
	Done(database->Refresh());
	auto animated = ModelImporter::BuildCandidate(*database, Path("prj:Animated.gltf"), settings);
	Expect(bool(animated) && !animated.value.skeletons.empty() && animated.value.animationOrder.size() == 1,
	       "glTF animation fixture lost its skeleton or animation: " + (animated ? String{} : animated.error));
	bool hasSkinBinding = false;
	for (const auto& entry : animated.value.meshes)
	{
		const auto data = entry.second->Snapshot();
		hasSkinBinding =
		    hasSkinBinding || (data->data.skeletonReference.guid == animated.value.guid && data->data.skeleton &&
		                       data->data.skeleton->joints.size() >= 2 && !data->data.skinJointIndices.empty() &&
		                       data->data.skinInverseBindMatrices.size() == data->data.skinJointIndices.size() &&
		                       !data->data.skinInfluences.empty());
	}
	Expect(hasSkinBinding, "glTF mesh import did not preserve its per-mesh skin binding and vertex influences");
	ModelImportSettings oneInfluenceSettings = settings;
	oneInfluenceSettings.maxBoneInfluences = 1;
	auto oneInfluence = ModelImporter::BuildCandidate(*database, Path("prj:Animated.gltf"), oneInfluenceSettings);
	Expect(bool(oneInfluence), "Single-influence skin import failed");
	const bool truncationDiagnostic = std::any_of(
	    oneInfluence.value.diagnostics.begin(), oneInfluence.value.diagnostics.end(),
	    [](const String& diagnostic) { return diagnostic.find("influences were truncated") != String::npos; });
	Expect(truncationDiagnostic, "Truncated skin influences were not reported in importer diagnostics");
	for (const auto& entry : oneInfluence.value.meshes)
	{
		const auto& data = entry.second->Snapshot()->data;
		if (!data.skeletonReference.guid)
		{
			continue;
		}
		for (const auto& influence : data.skinInfluences)
		{
			Expect(influence.weights[1] == 0.0f && influence.weights[2] == 0.0f && influence.weights[3] == 0.0f &&
			           (influence.weights[0] == 0.0f || std::abs(influence.weights[0] - 1.0f) < 0.0001f),
			       "Single-influence skin weights were not truncated and normalized");
		}
	}
	const auto& clipDocument = animated.value.archives.at(animated.value.animationOrder.front()).document;
	const auto& rotationChannel = clipDocument.at("channels").at(0);
	Expect(clipDocument.at("version") == 2 && clipDocument.at("durationSeconds") == 5.5 &&
	           rotationChannel.at("rotationInterpolation") == "LINEAR" &&
	           rotationChannel.at("rotationKeys").size() == 12 &&
	           rotationChannel.at("rotationKeys").front().at(0) == 0.0 &&
	           rotationChannel.at("rotationKeys").back().at(0) == 5.5,
	       "glTF animation import changed the source time range, key count or interpolation mode");
	const auto positiveRotation = rotationChannel.at("rotationKeys").at(2).at(1);
	const auto negativeRotation = rotationChannel.at("rotationKeys").at(8).at(1);
	Expect(std::abs(positiveRotation[0].get<Float32>()) < 0.001f &&
	           std::abs(positiveRotation[1].get<Float32>()) < 0.001f &&
	           std::abs(positiveRotation[2].get<Float32>() - 0.707f) < 0.002f &&
	           std::abs(positiveRotation[3].get<Float32>() - 0.707f) < 0.002f &&
	           std::abs(negativeRotation[2].get<Float32>() + 0.707f) < 0.002f &&
	           std::abs(negativeRotation[3].get<Float32>() - 0.707f) < 0.002f,
	       "glTF animation import changed Assimp's x/y/z/w quaternion values");
	const auto morphRoot = std::filesystem::u8path(URSA_CANDIDATE_MORPH_GLTF_DIR);
	Text(files, "prj:morph/AnimatedMorphSphere.gltf", FixtureText(morphRoot / "AnimatedMorphSphere.gltf"));
	Done(files.WriteAtomic(Path("prj:morph/AnimatedMorphSphere.bin"),
	                       Fixture((morphRoot / "AnimatedMorphSphere.bin").string().c_str())));
	Done(database->Refresh());
	auto morph = ModelImporter::BuildCandidate(*database, Path("prj:morph/AnimatedMorphSphere.gltf"), settings);
	Expect(bool(morph) && !morph.value.meshes.empty() && morph.value.animationOrder.empty(),
	       "Morph-only animation produced an empty playable clip or lost its geometry: " +
	           (morph ? String{} : morph.error));
	const bool morphWarning =
	    std::any_of(morph.value.diagnostics.begin(), morph.value.diagnostics.end(), [](const String& diagnostic)
	                { return diagnostic.find("morph animation channels are unsupported") != String::npos; });
	const bool targetWarning =
	    std::any_of(morph.value.diagnostics.begin(), morph.value.diagnostics.end(), [](const String& diagnostic)
	                { return diagnostic.find("Morph Targets are unsupported") != String::npos; });
	Expect(morphWarning && targetWarning,
	       "glTF morph target data and weights animation were not both reported as unsupported");
	auto mixedMorphDocument = nlohmann::json::parse(FixtureText(morphRoot / "AnimatedMorphSphere.gltf"));
	Vector<UInt8> mixedMorphBinary = Fixture((morphRoot / "AnimatedMorphSphere.bin").string().c_str());
	const UInt32 keyCount = mixedMorphDocument["accessors"][10]["count"].get<UInt32>();
	const UInt32 translationView = static_cast<UInt32>(mixedMorphDocument["bufferViews"].size());
	const UInt32 translationAccessor = static_cast<UInt32>(mixedMorphDocument["accessors"].size());
	const UInt32 translationSampler = static_cast<UInt32>(mixedMorphDocument["animations"][0]["samplers"].size());
	const UInt32 binaryOffset = static_cast<UInt32>(mixedMorphBinary.size());
	for (UInt32 keyIndex = 0; keyIndex < keyCount; ++keyIndex)
	{
		const Float32 translation = keyIndex == 0 ? 0.0f : 0.25f;
		const UInt32 bits = [&]
		{
			UInt32 value = 0;
			std::memcpy(&value, &translation, sizeof(value));
			return value;
		}();
		for (int component = 0; component < 3; ++component)
		{
			for (UInt32 shift = 0; shift < 32; shift += 8)
			{
				mixedMorphBinary.push_back(static_cast<UInt8>((bits >> shift) & 0xff));
			}
		}
	}
	mixedMorphDocument["buffers"][0]["byteLength"] = mixedMorphBinary.size();
	mixedMorphDocument["bufferViews"].push_back(
	    {{"buffer", 0}, {"byteOffset", binaryOffset}, {"byteLength", keyCount * 3 * sizeof(Float32)}});
	mixedMorphDocument["accessors"].push_back(
	    {{"bufferView", translationView}, {"componentType", 5126}, {"count", keyCount}, {"type", "VEC3"}});
	mixedMorphDocument["animations"][0]["samplers"].push_back(
	    {{"input", 10}, {"output", translationAccessor}, {"interpolation", "LINEAR"}});
	mixedMorphDocument["animations"][0]["channels"].push_back(
	    {{"sampler", translationSampler}, {"target", {{"node", 0}, {"path", "translation"}}}});
	Text(files, "prj:morph/MixedMorph.gltf", mixedMorphDocument.dump());
	Done(files.WriteAtomic(Path("prj:morph/AnimatedMorphSphere.bin"), mixedMorphBinary));
	Done(database->Refresh());
	auto mixedMorph = ModelImporter::BuildCandidate(*database, Path("prj:morph/MixedMorph.gltf"), settings);
	Expect(bool(mixedMorph) && mixedMorph.value.animationOrder.size() == 1,
	       "Mixed morph and transform animation lost its supported transform track: " +
	           (mixedMorph ? String{} : mixedMorph.error));
	const auto& mixedClip = mixedMorph.value.archives.at(mixedMorph.value.animationOrder.front()).document;
	Expect(!mixedClip.at("channels").empty() && !mixedClip.at("channels")[0].at("positionKeys").empty(),
	       "Mixed morph and transform animation published a clip without its supported node track");

	const auto skinDocument = nlohmann::json::parse(FixtureText(std::filesystem::u8path(URSA_CANDIDATE_ANIMATED_GLTF)));
	auto cubicDocument = skinDocument;
	std::vector<UInt8> cubicBytes;
	auto appendFloat = [&](Float32 value)
	{
		UInt32 bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		for (UInt32 shift = 0; shift < 32; shift += 8)
		{
			cubicBytes.push_back(static_cast<UInt8>((bits >> shift) & 0xff));
		}
	};
	const std::array<Float32, 3> cubicTimes{0.0f, 0.5f, 2.0f};
	for (Float32 time : cubicTimes)
	{
		appendFloat(time);
	}
	const std::array<std::array<Float32, 4>, 9> cubicRotationValues{{
	    {{0, 0, 0, 0}},
	    {{0, 0, 0, 1}},
	    {{0, 0, 0, 0.25f}},
	    {{0, 0, 0, 0.25f}},
	    {{0, 0, 0.382f, 0.924f}},
	    {{0, 0, 0, 0.25f}},
	    {{0, 0, 0, 0.25f}},
	    {{0, 0, 0.707f, 0.707f}},
	    {{0, 0, 0, 0}},
	}};
	for (const auto& quaternion : cubicRotationValues)
	{
		for (Float32 component : quaternion)
		{
			appendFloat(component);
		}
	}
	const UInt32 timeBytes = static_cast<UInt32>(cubicTimes.size() * sizeof(Float32));
	const UInt32 timeView = static_cast<UInt32>(cubicDocument["bufferViews"].size());
	const UInt32 valueView = timeView + 1;
	const UInt32 timeAccessor = static_cast<UInt32>(cubicDocument["accessors"].size());
	const UInt32 valueAccessor = timeAccessor + 1;
	const UInt32 cubicBuffer = static_cast<UInt32>(cubicDocument["buffers"].size());
	cubicDocument["buffers"].push_back({{"uri", "cubic.bin"}, {"byteLength", cubicBytes.size()}});
	cubicDocument["bufferViews"].push_back({{"buffer", cubicBuffer}, {"byteOffset", 0}, {"byteLength", timeBytes}});
	cubicDocument["bufferViews"].push_back(
	    {{"buffer", cubicBuffer}, {"byteOffset", timeBytes}, {"byteLength", cubicBytes.size() - timeBytes}});
	cubicDocument["accessors"].push_back(
	    {{"bufferView", timeView}, {"componentType", 5126}, {"count", cubicTimes.size()}, {"type", "SCALAR"}});
	cubicDocument["accessors"].push_back(
	    {{"bufferView", valueView}, {"componentType", 5126}, {"count", cubicRotationValues.size()}, {"type", "VEC4"}});
	cubicDocument["animations"][0]["samplers"][0] = {
	    {"input", timeAccessor}, {"output", valueAccessor}, {"interpolation", "CUBICSPLINE"}};
	Text(files, "prj:models/CubicSpline.gltf", cubicDocument.dump());
	Vector<UInt8> cubicResource;
	cubicResource.reserve(cubicBytes.size());
	for (UInt8 byte : cubicBytes)
	{
		cubicResource.push_back(byte);
	}
	Done(files.WriteAtomic(Path("prj:models/cubic.bin"), cubicResource));
	Done(database->Refresh());
	auto cubicSpline = ModelImporter::BuildCandidate(*database, Path("prj:models/CubicSpline.gltf"), settings);
	Expect(bool(cubicSpline) && cubicSpline.value.animationOrder.size() == 1,
	       "CUBICSPLINE glTF animation failed to import: " + (cubicSpline ? String{} : cubicSpline.error));
	const auto& cubicClip = cubicSpline.value.archives.at(cubicSpline.value.animationOrder.front()).document;
	const auto& cubicChannel = cubicClip.at("channels").at(0);
	Expect(cubicClip.at("version") == 2 && cubicClip.at("durationSeconds") == 2.0 &&
	           cubicChannel.at("rotationInterpolation") == "CUBICSPLINE" &&
	           cubicChannel.at("rotationKeys").size() == 3 && cubicChannel.at("rotationKeys")[1][0] == 0.5 &&
	           std::abs(cubicChannel.at("rotationKeys")[1][1][2].get<Float32>() - 0.382f) < 0.002f &&
	           std::abs(cubicChannel.at("rotationKeys")[1][3][3].get<Float32>() - 0.25f) < 0.002f,
	       "CUBICSPLINE import lost authored key times, quaternion values, tangents, or interpolation mode");
	const UInt64 cubicClipId = cubicSpline.value.animationOrder.front();
	const auto encodedCubic = Take(cubicSpline.value.Encode());
	const auto clipArtifact =
	    std::find_if(encodedCubic.begin(), encodedCubic.end(), [cubicClipId](const ImportedArtifact& artifact)
	                 { return artifact.localFileId == cubicClipId && artifact.kind == "AnimationClip"; });
	Expect(clipArtifact != encodedCubic.end(), "Candidate encoding omitted the CUBICSPLINE animation artifact");
	const VirtualPath roundTripPath = Path("lib:AnimationRoundTrip/CubicSpline.bin");
	Done(files.WriteAtomic(roundTripPath, clipArtifact->bytes));
	auto roundTripResources = Take(ResourceSession::Open(
	    files,
	    [&](ResourceId guid, UInt64 localId)
	    {
		    return guid == cubicSpline.value.guid && localId == cubicClipId
		               ? Result<VirtualPath>::Success(roundTripPath)
		               : Result<VirtualPath>::Failure("Unexpected CUBICSPLINE round-trip asset reference");
	    }));
	const auto restoredCubic = Take(roundTripResources->LoadAnimationClip(cubicSpline.value.guid, cubicClipId));
	Expect(restoredCubic->channels[0].rotationInterpolation == AnimationInterpolation::CubicSpline &&
	           restoredCubic->channels[0].rotations.size() == 3 &&
	           std::abs(restoredCubic->channels[0].rotations[1].value.z - 0.382f) < 0.001f &&
	           std::abs(restoredCubic->channels[0].rotations[1].outTangent.w - 0.25f) < 0.001f,
	       "Animation v2 candidate encode and ResourceSession load did not round-trip cubic keys");
}

void StableGltfNodeIdentity()
{
	auto files = Files();
	auto document = nlohmann::json::parse(FixtureText(std::filesystem::u8path(URSA_CANDIDATE_ANIMATED_GLTF)));
	Expect(document["nodes"].size() >= 3, "Animated glTF identity fixture has too few nodes");
	document["nodes"][0]["name"] = "Duplicate";
	document["nodes"][1]["name"] = "Duplicate";
	document["nodes"][2].erase("name");
	const std::string source = document.dump();
	Text(files, "prj:StableNodes.gltf", source);
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	auto first = ModelImporter::BuildCandidate(*database, Path("prj:StableNodes.gltf"), {});
	Expect(bool(first), "Duplicate or unnamed glTF nodes were not normalized: " + (first ? String{} : first.error));
	Set<UInt64> stableIds;
	bool duplicateDisplayName = false;
	bool unnamedDisplayFallback = false;
	for (const auto& node : first.value.nodes)
	{
		stableIds.insert(node.localFileId);
		duplicateDisplayName = duplicateDisplayName || node.name == "Duplicate";
		unnamedDisplayFallback = unnamedDisplayFallback || node.name == "Node 2";
	}
	Expect(stableIds.size() == document["nodes"].size() && duplicateDisplayName && unnamedDisplayFallback,
	       "glTF source node indices or display names were not preserved uniquely");
	const auto before = Take(files.Read(Path("prj:StableNodes.gltf")));
	Expect(std::string(reinterpret_cast<const char*>(before.data()), before.size()) == source,
	       "glTF node normalization modified the source document");
	document["nodes"][0]["name"] = "Renamed Root";
	Text(files, "prj:StableNodes.gltf", document.dump());
	Done(database->Refresh());
	auto renamed = ModelImporter::BuildCandidate(*database, Path("prj:StableNodes.gltf"), {});
	Expect(bool(renamed), "Renamed glTF display node failed to import");
	Set<UInt64> renamedIds;
	bool renamedDisplayName = false;
	for (const auto& node : renamed.value.nodes)
	{
		renamedIds.insert(node.localFileId);
		renamedDisplayName = renamedDisplayName || node.name == "Renamed Root";
	}
	Expect(renamedIds == stableIds && renamedDisplayName,
	       "Changing a glTF display name changed source-index identity or failed to refresh the display name");
}

void MultipleGltfSkinBindings()
{
	auto files = Files();
	auto document = nlohmann::json::parse(FixtureText(std::filesystem::u8path(URSA_CANDIDATE_ANIMATED_GLTF)));
	document["skins"][0].erase("inverseBindMatrices");
	auto secondSkin = document["skins"][0];
	std::swap(secondSkin["joints"][0], secondSkin["joints"][1]);
	document["skins"].push_back(secondSkin);
	document["nodes"].push_back({{"name", "SecondSkinInstance"}, {"mesh", 0}, {"skin", 1}});
	document["scenes"][0]["nodes"].push_back(static_cast<UInt32>(document["nodes"].size() - 1));
	Text(files, "prj:MultipleSkins.gltf", document.dump());
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	auto candidate = ModelImporter::BuildCandidate(*database, Path("prj:MultipleSkins.gltf"), {});
	Expect(bool(candidate), "glTF model with a shared mesh and reordered skin joints failed: " +
	                            (candidate ? String{} : candidate.error));
	Vector<Vector<UInt16>> bindings;
	bool identityInverseBind = false;
	for (const auto& entry : candidate.value.meshes)
	{
		const auto& data = entry.second->Snapshot()->data;
		if (data.skeletonReference.guid)
		{
			bindings.push_back(data.skinJointIndices);
			identityInverseBind =
			    identityInverseBind ||
			    (!data.skinInverseBindMatrices.empty() && data.skinInverseBindMatrices[0].At(0, 0) == 1.0f &&
			     data.skinInverseBindMatrices[0].At(1, 1) == 1.0f && data.skinInverseBindMatrices[0].At(2, 2) == 1.0f &&
			     data.skinInverseBindMatrices[0].At(3, 3) == 1.0f);
		}
	}
	Expect(bindings.size() >= 2 && bindings[0] != bindings[1] && identityInverseBind,
	       "Shared glTF meshes did not retain distinct joint bindings or synthesize omitted identity inverse binds");
	auto encoded = Take(candidate.value.Encode());
	Map<UInt64, VirtualPath> encodedPaths;
	for (const auto& artifact : encoded)
	{
		if (artifact.kind != "Mesh" && artifact.kind != "Skeleton")
		{
			continue;
		}
		const std::string extension = artifact.kind == "Mesh" ? ".ursamesh" : ".skeleton";
		const std::string artifactPath = "lib:MultipleSkins/" + std::to_string(artifact.localFileId) + extension;
		const VirtualPath path = Path(artifactPath.c_str());
		Done(files.WriteAtomic(path, artifact.bytes));
		encodedPaths.emplace(artifact.localFileId, path);
	}
	auto resources =
	    Take(ResourceSession::Open(files,
	                               [&](ResourceId, UInt64 localFileId)
	                               {
		                               const auto path = encodedPaths.find(localFileId);
		                               return path == encodedPaths.end()
		                                          ? Result<VirtualPath>::Failure("Missing encoded model artifact")
		                                          : Result<VirtualPath>::Success(path->second);
	                               }));
	for (const auto& entry : candidate.value.meshes)
	{
		const auto& original = entry.second->Snapshot()->data;
		if (!original.skeletonReference.guid)
		{
			continue;
		}
		const auto restored = Take(resources->LoadMesh(candidate.value.guid, entry.first))->Snapshot();
		Expect(restored->data.skinJointIndices == original.skinJointIndices &&
		           restored->data.skinInverseBindMatrices.size() == original.skinInverseBindMatrices.size(),
		       "Mesh v3 serialization overwrote or lost a per-skin binding");
	}
}

void PublicGltfSmoke(const char* sourcePath)
{
	auto files = Files();
	Done(files.WriteAtomic(Path("prj:PublicSample.glb"), Fixture(sourcePath)));
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	auto candidate = ModelImporter::BuildCandidate(*database, Path("prj:PublicSample.glb"), {});
	Expect(bool(candidate), "Public Khronos GLB candidate failed: " + (candidate ? String{} : candidate.error));
	Expect(!candidate.value.meshes.empty() && !candidate.value.skeletons.empty() &&
	           !candidate.value.animationOrder.empty(),
	       "Public Khronos RiggedSimple did not produce meshes, a skeleton and animations");
	Size skinnedMeshes = 0;
	for (const auto& entry : candidate.value.meshes)
	{
		const auto data = entry.second->Snapshot();
		if (data->data.skeletonReference.guid == candidate.value.guid && data->data.skeleton &&
		    !data->data.skinJointIndices.empty() &&
		    data->data.skinInverseBindMatrices.size() == data->data.skinJointIndices.size() &&
		    !data->data.skinInfluences.empty())
		{
			++skinnedMeshes;
		}
	}
	Expect(skinnedMeshes > 0, "Public Khronos RiggedSimple lost its skin binding or vertex weights");
	for (UInt64 animationId : candidate.value.animationOrder)
	{
		const auto& document = candidate.value.archives.at(animationId).document;
		Expect(document.at("version") == 2 && document.at("channels").is_array() && !document.at("channels").empty() &&
		           document.at("durationSeconds").get<Float64>() > 0.0,
		       "Public Khronos animation did not encode as a playable v2 clip");
	}
	const auto encoded = Take(candidate.value.Encode());
	Expect(encoded.size() >= candidate.value.meshOrder.size() + candidate.value.animationOrder.size(),
	       "Public Khronos model candidate did not encode all imported sub-assets");
	std::cout << "Public glTF validation passed: " << candidate.value.nodes.size() << " nodes, "
	          << candidate.value.meshes.size() << " meshes, " << skinnedMeshes << " skinned meshes, "
	          << candidate.value.skeletons.size() << " skeletons, " << candidate.value.animationOrder.size()
	          << " animations; " << encoded.size() << " artifacts encoded.\n";
}

void TestSkinInfluenceSelection()
{
	const auto normalized =
	    Take(ModelImporter::NormalizeSkinInfluences(2, {{0, 7, 0.1f}, {0, 3, 0.6f}, {0, 5, 0.3f}, {1, 9, 0.0f}}, 2));
	Expect(normalized.size() == 2 && normalized[0].jointIndices[0] == 3 && normalized[0].jointIndices[1] == 5 &&
	           std::abs(normalized[0].weights[0] - 2.0f / 3.0f) < 0.0001f &&
	           std::abs(normalized[0].weights[1] - 1.0f / 3.0f) < 0.0001f && normalized[1].weights[0] == 0.0f,
	       "Skin influences did not keep the highest weights, normalize them and allow an empty vertex");
	Expect(!ModelImporter::NormalizeSkinInfluences(1, {{1, 0, 1.0f}}, 4) &&
	           !ModelImporter::NormalizeSkinInfluences(1, {{0, 65536, 1.0f}}, 4) &&
	           !ModelImporter::NormalizeSkinInfluences(1, {{0, 0, std::numeric_limits<Float32>::infinity()}}, 4),
	       "Invalid skin vertex, joint or weight data was accepted");
}
} // namespace

int main(int argc, char** argv)
{
	try
	{
		if (argc > 2 && std::string(argv[1]) == "--external-glb")
		{
			PublicGltfSmoke(argv[2]);
			return 0;
		}
		if (argc > 1 && std::string(argv[1]) == "--gltf-preflight-only")
		{
			GltfPreflight();
			GltfExternalResources();
			return 0;
		}
		EditableGraph();
		TestSkinInfluenceSelection();
		FbxCandidates(URSA_CANDIDATE_ANIMATED_FBX, "prj:Animated.fbx", true);
		FbxCandidates(URSA_CANDIDATE_EMBEDDED_FBX, "prj:Embedded.fbx", false);
		GltfPreflight();
		GltfExternalResources();
		StableGltfNodeIdentity();
		MultipleGltfSkinBindings();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
