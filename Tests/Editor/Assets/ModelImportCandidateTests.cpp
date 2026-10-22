#include <Assets/ModelImporter.hpp>
#include <Assets/ModelImportScene.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Assets/AssetImportRules.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <algorithm>
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
	Expect(bool(input) && input.tellg() > 0, "Model candidate FBX fixture is missing");
	Vector<UInt8> bytes(static_cast<Size>(input.tellg()));
	input.seekg(0);
	input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Expect(bool(input), "Model candidate FBX fixture could not be read");
	return bytes;
}

void EditableGraph()
{
	auto files = Files();
	Text(files, "prj:Example.obj", "mtllib Example.mtl\no Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Red\nf 1 2 3\n");
	Text(files, "prj:Example.mtl", "newmtl Red\nKd 1 0 0\n");
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	const auto source = Path("prj:Example.obj");
	auto candidate = Take(ModelImporter::BuildCandidate(*database, source, {}));
	Expect(!candidate.nodes.empty() && candidate.meshes.size() == 1 && !candidate.materials.empty() &&
	           !Take(files.Exists(Path("lib:AssetDatabase/prj.json"))),
	       "Model construction encoded or published the primary model before postprocessing");
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
	candidate.nodes.push_back(std::move(child));
	auto positions = candidate.meshes.at(meshId)->Snapshot()->data.positions;
	positions[0].z = 2;
	Done(candidate.meshes.at(meshId)->SetVertices(std::move(positions)));
	Done(candidate.materials.at(materialId).material->SetBaseColor({.1f, .7f, .2f, 1}));
	const auto outputs = Take(candidate.Encode());
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
} // namespace

int main()
{
	try
	{
		EditableGraph();
		FbxCandidates(URSA_CANDIDATE_ANIMATED_FBX, "prj:Animated.fbx", true);
		FbxCandidates(URSA_CANDIDATE_EMBEDDED_FBX, "prj:Embedded.fbx", false);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
