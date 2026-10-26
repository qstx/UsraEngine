#include <Scripting/Bindings/EditorServices.hpp>
#include <Assets/AssetImporterInspector.hpp>
#include <Assets/MeshAssetEditor.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/ProjectBrowserModel.hpp>
#include <Assets/AssetPreviewExtension.hpp>
#include <Runtime.generated.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/Bindings/BindingAssets.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <Serialization/RenderResourceReferences.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/SkinnedMeshRendererComponent.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <imgui.h>
#include <algorithm>
#include <atomic>

namespace Ursa::Editor::Bindings
{
namespace
{
using K = B::Wire::Enum_Ursa_Editor_EditorReferenceKind;
std::atomic<UInt64> nextEditorGeneration{1};

class EditorRuntimeApi final : public Scripting::ManagedSceneApi
{
public:
	bool PersistentResourcesReadOnly() const override
	{
		return true;
	}

	bool LocalResourceReferencesReadOnly() const override
	{
		return !invocation.source.canEditTarget;
	}

	EditorRuntimeApi(Scene& scene, ResourceSession* resources, EditorInvocation& invocation)
	    : ManagedSceneApi(scene, resources), invocation(invocation)
	{
	}

	void Require(UInt32 policy) const override
	{
		invocation.Require(policy);
		ManagedSceneApi::Require(policy);
	}

	void RequireSceneMutation() const override
	{
		Require(0);
		if (!invocation.source.canEditTarget)
		{
			throw std::runtime_error("This Editor target is read-only");
		}
	}

	void RequireGameExecution() const override
	{
		Require(0);
		throw std::runtime_error("Editor targets cannot access game input, Play, window or untracked file operations");
	}

