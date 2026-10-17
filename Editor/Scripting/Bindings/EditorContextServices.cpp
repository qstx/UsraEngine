#include <Scripting/Bindings/EditorServices.hpp>
#include <Scripting/Bindings/BindingAssets.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <Serialization/RenderResourceReferences.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <imgui.h>
#include <algorithm>

namespace Ursa::Editor::Bindings
{
namespace
{
using K = B::Wire::Enum_Ursa_Editor_EditorReferenceKind;

Vector<GameObject::Ptr> Objects(EditorInvocation& scope)
{
	Vector<GameObject::Ptr> pending = scope.source.selection.roots;
	if (scope.source.selection.scene)
	{
		pending = scope.source.selection.scene->RootGameObjects();
	}
	if (pending.empty() && scope.source.selection.object)
	{
		auto root = scope.source.selection.object;
		while (root->Parent())
		{
			root = root->Parent();
		}
		pending.push_back(root);
	}
	Vector<GameObject::Ptr> result;
	while (!pending.empty())
	{
		auto object = std::move(pending.back());
		pending.pop_back();
		if (!object->IsValid())
		{
			continue;
		}
		result.push_back(object);
		for (const auto& child : object->Children())
		{
			pending.push_back(child);
		}
	}
	return result;
}

String PathExtension(const String& path)
{
	const auto dot = path.rfind('.');
	String extension = dot == String::npos ? String{} : path.substr(dot);
	for (char& value : extension)
	{
		if (value >= 'A' && value <= 'Z')
		{
			value += 'a' - 'A';
		}
	}
	return extension;
}

UInt32 AssetKind(const String& path, const String& artifact = {})
{
	const String extension = PathExtension(path);
	if (artifact == "Mesh" || extension == ".ursamesh")
	{
		return K::V_Mesh;
	}
	if (artifact == "Material" || extension == ".ursamat")
	{
		return K::V_Material;
	}
	if (artifact == "Texture" || extension == ".ursatex")
	{
		return K::V_Texture;
	}
	if (extension == ".compute")
	{
		return K::V_ComputeShader;
	}
	if (artifact == "Shader" || extension == ".shader" || extension == ".ursashaderasset")
	{
		return K::V_Shader;
	}
	if (artifact == "ScriptableObject" || artifact == "ScriptableAsset")
	{
		return K::V_ScriptableObject;
	}
	return K::V_Asset;
}
} // namespace

EditorInvocation::EditorInvocation(ManagedEditorContext& editorContext, UInt64 scriptSession)
    : source(editorContext), session(scriptSession)
{
	if (source.target)
	{
		targetId = source.target->PersistentId();
		targetOwner = source.target->GetGameObject();
		attachedTarget = !targetOwner.expired();
		if (!source.registry || !source.selection.loadContext)
		{
			throw std::runtime_error("Inspector context has no serialization services");
		}
		properties = B::TakeBindingResult(SerializedObject::Create(*source.target, *source.registry,
		                                                           *source.selection.loadContext, source.saveContext));
	}
}

EditorInvocation::~EditorInvocation()
{
	Deactivate();
	while (groups > 0)
	{
		if (ImGui::GetCurrentContext())
		{
			ImGui::EndGroup();
		}
		--groups;
	}
}

void EditorInvocation::Require(UInt32 policy) const
{
	B::BindingContext::Require(policy);
	if (source.document && source.documents && source.documents->Current() != source.document)
	{
		throw std::runtime_error("The editing document changed or closed during this callback");
	}
	if (attachedTarget)
	{
		const auto owner = targetOwner.lock();
		if (!owner || !owner->IsValid())
		{
			throw std::runtime_error("The serialized target was destroyed");
		}
		const auto components = owner->Components();
		const auto found = std::find_if(components.begin(), components.end(), [&](const auto* value)
		                                { return value == source.target && value->PersistentId() == targetId; });
		if (found == components.end())
		{
			throw std::runtime_error("The serialized target was removed or replaced");
		}
	}
}

UInt64 EditorInvocation::SessionIdentity() const
{
	return session;
}

UInt64 EditorInvocation::TransferBudget() const
{
	return 16ull * 1024 * 1024;
}

UInt64 EditorInvocation::Generation() const
{
	return UInt64(reinterpret_cast<UIntPtr>(Token()));
}

void EditorInvocation::RequireProperties(UInt64 expected) const
{
	if (!properties || revision != expected)
	{
		throw std::runtime_error("SerializedObject snapshot is absent or stale; call Update");
	}
}

ResourceSession& EditorInvocation::Resources()
{
	if (source.selection.loadContext)
	{
		return source.selection.loadContext->Resources();
	}
	if (source.documents)
	{
		return source.documents->Session();
	}
	if (!fallbackResources && source.workspace)
	{
		fallbackResources = B::TakeBindingResult(
		    ResourceSession::Open(source.workspace->ContentFiles(), source.workspace->AssetResolver()));
	}
	if (!fallbackResources)
	{
		throw std::runtime_error("This editing context has no asset loader");
	}
	return *fallbackResources;
}

EditorInvocation& EditorServices::Scope(B::BindingCall& call)
{
	auto* scope = dynamic_cast<EditorInvocation*>(&call.context);
	if (!scope)
	{
		throw std::runtime_error("This binding requires an active Editor callback");
	}
	return *scope;
}

EditorServices::Reference EditorServices::WriteReference(B::BindingCall& call, const EditorReferenceData& reference)
{
	Reference result;
	result.F_Kind = reference.kind;
	result.F_Scope = Scope(call).Generation();
	result.F_Id = call.arena.StringValue(reference.id);
	result.F_TypeId = call.arena.StringValue(reference.typeId);
	if (reference.hasAsset)
	{
		result.F_Asset.present = 1;
		result.F_Asset.F_value =
		    B::WriteAssetValue<B::Wire::Value_Ursa_NativeAssetReference>(call.arena, reference.asset);
	}
	return result;
}

EditorReferenceData EditorServices::ReadReference(EditorInvocation& scope, const Reference& reference)
{
	if (reference.F_Scope != scope.Generation() || reference.F_Kind == K::V_None ||
	    reference.F_Kind > K::V_ComputeShader)
	{
		throw std::runtime_error("Editor reference belongs to another callback/session or has an invalid kind");
	}
	EditorReferenceData result{reference.F_Kind,
	                           B::ReadString(reference.F_Id),
	                           {},
	                           reference.F_Asset.present != 0,
	                           B::ReadString(reference.F_TypeId)};
	if (result.hasAsset)
	{
		result.asset = B::ReadAssetValue(reference.F_Asset.F_value);
	}
	if ((result.hasAsset && (!result.id.empty() || result.kind <= K::V_Component)) ||
	    (!result.hasAsset && (result.id.empty() || (result.kind > K::V_Component && result.kind != K::V_Mesh &&
	                                                result.kind != K::V_Material))))
	{
		throw std::runtime_error("Editor reference tag does not match its scene, local or asset identity");
	}
	return result;
}

EditorReferenceData EditorServices::Resolve(EditorInvocation& scope, UInt32 kind, String id, AssetReference asset)
{
	if (kind == K::V_GameObject || kind == K::V_Component)
	{
		if (asset.guid)
		{
			throw std::runtime_error("A scene object reference cannot contain an asset identity");
		}
		const auto identity = B::TakeBindingResult(ResourceId::Parse(id));
		if (kind == K::V_Component && scope.source.target && scope.source.target->PersistentId() == identity)
		{
			const auto* script = dynamic_cast<const Scripting::ScriptBehaviourComponent*>(scope.source.target);
			return {kind,
			        identity.ToString(),
			        {},
			        false,
			        script ? script->TypeName() : scope.source.target->SerializationType()};
		}
		for (const auto& object : Objects(scope))
		{
			if (kind == K::V_GameObject && object->Id() == identity)
			{
				return {kind, identity.ToString(), {}, false, "ursa.GameObject"};
			}
			if (kind == K::V_Component)
			{
				for (const auto* component : object->Components())
				{
					if (component->PersistentId() == identity)
					{
						const auto* script = dynamic_cast<const Scripting::ScriptBehaviourComponent*>(component);
						return {kind,
						        identity.ToString(),
						        {},
						        false,
						        script ? script->TypeName() : component->SerializationType()};
					}
				}
			}
		}
		throw std::runtime_error("Editor object/component reference does not resolve in this editing graph");
	}
	if (kind <= K::V_Component || kind > K::V_ComputeShader)
	{
		throw std::runtime_error("Unknown Editor reference kind");
	}
	if (asset.guid && !id.empty())
	{
		throw std::runtime_error("An asset identity cannot also contain a document-local ID");
	}
	if (!asset.guid)
	{
		if ((kind != K::V_Mesh && kind != K::V_Material) || id.empty() || !scope.source.selection.loadContext)
		{
			throw std::runtime_error("An asset reference requires a persistent GUID or a document-local Mesh/Material");
		}
		B::TakeBindingResult(ResourceId::Parse(id));
		const auto reader = B::TakeBindingResult(
		    ArchiveFromJson(nlohmann::json{{"value", {{"kind", "local"}, {"id", ToStdString(id)}}}}));
		if (kind == K::V_Mesh)
		{
			B::TakeBindingResult(ReadMeshBinding(reader, "value", *scope.source.selection.loadContext));
		}
		else
		{
			B::TakeBindingResult(ReadMaterialBinding(reader, "value", *scope.source.selection.loadContext));
		}
		return {kind, std::move(id), {}, false, kind == K::V_Mesh ? "Mesh" : "Material"};
	}
	auto& resources = scope.Resources();
	String type;
	if (kind == K::V_Mesh)
	{
		B::TakeBindingResult(resources.LoadMesh(asset.guid, asset.localFileId));
		type = "Mesh";
	}
	else if (kind == K::V_Material)
	{
		B::TakeBindingResult(resources.LoadMaterial(asset.guid, asset.localFileId));
		type = "Material";
	}
	else if (kind == K::V_Texture)
	{
		B::TakeBindingResult(resources.LoadTexture(asset.guid, asset.localFileId));
		type = "Texture";
	}
	else if (kind == K::V_Shader || kind == K::V_ComputeShader)
	{
		if (asset.localFileId)
		{
			throw std::runtime_error("Shader sub-assets are not supported");
		}
		const auto shader = B::TakeBindingResult(resources.LoadShader(asset.guid));
		bool hasCompute = false;
		bool hasGraphics = false;
		for (const auto& subShader : shader->subShaders)
		{
			for (const auto& pass : subShader.passes)
			{
				hasCompute = hasCompute || pass.computeKernel;
				hasGraphics = hasGraphics || !pass.computeKernel;
			}
		}
		if ((kind == K::V_ComputeShader && !hasCompute) || (kind == K::V_Shader && !hasGraphics))
		{
			throw std::runtime_error("Shader asset does not match the declared reference kind");
		}
		type = shader->name;
	}
	else if (kind == K::V_ScriptableObject)
	{
		const auto graph = B::TakeBindingResult(LoadScriptableAssetGraph(resources, asset));
		const auto root =
		    std::find_if(graph.assets.begin(), graph.assets.end(), [&](const auto& value)
		                 { return value.SourceGuid() == asset.guid && value.LocalFileId() == asset.localFileId; });
		if (root == graph.assets.end())
		{
			throw std::runtime_error("ScriptableObject root is missing from its asset graph");
		}
		type = root->TypeId();
	}
	else
	{
		B::TakeBindingResult(resources.ResolveAsset(asset.guid, asset.localFileId));
	}
	return {kind, {}, asset, true, std::move(type)};
}

Vector<EditorAssetData> EditorServices::AssetChoices(EditorInvocation& scope)
{
	Vector<EditorAssetData> result;
	Set<String> identities;
	const auto append = [&](AssetReference asset, UInt32 kind, const String& path, const String& type)
	{
		const String key = asset.guid.ToString() + ":" + ToUrsaString(std::to_string(asset.localFileId));
		if (!identities.insert(key).second)
		{
			return;
		}
		result.push_back(
		    {{kind, {}, asset, true, type},
		     path,
		     path + (asset.localFileId ? " / " + type + " #" + ToUrsaString(std::to_string(asset.localFileId)) : "")});
	};
	if (scope.source.workspace)
	{
		Vector<AssetRecord> assets;
		Vector<ImportedArtifactRecord> artifacts;
		for (const auto* database : {scope.source.workspace->Database(), scope.source.workspace->BuiltinDatabase()})
		{
			if (!database)
			{
				continue;
			}
			for (const auto& asset : database->Assets())
			{
				assets.push_back(asset);
			}
			for (const auto& artifact : database->ImportedArtifacts())
			{
				artifacts.push_back(artifact);
			}
		}
		for (const auto& source : assets)
		{
			if (source.metadata.folder)
			{
				continue;
			}
			const auto root = std::find_if(artifacts.begin(), artifacts.end(), [&](const auto& value)
			                               { return value.guid == source.metadata.guid && value.localFileId == 0; });
			const String path = source.path.ToString();
			append({source.metadata.guid, 0}, AssetKind(path, root == artifacts.end() ? String{} : root->kind), path,
			       root == artifacts.end() ? String{} : root->kind);
		}
		for (const auto& artifact : artifacts)
		{
			const auto source = std::find_if(assets.begin(), assets.end(),
			                                 [&](const auto& value) { return value.metadata.guid == artifact.guid; });
			const String path = source == assets.end() ? artifact.path.ToString() : source->path.ToString();
			append({artifact.guid, artifact.localFileId}, AssetKind(path, artifact.kind), path, artifact.kind);
		}
		for (const auto& entry : scope.source.workspace->Resources())
		{
			append({entry.id, 0}, AssetKind(entry.location.path.ToString()), entry.location.path.ToString(), {});
		}
	}
	else if (scope.source.selection.loadContext || scope.source.documents)
	{
		for (const auto& entry : scope.Resources().Index().Entries())
		{
			append({entry.id, 0}, AssetKind(entry.location.path.ToString()), entry.location.path.ToString(), {});
		}
	}
	return result;
}

Result<B::Wire::Value_Ursa_Editor_EditorScopeValue> EditorServices::GetScope(B::BindingCall& call)
{
	const auto& scope = Scope(call);
	return Result<B::Wire::Value_Ursa_Editor_EditorScopeValue>::Success(
	    {scope.Generation(), scope.SessionIdentity(), scope.revision, scope.properties ? 1u : 0u});
}

Result<B::DataView> EditorServices::GetSelection(B::BindingCall& call)
{
	auto& scope = Scope(call);
	Vector<B::Wire::Value_Ursa_Editor_EditorSelectionValue> values;
	if (scope.source.assetSelection)
	{
		const auto selected = *scope.source.assetSelection;
		const auto choices = AssetChoices(scope);
		const auto asset = std::find_if(choices.begin(), choices.end(),
		                                [&](const auto& value)
		                                {
			                                return value.reference.asset.guid == selected.guid &&
			                                       value.reference.asset.localFileId == selected.localFileId;
		                                });
		const EditorReferenceData reference =
		    asset == choices.end() ? EditorReferenceData{K::V_Asset, {}, selected, true, {}} : asset->reference;
		values.push_back({WriteReference(call, reference), call.arena.StringValue(scope.source.assetSelectionName)});
	}
	else if (const auto object = scope.source.selection.object)
	{
		values.push_back({WriteReference(call, Resolve(scope, K::V_GameObject, object->Id().ToString())),
		                  call.arena.StringValue(B::TakeBindingResult(object->Name()))});
	}
	return Result<B::DataView>::Success(call.arena.Array(values.data(), values.size()));
}

Result<EditorServices::OptionalReference> EditorServices::GetTarget(B::BindingCall& call)
{
	auto& scope = Scope(call);
	OptionalReference value;
	if (scope.source.target)
	{
		value.present = 1;
		value.F_value =
		    WriteReference(call, Resolve(scope, K::V_Component, scope.source.target->PersistentId().ToString()));
	}
	return Result<OptionalReference>::Success(value);
}

Result<B::Wire::Value_Ursa_Editor_EditorDocumentValue_> EditorServices::GetDocument(B::BindingCall& call)
{
	const auto& scope = Scope(call);
	B::Wire::Value_Ursa_Editor_EditorDocumentValue_ value;
	if (const auto document = scope.source.document)
	{
		using D = B::Wire::Enum_Ursa_Editor_EditorDocumentKind;
		UInt32 kind = D::V_Unknown;
		switch (document->Kind())
		{
			case ResourceKind::Scene:
				kind = D::V_Scene;
				break;
			case ResourceKind::Prefab:
				kind = D::V_Prefab;
				break;
			case ResourceKind::Texture:
				kind = D::V_Texture;
				break;
			case ResourceKind::Material:
				kind = D::V_Material;
				break;
			case ResourceKind::Shader:
				kind = D::V_Shader;
				break;
			case ResourceKind::Mesh:
				kind = D::V_Mesh;
				break;
		}
		value.present = 1;
		value.F_value = {call.arena.StringValue(document->Id().ToString()),
		                 call.arena.StringValue(document->Path().ToString()), kind, document->Dirty() ? 1u : 0u};
	}
	return Result<B::Wire::Value_Ursa_Editor_EditorDocumentValue_>::Success(value);
}

Result<B::DataView> EditorServices::GetAssets(B::BindingCall& call)
{
	return Result<B::DataView>::Success(B::WriteArray<B::Wire::Value_Ursa_Editor_EditorAssetValue>(
	    call.arena, AssetChoices(Scope(call)),
	    [&](const auto& value)
	    {
		    return B::Wire::Value_Ursa_Editor_EditorAssetValue{WriteReference(call, value.reference),
		                                                       call.arena.StringValue(value.path),
		                                                       call.arena.StringValue(value.name)};
	    }));
}

Result<EditorServices::Reference> EditorServices::ResolveReference(B::BindingCall& call, UInt32 kind,
                                                                   const B::Utf8View& id, const OptionalAsset& asset)
{
	return Result<Reference>::Success(
	    WriteReference(call, Resolve(Scope(call), kind, B::ReadString(id),
	                                 asset.present ? B::ReadAssetValue(asset.F_value) : AssetReference{})));
}

Result<void> EditorServices::MarkDirty(B::BindingCall& call)
{
	const auto& scope = Scope(call);
	if (!scope.source.document)
	{
		return Result<void>::Failure("No editing document is open");
	}
	scope.source.document->MarkDirty();
	return {};
}

Result<void> EditorServices::Save(B::BindingCall& call)
{
	const auto& scope = Scope(call);
	return scope.source.documents ? scope.source.documents->SaveCurrent()
	                              : Result<void>::Failure("No document service is available");
}

Result<void> EditorServices::OpenAsset(B::BindingCall& call, const B::Utf8View& path)
{
	const auto& scope = Scope(call);
	const auto parsed = VirtualPath::Parse(B::ReadString(path));
	if (!parsed || !scope.source.openAsset)
	{
		return Result<void>::Failure("Asset opening is unavailable or the path is invalid");
	}
	return scope.source.openAsset(parsed.value);
}
} // namespace Ursa::Editor::Bindings
