#include <Assets/ModelImportScene.hpp>
#include <Assets/AssetImportRules.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Core/StringInterop.hpp>
#include <stdexcept>

namespace Ursa::Editor
{
namespace
{
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
} // namespace

ModelImportScene::ModelImportScene(ModelImportCandidate& candidate, Scene& scene) : candidate(candidate), scene(scene)
{
}

UInt64 ModelImportScene::OutputId(const String& key)
{
	const auto id = Take(StableImportObjectId("ursa.Model", key));
	const auto found = candidate.outputKeys.find(id);
	if (found != candidate.outputKeys.end() && found->second != key)
	{
		throw std::runtime_error("Model postprocess output identity collision");
	}
	candidate.outputKeys[id] = key;
	return id;
}

Result<GameObject::Ptr> ModelImportScene::Build()
{
	try
	{
		Done(candidate.Validate());
		if (root || !scene.RootGameObjects().empty())
		{
			throw std::runtime_error("Model view requires an empty private scene");
		}
		Map<UInt64, GameObject::Ptr> nodes;
		for (const auto& node : candidate.nodes)
		{
			auto object = Take(scene.CreateGameObject(node.name));
			Done(object->SetLocalTransform(node.transform));
			nodes.emplace(node.localFileId, object);
			originalObjects.push_back(object);
			nodeIds.emplace(object.get(), node.localFileId);
		}
		root = nodes.at(candidate.rootNodeLocalFileId);
		for (const auto& material : candidate.materials)
		{
			materialIds.emplace(material.second.material.get(), material.first);
			if (!material.second.material->Shader() &&
			    (material.second.material->ShaderId() == Shader::BuiltinPbrShaderId() ||
			     material.second.material->ShaderId() == Shader::BuiltinUnlitShaderId()))
			{
				auto schema = material.second.material->ShaderId() == Shader::BuiltinPbrShaderId()
				                  ? Shader::CreateBuiltinPbrShaderAsset()
				                  : Shader::CreateBuiltinUnlitShaderAsset();
				Done(material.second.material->SetShaderAsset(
				    std::make_shared<const Shader::ShaderAsset>(std::move(schema))));
			}
			for (const auto& texture : material.second.textures)
			{
				MaterialTexturePtr pixels;
				if (texture.second.guid == candidate.guid)
				{
					pixels = candidate.textures.at(texture.second.localFileId);
				}
				Done(material.second.material->SetTextureReference(texture.first, texture.second, pixels));
			}
		}
		for (const auto& mesh : candidate.meshes)
		{
			meshIds.emplace(mesh.second.get(), mesh.first);
		}
		for (const auto& node : candidate.nodes)
		{
			const auto object = nodes.at(node.localFileId);
			if (node.parentLocalFileId)
			{
				Done(object->SetParent(nodes.at(node.parentLocalFileId)));
			}
			Size ordinal = 0;
			for (const auto meshId : node.meshes)
			{
				const auto mesh = candidate.meshes.at(meshId);
				const String key = "postprocess:mesh-node:" + candidate.outputKeys.at(node.localFileId) + ":" +
				                   ToUrsaString(std::to_string(ordinal++));
				auto host = Take(scene.CreateGameObject("Mesh", object));
				originalObjects.push_back(host);
				nodeIds.emplace(host.get(), OutputId(key));
				RendererComponent* renderer = nullptr;
				if (mesh->Snapshot()->data.skeleton)
				{
					auto* skinned = Take(host->AddComponent<SkinnedMeshRendererComponent>());
					Done(skinned->SetSharedMesh(mesh));
					renderer = skinned;
				}
				else
				{
					Done(Take(host->AddComponent<MeshFilterComponent>())->SetSharedMesh(mesh));
					renderer = Take(host->AddComponent<MeshRendererComponent>());
				}
				const auto binding = candidate.meshMaterialBindings.find(meshId);
				if (binding != candidate.meshMaterialBindings.end())
				{
					Vector<std::shared_ptr<MaterialInstance>> materials;
					for (const auto material : binding->second)
					{
						materials.push_back(material ? candidate.materials.at(material).material : nullptr);
					}
					Done(renderer->SetSharedMaterials(std::move(materials)));
				}
			}
		}
		return Result<GameObject::Ptr>::Success(root);
	}
	catch (const std::exception& error)
	{
		return Result<GameObject::Ptr>::Failure(ToUrsaString(error.what()));
	}
}

UInt64 ModelImportScene::MaterialId(const std::shared_ptr<MaterialInstance>& material, const String& key)
{
	if (!material)
	{
		return 0;
	}
	if (!material->IsValid())
	{
		throw std::runtime_error("Model postprocessor assigned a destroyed material");
	}
	const auto known = materialIds.find(material.get());
	if (known != materialIds.end())
	{
		return known->second;
	}
	if (material->AssetIdentity().guid)
	{
		throw std::runtime_error("Model postprocessor must clone an external material before publishing it");
	}
	const auto id = OutputId(key);
	if (candidate.materials.count(id))
	{
		throw std::runtime_error("Model postprocess material key is ambiguous");
	}
	Done(material->BindAssetIdentity({candidate.guid, id}));
	candidate.materials.emplace(id, ModelCandidateMaterial{id, key, material, {}});
	materialIds.emplace(material.get(), id);
	return id;
}

UInt64 ModelImportScene::MeshId(const MeshPtr& mesh, const String& key)
{
	if (!mesh || !mesh->IsValid())
	{
		throw std::runtime_error("Model postprocessor assigned a missing or destroyed Mesh");
	}
	const auto known = meshIds.find(mesh.get());
	if (known != meshIds.end())
	{
		return known->second;
	}
	if (mesh->AssetIdentity().guid)
	{
		throw std::runtime_error("Model postprocessor must clone an external Mesh before publishing it");
	}
	const auto id = OutputId(key);
	if (candidate.meshes.count(id))
	{
		throw std::runtime_error("Model postprocess Mesh key is ambiguous");
	}
	Done(mesh->BindAssetIdentity({candidate.guid, id}));
	candidate.meshes.emplace(id, mesh);
	candidate.meshOrder.push_back(id);
	meshIds.emplace(mesh.get(), id);
	return id;
}

Result<void> ModelImportScene::Apply()
{
	try
	{
		if (!root || !scene.Contains(root) || root->Parent())
		{
			throw std::runtime_error("Model postprocessor removed or detached its root");
		}
		Vector<ModelCandidateNode> nodes;
		Map<UInt64, Vector<UInt64>> bindings;
		Set<UInt64> usedNodes;
		std::function<UInt64(const GameObject::Ptr&, UInt64, const String&)> visit;
		visit = [&](const GameObject::Ptr& object, UInt64 parent, const String& path)
		{
			const auto existing = nodeIds.find(object.get());
			const UInt64 id = existing != nodeIds.end() ? existing->second : OutputId("postprocess:node:" + path);
			if (!usedNodes.insert(id).second)
			{
				throw std::runtime_error("Model postprocessor produced ambiguous node keys");
			}
			ModelCandidateNode node{id, parent, Take(object->Name()), Take(object->GetLocalTransform()), {}, {}};
			MeshPtr mesh;
			if (const auto* filter = object->FindComponent<MeshFilterComponent>())
			{
				mesh = filter->SharedMesh();
			}
			if (const auto* skinned = object->FindComponent<SkinnedMeshRendererComponent>())
			{
				mesh = skinned->SharedMesh();
			}
			if (mesh)
			{
				auto meshId = MeshId(mesh, "postprocess:mesh:" + path);
				const auto* renderer = object->FindComponent<RendererComponent>();
				Vector<UInt64> materialSlots;
				if (renderer)
				{
					const auto materials = renderer->SharedMaterials();
					for (Size slot = 0; slot < materials.size(); ++slot)
					{
						materialSlots.push_back(MaterialId(materials[slot], "postprocess:material:" + path + ":" +
						                                                        ToUrsaString(std::to_string(slot))));
					}
				}
				const auto previous = bindings.find(meshId);
				if (previous != bindings.end() && previous->second != materialSlots)
				{
					meshId = MeshId(Take(mesh->Clone()), "postprocess:mesh-binding:" + path);
				}
				bindings[meshId] = std::move(materialSlots);
				node.meshes.push_back(meshId);
			}
			Map<String, UInt64> occurrences;
			for (const auto& child : object->Children())
			{
				const String name = Take(child->Name());
				const String childPath = path + "/" + ToUrsaString(std::to_string(name.size())) + ":" + name + ":" +
				                         ToUrsaString(std::to_string(occurrences[name]++));
				node.children.push_back(visit(child, id, childPath));
			}
			nodes.push_back(std::move(node));
			return id;
		};
		const auto rootId = visit(root, 0, "root");
		for (const auto& old : candidate.nodes)
		{
			if (!usedNodes.count(old.localFileId))
			{
				candidate.outputKeys.erase(old.localFileId);
			}
		}
		candidate.nodes = std::move(nodes);
		candidate.rootNodeLocalFileId = rootId;
		for (const auto& binding : bindings)
		{
			candidate.meshMaterialBindings[binding.first] = binding.second;
		}
		for (auto& entry : candidate.materials)
		{
			entry.second.textures.clear();
			for (UInt32 slot = 0; slot < 5; ++slot)
			{
				const auto type = static_cast<MaterialTextureSlot>(slot);
				const auto reference = entry.second.material->TextureReference(type);
				if (reference.guid)
				{
					entry.second.textures.emplace(type, reference);
				}
			}
		}
		return candidate.Validate();
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Editor