	void RequireRendering() const override
	{
		Require(0);
		throw std::runtime_error("Editor target access cannot render; use the Preview host");
	}

private:
	EditorInvocation& invocation;
};

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
    : source(editorContext), generation(nextEditorGeneration.fetch_add(1, std::memory_order_relaxed)),
      session(scriptSession)
{
	if (source.serializedProperties)
	{
		properties = source.serializedProperties;
		propertyGroup = source.serializedPropertyGroup;
	}
	else if (source.serializedTarget)
	{
		auto adapter = *source.serializedTarget;
		if (!adapter.resources && (source.workspace || source.documents || source.selection.loadContext))
		{
			adapter.resources = &Resources();
		}
		ownedProperties = B::TakeBindingResult(SerializedObject::Create(std::move(adapter)));
		properties = ownedProperties.get();
	}
	else if (!source.componentTargets.empty())
	{
		if (source.componentTargets.size() > 1 && !source.supportsMultipleObjects)
		{
			throw std::runtime_error("This Inspector has not declared multi-object editing support");
		}
		if (source.componentPrimaryIndex >= source.componentTargets.size() ||
		    source.componentTargets[source.componentPrimaryIndex] != source.target)
		{
			throw std::runtime_error("Managed Inspector primary target does not match its target group");
		}
		if (!source.registry || !source.selection.loadContext)
		{
			throw std::runtime_error("Inspector context has no serialization services");
		}
		targetId = source.target->PersistentId();
		targetOwner = source.target->GetGameObject();
		attachedTarget = !targetOwner.expired();
		Vector<SerializedObject*> groupTargets(source.componentTargets.size(), nullptr);
		for (Size index = 0; index < source.componentTargets.size(); ++index)
		{
			auto* component = source.componentTargets[index];
			if (!component)
			{
				throw std::runtime_error("Managed Inspector target group contains a missing component");
			}
			auto serialized = SerializedObject::Create(*component, *source.registry, *source.selection.loadContext,
			                                           source.saveContext);
			if (!serialized)
			{
				throw std::runtime_error(ToStdString(serialized.error));
			}
			if (index == source.componentPrimaryIndex)
			{
				ownedProperties = std::move(serialized.value);
				properties = ownedProperties.get();
				groupTargets[index] = properties;
			}
			else
			{
				groupTargets[index] = serialized.value.get();
				additionalProperties.push_back(std::move(serialized.value));
			}
		}
		if (groupTargets.size() > 1)
		{
			auto group = SerializedObjectGroup::Create(std::move(groupTargets), source.componentPrimaryIndex);
			if (!group)
			{
				throw std::runtime_error(ToStdString(group.error));
			}
			ownedPropertyGroup = std::make_unique<SerializedObjectGroup>(std::move(group.value));
			propertyGroup = ownedPropertyGroup.get();
		}
	}
	else if (source.target)
	{
		targetId = source.target->PersistentId();
		targetOwner = source.target->GetGameObject();
		attachedTarget = !targetOwner.expired();
		if (!source.registry || !source.selection.loadContext)
		{
			throw std::runtime_error("Inspector context has no serialization services");
		}
		ownedProperties = B::TakeBindingResult(SerializedObject::Create(
		    *source.target, *source.registry, *source.selection.loadContext, source.saveContext));
		properties = ownedProperties.get();
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
	if (source.validateTarget)
	{
		B::CompleteBindingResult(source.validateTarget());
	}
	if (source.targetUsesDocument && source.document && source.documents &&
	    source.documents->Current() != source.document)
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

void EditorInvocation::CheckHandle(const void* value, Size size, UInt32 kind) const
{
	const_cast<EditorInvocation*>(this)->RuntimeApi().CheckHandle(value, size, kind);
}

UInt64 EditorInvocation::TransferBudget() const
{
	return 16ull * 1024 * 1024;
}

UInt64 EditorInvocation::Generation() const
{
	return generation;
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

Scripting::ManagedSceneApi& EditorInvocation::RuntimeApi()
{
	Require(0);
	if (source.preview && !source.previewProperties)
	{
		return source.preview->RuntimeApi();
	}
	if (!runtimeApi)
	{
		ResourceSession* resources =
		    source.workspace || source.documents || source.selection.loadContext ? &Resources() : nullptr;
		runtimeApi = std::make_unique<EditorRuntimeApi>(source.selection.scene ? *source.selection.scene : privateScene,
		                                                resources, *this);
		if (hostToken)
		{
			B::CompleteBindingResult(runtimeApi->BindHostServices(hostToken));
		}
		if (!source.selection.scene)
		{
			auto roots = source.selection.roots;
			if (roots.empty() && source.selection.object)
			{
				auto root = source.selection.object;
				while (root->Parent())
				{
					root = root->Parent();
				}
				roots.push_back(root);
			}
			if (!roots.empty())
			{
				runtimeApi->SetDetachedRoots(std::move(roots));
			}
		}
	}
	return *runtimeApi;
}

Result<bool> EditorInvocation::SynchronizeRuntimeTarget()
{
	if (!runtimeMaterial)
	{
		return Result<bool>::Success(false);
	}
	const auto current = MaterialDraft::DescribeProperties(*runtimeMaterial);
	if (!current)
	{
		return Result<bool>::Failure(current.error);
	}
	if (current.value.at("Shader") != runtimeMaterialBaseline.at("Shader"))
	{
		return Result<bool>::Failure("Changing the target Shader requires reopening its source Inspector schema");
	}
	nlohmann::json fields = nlohmann::json::object();
	for (const auto& field : current.value.at("Fields").items())
	{
		if (field.value() != runtimeMaterialBaseline.at("Fields").at(field.key()))
		{
			fields[field.key()] = field.value();
		}
	}
	if (fields.empty())
	{
		return Result<bool>::Success(false);
	}
	if (!source.canEditTarget)
	{
		return Result<bool>::Failure("This material source is read-only");
	}
	if (!properties)
	{
		return Result<bool>::Failure("Material target has no source property transaction");
	}
	const auto applied = properties->ApplyFields(fields);
	if (applied && applied.value)
	{
		changed = true;
		if (source.markTargetDirty)
		{
			source.markTargetDirty();
		}
		else if (source.targetUsesDocument && source.document)
		{
			source.document->MarkDirty();
		}
		runtimeMaterialBaseline = current.value;
	}
	return applied;
}

Result<B::Wire::Value_Ursa_EditorBindingCapability> EditorServices::GetRuntimeCapability(B::BindingCall& call)
{
	auto& api = Scope(call).RuntimeApi();
	return Result<B::Wire::Value_Ursa_EditorBindingCapability>::Success(
	    {B::Runtime::moduleId, reinterpret_cast<UInt64>(&B::Runtime::NativeDescriptor()),
	     reinterpret_cast<UInt64>(api.Token())});
}

Result<B::Wire::Value_Ursa_NativeHandle> EditorServices::GetTargetHandle(B::BindingCall& call)
{
	auto& scope = Scope(call);
	auto& api = scope.RuntimeApi();
	if (scope.source.targetMesh)
	{
		return Result<B::Wire::Value_Ursa_NativeHandle>::Success(
		    B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(
		        B::TakeBindingResult(api.RegisterMesh(scope.source.targetMesh))));
	}
	if (scope.source.targetTexture)
	{
		return Result<B::Wire::Value_Ursa_NativeHandle>::Success(
		    B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(
		        B::TakeBindingResult(api.RegisterTexture(scope.source.targetTexture))));
	}
	if (scope.source.target)
	{
		return Result<B::Wire::Value_Ursa_NativeHandle>::Success(
		    B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(
		        B::TakeBindingResult(api.RegisterComponent(scope.source.target))));
	}
	if (scope.source.targetMaterial)
	{
		if (scope.source.preview && !scope.source.previewProperties)
		{
			return Result<B::Wire::Value_Ursa_NativeHandle>::Success(
			    B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(
			        B::TakeBindingResult(api.RegisterMaterial(scope.source.targetMaterial))));
		}
		if (!scope.runtimeMaterial)
		{
			scope.runtimeMaterial = scope.source.targetMaterial->Clone();
			if (!scope.runtimeMaterial)
			{
				return Result<B::Wire::Value_Ursa_NativeHandle>::Failure("Material target was destroyed");
			}
			if (scope.source.assetSelection)
			{
				B::CompleteBindingResult(scope.runtimeMaterial->BindAssetIdentity(*scope.source.assetSelection));
			}
			scope.runtimeMaterialBaseline =
			    B::TakeBindingResult(MaterialDraft::DescribeProperties(*scope.runtimeMaterial));
		}
		return Result<B::Wire::Value_Ursa_NativeHandle>::Success(
		    B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(
		        B::TakeBindingResult(api.RegisterMaterial(scope.runtimeMaterial, scope.source.canEditTarget))));
	}
	if (scope.source.selection.object)
	{
		return Result<B::Wire::Value_Ursa_NativeHandle>::Success(
		    B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(
		        B::TakeBindingResult(api.Register(scope.source.selection.object))));
	}
	return Result<B::Wire::Value_Ursa_NativeHandle>::Failure("No typed native Editor target is active");
}

Result<B::DataView> EditorServices::GetTargetHandles(B::BindingCall& call)
{
	auto& scope = Scope(call);
	Vector<B::Wire::Value_Ursa_NativeHandle> handles;
	if (!scope.source.componentTargets.empty())
	{
		handles.reserve(scope.source.componentTargets.size());
		for (Component* component : scope.source.componentTargets)
		{
			if (!component)
			{
				return Result<B::DataView>::Failure("Inspector target group contains a missing component");
			}
			handles.push_back(B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(
			    B::TakeBindingResult(scope.RuntimeApi().RegisterComponent(component))));
		}
	}
	else if (!scope.source.gameObjectTargets.empty())
	{
		handles.reserve(scope.source.gameObjectTargets.size());
		for (const auto& object : scope.source.gameObjectTargets)
		{
			if (!object)
			{
				return Result<B::DataView>::Failure("Inspector target group contains a missing GameObject");
			}
			handles.push_back(B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(
			    B::TakeBindingResult(scope.RuntimeApi().Register(object))));
		}
	}
	else
	{
		auto handle = GetTargetHandle(call);
		if (!handle)
		{
			return Result<B::DataView>::Failure(handle.error);
		}
		handles.push_back(handle.value);
	}
	return Result<B::DataView>::Success(
	    B::WriteArray<B::Wire::Value_Ursa_NativeHandle>(call.arena, handles, [](const auto& value) { return value; }));
}

Result<UInt32> EditorServices::GetTargetPrimaryIndex(B::BindingCall& call)
{
	const auto& scope = Scope(call);
	if (!scope.source.gameObjectTargets.empty())
	{
		return Result<UInt32>::Success(static_cast<UInt32>(scope.source.gameObjectPrimaryIndex));
	}
	return Result<UInt32>::Success(
	    scope.source.componentTargets.empty() ? 0 : static_cast<UInt32>(scope.source.componentPrimaryIndex));
}

Result<B::Utf8View> EditorServices::GetComponentPayload(B::BindingCall& call,
                                                        const B::Wire::Value_Ursa_NativeHandle& component)
{
	const auto candidate =
	    B::TakeBindingResult(Scope(call).RuntimeApi().CandidateResource(B::ReadManagedHandle(component)));
	const auto* script =
	    dynamic_cast<const Scripting::ScriptBehaviourComponent*>(static_cast<Component*>(candidate.get()));
	return script && script->HasSerializedPayload()
	           ? Result<B::Utf8View>::Success(call.arena.StringValue(script->SerializedPayload()))
	           : Result<B::Utf8View>::Failure("Component has no typed managed field snapshot");
}

Result<bool> EditorServices::ApplyComponentFields(B::BindingCall& call,
                                                  const B::Wire::Value_Ursa_NativeHandle& component,
                                                  const B::Utf8View& fields)
{
	auto& scope = Scope(call);
	if (!scope.source.canEditTarget)
	{
		return Result<bool>::Failure("This Editor target is read-only");
	}
	const auto candidate = B::TakeBindingResult(scope.RuntimeApi().CandidateResource(B::ReadManagedHandle(component)));
	auto* script = dynamic_cast<Scripting::ScriptBehaviourComponent*>(static_cast<Component*>(candidate.get()));
	if (!script)
	{
		return Result<bool>::Failure("Component is not a managed field target");
	}
	ComponentLoadContext fallback(scope.Resources());
	auto properties = scope.source.registry
	                      ? SerializedObject::Create(
	                            *script, *scope.source.registry,
	                            scope.source.selection.loadContext ? *scope.source.selection.loadContext : fallback,
	                            scope.source.saveContext)
	                      : SerializedObject::Create(*script, &scope.Resources());
	if (!properties)
	{
		return Result<bool>::Failure(properties.error);
	}
	const auto changed = script == scope.source.target && scope.properties
	                         ? scope.properties->ApplyFields(nlohmann::json::parse(ToStdString(B::ReadString(fields))))
	                         : properties.value->ApplyFields(nlohmann::json::parse(ToStdString(B::ReadString(fields))));
	if (changed && changed.value)
	{
		scope.changed = true;
		if (scope.source.markTargetDirty)
		{
			scope.source.markTargetDirty();
		}
		else if (scope.source.targetUsesDocument && scope.source.document)
		{
			scope.source.document->MarkDirty();
		}
	}
	return changed;
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
	if (scope.source.workspace)
	{
		for (const auto& object : AssetObjectChoices(*scope.source.workspace))
		{
			const auto path = object.sourcePath.ToString();
			const auto kind = AssetKind(path, object.kind);
			const AssetReference asset{object.guid, object.localFileId};
			EditorReferenceData reference{kind, {}, asset, true, object.kind};
			if (kind == K::V_ScriptableObject)
			{
				try
				{
					reference = Resolve(scope, kind, {}, asset);
				}
				catch (const std::exception&)
				{
					// Keep the last complete directory entry even when its runtime content cannot be loaded.
				}
			}
			result.push_back({std::move(reference), path, object.Label() + " - " + path, !object.isMainObject});
		}
	}
	else if (scope.source.selection.loadContext || scope.source.documents)
	{
		for (const auto& entry : scope.Resources().Index().Entries())
		{
			const auto path = entry.location.path.ToString();
			result.push_back({{AssetKind(path), {}, {entry.id, 0}, true, {}}, path, path, false});
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

Result<B::Wire::Value_Ursa_Editor_EditorImporterInfo_> EditorServices::GetImporterInfo(B::BindingCall& call)
{
	const auto& scope = Scope(call);
	B::Wire::Value_Ursa_Editor_EditorImporterInfo_ result;
	if (const auto target = scope.source.importerInspector)
	{
		result.present = 1;
		result.F_value.F_Path = call.arena.StringValue(target->SourcePath().ToString());
		result.F_value.F_Identity = call.arena.StringValue(target->Identity());
		result.F_value.F_ClrTypeName = call.arena.StringValue(target->ClrTypeName());
		result.F_value.F_Reference =
		    B::WriteAssetValue<decltype(result.F_value.F_Reference)>(call.arena, target->Reference());
	}
	return Result<B::Wire::Value_Ursa_Editor_EditorImporterInfo_>::Success(result);
}

Result<B::Utf8View> EditorServices::GetTargetPayload(B::BindingCall& call)
{
	auto& scope = Scope(call);
	scope.RequireProperties(scope.revision);
	return Result<B::Utf8View>::Success(call.arena.StringValue(ToUrsaString(scope.properties->Snapshot().dump())));
}

Result<bool> EditorServices::ApplyTargetFields(B::BindingCall& call, const B::Utf8View& fields)
{
	auto& scope = Scope(call);
	scope.RequireProperties(scope.revision);
	if (!scope.source.canEditTarget)
	{
		return Result<bool>::Failure("This Editor target is read-only");
	}
	const auto payload = nlohmann::json::parse(ToStdString(B::ReadString(fields)));
	Result<bool> applied = Result<bool>::Success(false);
	if (scope.propertyGroup && payload.is_object() && payload.contains("Targets"))
	{
		auto targets = payload["Targets"];
		if (!targets.is_array() || targets.size() != scope.propertyGroup->TargetCount())
		{
			return Result<bool>::Failure("Managed Inspector target field patch does not match its target group");
		}
		bool changed = false;
		for (const auto& propertyPath : scope.appliedPropertyPaths)
		{
			constexpr const char* fieldPrefix = "/Fields/";
			if (propertyPath.rfind(fieldPrefix, 0) != 0)
			{
				continue;
			}
			const Size separator = propertyPath.find('/', std::char_traits<char>::length(fieldPrefix));
			const String field = propertyPath.substr(
			    std::char_traits<char>::length(fieldPrefix),
			    separator == String::npos ? String::npos : separator - std::char_traits<char>::length(fieldPrefix));
			for (auto& target : targets)
			{
				if (target.is_object())
				{
					target.erase(ToStdString(field));
				}
			}
		}
		for (Size index = 0; index < targets.size(); ++index)
		{
			auto targetPatch = scope.propertyGroup->SetTargetFields(index, targets[index]);
			if (!targetPatch)
			{
				return Result<bool>::Failure(targetPatch.error);
			}
			changed |= targetPatch.value;
		}
		if (changed)
		{
			auto committed = scope.propertyGroup->Apply();
			if (!committed)
			{
				return Result<bool>::Failure(committed.error);
			}
		}
		applied = Result<bool>::Success(changed);
	}
	else
	{
		applied = scope.properties->ApplyFields(payload);
	}
	if (applied && applied.value)
	{
		scope.changed = true;
		if (scope.source.markTargetDirty)
		{
			scope.source.markTargetDirty();
		}
		else if (scope.source.targetUsesDocument && scope.source.document)
		{
			scope.source.document->MarkDirty();
		}
	}
	return applied;
}

Result<bool> EditorServices::HasModifiedImporterSettings(B::BindingCall& call)
{
	const auto target = Scope(call).source.importerInspector;
	return target ? Result<bool>::Success(target->HasModified())
	              : Result<bool>::Failure("No importer Inspector target is active");
}

Result<bool> EditorServices::CanApplyImporterSettings(B::BindingCall& call)
{
	const auto& scope = Scope(call);
	return scope.source.importerInspector ? Result<bool>::Success(scope.source.canApplyImporter)
	                                     : Result<bool>::Failure("No importer Inspector target is active");
}

Result<bool> EditorServices::ApplyImporterSettings(B::BindingCall& call)
{
	auto& scope = Scope(call);
	const auto target = scope.source.importerInspector;
	if (!target || !scope.source.canApplyImporter)
	{
		return Result<bool>::Failure("Importer Apply is unavailable in this callback");
	}
	const auto properties = ApplyProperties(call, scope.revision);
	if (!properties)
	{
		return Result<bool>::Failure(properties.error);
	}
	auto applied = target->Apply();
	if (applied && applied.value && scope.source.requestImport)
	{
		scope.source.requestImport(target->SourcePath());
	}
	return applied;
}

Result<void> EditorServices::RevertImporterSettings(B::BindingCall& call)
{
	const auto target = Scope(call).source.importerInspector;
	return target ? target->Revert() : Result<void>::Failure("No importer Inspector target is active");
}

Result<void> EditorServices::MarkImporterControlsDrawn(B::BindingCall& call)
{
	RequireGui(call);
	auto& context = Scope(call).source;
	if (!context.importerInspector)
	{
		return Result<void>::Failure("No importer Inspector target is active");
	}
	context.importerControlsDrawn = true;
	return {};
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
	else if (scope.source.serializedTarget && scope.source.assetSelection)
	{
		value.present = 1;
		if (scope.source.targetMaterial)
		{
			value.F_value =
			    WriteReference(call, {K::V_Material, {}, *scope.source.assetSelection, true, "ursa.Material"});
			return Result<OptionalReference>::Success(value);
		}
		if (scope.source.targetIdentity == "asset:ursa.Prefab")
		{
			value.F_value = WriteReference(call, {K::V_Asset, {}, *scope.source.assetSelection, true, "ursa.Prefab"});
			return Result<OptionalReference>::Success(value);
		}
		value.F_value = WriteReference(call, scope.source.targetIdentity.find("asset:") == 0
		                                         ? EditorReferenceData{K::V_ScriptableObject,
		                                                               {},
		                                                               *scope.source.assetSelection,
		                                                               true,
		                                                               scope.source.targetIdentity.substr(6)}
		                                         : Resolve(scope, K::V_Asset, {}, *scope.source.assetSelection));
	}
	return Result<OptionalReference>::Success(value);
}

Result<B::Wire::Value_Ursa_Editor_EditorDocumentValue_> EditorServices::GetDocument(B::BindingCall& call)
{
	const auto& scope = Scope(call);
	B::Wire::Value_Ursa_Editor_EditorDocumentValue_ value;
	if (const auto document = scope.source.targetUsesDocument ? scope.source.document : nullptr)
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
		    return B::Wire::Value_Ursa_Editor_EditorAssetValue{
		        WriteReference(call, value.reference), call.arena.StringValue(value.path),
		        call.arena.StringValue(value.name), value.isSubAsset ? 1u : 0u};
	    }));
}

Result<B::DataView> EditorServices::GetProjectTags(B::BindingCall& call)
{
	const auto& values = Scope(call).source.projectTags;
	return Result<B::DataView>::Success(B::WriteArray<B::Utf8View>(call.arena, values, [&](const auto& value)
	                                                               { return call.arena.StringValue(value); }));
}

Result<B::DataView> EditorServices::GetProjectLayers(B::BindingCall& call)
{
	const auto& values = Scope(call).source.projectLayers;
	Vector<String> layerNames;
	layerNames.reserve(values.size());
	for (const auto& value : values)
	{
		layerNames.push_back(value);
	}
	return Result<B::DataView>::Success(B::WriteArray<B::Utf8View>(call.arena, layerNames, [&](const auto& value)
	                                                               { return call.arena.StringValue(value); }));
}

Result<void> EditorServices::OpenTagLayerSettings(B::BindingCall& call, bool layers)
{
	auto& callback = Scope(call).source.openTagLayerSettings;
	if (callback)
	{
		callback(layers);
	}
	return {};
}

Result<UInt32> EditorServices::GetComponentMenuState(B::BindingCall& call)
{
	return Result<UInt32>::Success(Scope(call).source.componentMenuState);
}

Result<void> EditorServices::SetComponentMenuCommand(B::BindingCall& call, UInt32 command)
{
	if (command > static_cast<UInt32>(ComponentMenuCommand::Remove))
	{
		return Result<void>::Failure("Unknown component menu command");
	}
	Scope(call).source.componentMenuCommand = command;
	return {};
}

Result<UInt32> EditorServices::GetHierarchyMenuState(B::BindingCall& call)
{
	return Result<UInt32>::Success(Scope(call).source.hierarchyMenuState);
}

Result<void> EditorServices::SetHierarchyMenuCommand(B::BindingCall& call, UInt32 command)
{
	if (command > 15)
	{
		return Result<void>::Failure("Unknown Hierarchy menu command");
	}
	Scope(call).source.hierarchyMenuCommand = command;
	return {};
}

Result<UInt32> EditorServices::GetHierarchyInputState(B::BindingCall& call)
{
	return Result<UInt32>::Success(Scope(call).source.hierarchyInputState);
}

Result<void> EditorServices::SetHierarchyInputCommand(B::BindingCall& call, UInt32 command)
{
	const UInt32 action = command & 0xffffu;
	if (action > 16 || (command & ~0x3ffffu) != 0)
	{
		return Result<void>::Failure("Unknown Hierarchy input command");
	}
	Scope(call).source.hierarchyInputCommand = command;
	return {};
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
	auto& scope = Scope(call);
	if (!scope.source.canEditTarget)
	{
		return Result<void>::Failure("This Editor target is read-only");
	}
	const auto synchronized = scope.SynchronizeRuntimeTarget();
	if (!synchronized)
	{
		return Result<void>::Failure(synchronized.error);
	}
	if (scope.source.markTargetDirty)
	{
		scope.source.markTargetDirty();
		return {};
	}
	if (!scope.source.targetUsesDocument || !scope.source.document)
	{
		return Result<void>::Failure("No editing document is open");
	}
	scope.source.document->MarkDirty();
	return {};
}

Result<void> EditorServices::Save(B::BindingCall& call)
{
	auto& scope = Scope(call);
	if (!scope.source.canEditTarget)
	{
		return Result<void>::Failure("This Editor target is read-only");
	}
	const auto synchronized = scope.SynchronizeRuntimeTarget();
	if (!synchronized)
	{
		return Result<void>::Failure(synchronized.error);
	}
	if (scope.source.saveTarget)
	{
		return scope.source.saveTarget();
	}
	if (!scope.source.targetUsesDocument)
	{
		return Result<void>::Failure("This target requires its own explicit save or importer Apply command");
	}
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

Result<void> EditorServices::SetActiveCamera(B::BindingCall& call)
{
	auto& scope = Scope(call);
	RequireGui(call);
	if (!scope.source.canEditTarget || !scope.source.target)
	{
		return Result<void>::Failure("The current Inspector target cannot be edited");
	}
	if (!scope.source.selection.scene)
	{
		return Result<void>::Failure("The current document has no editable Scene");
	}
	auto* camera = dynamic_cast<CameraComponent*>(scope.source.target);
	if (!camera)
	{
		return Result<void>::Failure("The current Inspector target is not a Camera component");
	}
	auto activated = scope.source.selection.scene->SetActiveCamera(camera->Id());
	if (!activated)
	{
		return Result<void>::Failure(activated.error);
	}
	scope.changed = true;
	if (scope.source.document)
	{
		scope.source.document->MarkDirty();
	}
	return {};
}

Result<void> EditorServices::SaveMeshComponentAs(B::BindingCall& call, const B::Utf8View& path, bool replaceBinding)
{
	RequireGui(call);
	auto& scope = Scope(call);
	if (!scope.source.canEditTarget || !scope.source.target || !scope.source.workspace)
	{
		return Result<void>::Failure("The current Inspector target cannot save a Mesh asset");
	}
	auto* filter = dynamic_cast<MeshFilterComponent*>(scope.source.target);
	auto* renderer = dynamic_cast<SkinnedMeshRendererComponent*>(scope.source.target);
	if (!filter && !renderer)
	{
		return Result<void>::Failure("The current Inspector target has no Mesh binding");
	}
	const MeshPtr mesh = filter ? filter->SharedMesh() : renderer->SharedMesh();
	if (!mesh || !mesh->IsValid())
	{
		return Result<void>::Failure("Assign a valid skinned Mesh before saving it as an asset");
	}
	const auto destination = VirtualPath::Parse(B::ReadString(path));
	if (!destination)
	{
		return Result<void>::Failure(destination.error);
	}
	AssetDatabase* database = scope.source.workspace->Database();
	if (!database)
	{
		return Result<void>::Failure("Project AssetDatabase is unavailable");
	}
	auto saved = SaveMeshAs(*database, scope.Resources(), destination.value, mesh);
	if (!saved)
	{
		return Result<void>::Failure(saved.error);
	}
	if (replaceBinding)
	{
		auto assigned =
		    filter ? filter->SetSharedMesh(std::move(saved.value)) : renderer->SetSharedMesh(std::move(saved.value));
		if (!assigned)
		{
			return assigned;
		}
	}
	if (!replaceBinding)
	{
		return {};
	}
	scope.changed = true;
	if (scope.source.markTargetDirty)
	{
		scope.source.markTargetDirty();
	}
	else if (scope.source.document)
	{
		scope.source.document->MarkDirty();
	}
	return {};
}
} // namespace Ursa::Editor::Bindings
