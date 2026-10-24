#include <Assets/ImportedAssetInspector.hpp>
#include <Assets/MaterialDraft.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/SceneResource.hpp>
#include <Scripting/Bindings/EditorServices.hpp>
#include <Editor.generated.hpp>
#include <imgui.h>
#include <algorithm>
#include <stdexcept>

namespace Ursa::Editor
{
namespace
{
template <class T> T Take(Result<T> value)
{
	return Scripting::Bindings::TakeBindingResult(std::move(value));
}

void Done(Result<void> value)
{
	Scripting::Bindings::CompleteBindingResult(std::move(value));
}
} // namespace

Result<std::shared_ptr<ImportedAssetInspector>>
ImportedAssetInspector::Open(ProjectWorkspace& workspace, AssetReference reference, const String& kind)
{
	try
	{
		auto result = std::make_shared<ImportedAssetInspector>();
		result->workspace = &workspace;
		result->reference = reference;
		result->resources = Take(ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver()));
		result->loadContext = std::make_unique<ComponentLoadContext>(*result->resources);
		result->payload = {{"Fields", nlohmann::json::object()}, {"FieldTypes", nlohmann::json::object()}};
		const auto number = [&](const char* name, UInt64 value)
		{
			result->payload["Fields"][name] = value;
			result->payload["FieldTypes"][name] = {{"Kind", "UnsignedInteger"}, {"TypeName", "System.UInt64"}};
		};
		if (kind == "Mesh")
		{
			result->identity = "asset:ursa.Mesh";
			result->mesh = Take(result->resources->LoadMesh(reference.guid, reference.localFileId));
			const auto snapshot = result->mesh->Snapshot();
			number("vertexCount", snapshot->data.positions.size());
			number("indexCount", snapshot->data.indices.size());
			number("subMeshCount", snapshot->data.subMeshes.size());
		}
		else if (kind == "Texture")
		{
			result->identity = "asset:ursa.Texture";
			result->texture = Take(result->resources->LoadTexture(reference.guid, reference.localFileId));
			number("width", result->texture->GetWidth());
			number("height", result->texture->GetHeight());
			number("mipCount", result->texture->GetMipLevels());
		}
		else if (kind == "Material")
		{
			result->identity = "asset:ursa.Material";
			result->material = Take(result->resources->LoadMaterial(reference.guid, reference.localFileId));
			result->payload = Take(MaterialDraft::DescribeProperties(*result->material));
		}
		else if (kind == "ScriptableObject" || kind == "ScriptableAsset")
		{
			const auto graph = Take(LoadScriptableAssetGraph(*result->resources, reference));
			const auto found = std::find_if(
			    graph.assets.begin(), graph.assets.end(), [&](const auto& asset)
			    { return asset.SourceGuid() == graph.root.guid && asset.LocalFileId() == graph.root.localFileId; });
			if (found == graph.assets.end())
			{
				throw std::runtime_error("Imported ScriptableObject root is missing");
			}
			result->reference = graph.root;
			result->identity = "asset:" + found->TypeId();
			result->payload = found->Payload();
		}
		else if (kind == "Prefab")
		{
			ComponentRegistry components;
			Done(RegisterBuiltinComponents(components));
			const auto path = Take(result->resources->ResolveAsset(reference.guid, reference.localFileId));
			const auto prefab = Take(LoadPrefabAssetResource(result->resources->Index().Filesystem(), path, components,
			                                                 *result->loadContext));
			result->root = prefab.Root();
			result->identity = "asset:ursa.Prefab";
			result->payload["Fields"]["name"] = ToStdString(Take(result->root->Name()));
			result->payload["FieldTypes"]["name"] = {{"Kind", "String"}, {"TypeName", "System.String"}};
		}
		else if (kind == "Skeleton")
		{
			result->identity = "asset:ursa.Skeleton";
			result->skeleton = Take(result->resources->LoadSkeleton(reference.guid, reference.localFileId));
			number("jointCount", result->skeleton->joints.size());
		}
		else if (kind == "AnimationClip")
		{
			result->identity = "asset:ursa.AnimationClip";
			result->clip = Take(result->resources->LoadAnimationClip(reference.guid, reference.localFileId));
			number("channelCount", result->clip->channels.size());
			result->payload["Fields"]["durationSeconds"] = result->clip->durationSeconds;
			result->payload["FieldTypes"]["durationSeconds"] = {{"Kind", "FloatingPoint"},
			                                                    {"TypeName", "System.Double"}};
		}
		else
		{
			throw std::runtime_error("This imported object has no resource Inspector adapter");
		}
		return Result<std::shared_ptr<ImportedAssetInspector>>::Success(std::move(result));
	}
	catch (const std::exception& error)
	{
		return Result<std::shared_ptr<ImportedAssetInspector>>::Failure(ToUrsaString(error.what()));
	}
}

ManagedEditorContext ImportedAssetInspector::Context()
{
	ManagedEditorContext context;
	context.workspace = workspace;
	context.assetSelection = reference;
	context.targetIdentity = identity;
	context.targetUsesDocument = false;
	context.canEditTarget = false;
	context.targetMesh = mesh;
	context.targetMaterial = material;
	context.targetTexture = texture;
	context.selection.loadContext = loadContext.get();
	context.selection.object = root;
	if (root)
	{
		context.selection.roots = {root};
	}
	SerializedTargetAdapter adapter;
	adapter.resources = resources.get();
	const std::weak_ptr<ImportedAssetInspector> owner = shared_from_this();
	adapter.capture = [owner]
	{
		const auto target = owner.lock();
		return target ? Result<nlohmann::json>::Success(target->payload)
		              : Result<nlohmann::json>::Failure("Imported Inspector target is closed");
	};
	context.serializedTarget = std::move(adapter);
	return context;
}

Result<void> ImportedAssetInspector::Draw(const std::function<Result<bool>(ManagedEditorContext&)>& custom)
{
	ImGui::TextDisabled("Imported object is read-only. Edit its source or importer to regenerate it.");
	auto context = Context();
	if (custom)
	{
		const auto drawn = custom(context);
		if (!drawn)
		{
			return Result<void>::Failure(drawn.error);
		}
		if (drawn.value)
		{
			return {};
		}
	}
	if (skeleton)
	{
		ImGui::Text("Joints: %zu", skeleton->joints.size());
		for (Size index = 0; index < skeleton->joints.size(); ++index)
		{
			const auto& joint = skeleton->joints[index];
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::TreeNode(joint.name.c_str()))
			{
				const auto parent = joint.parentJointIndex;
				ImGui::Text("Parent: %s", parent >= 0 && Size(parent) < skeleton->joints.size()
				                              ? skeleton->joints[parent].name.c_str()
				                              : "Root");
				const auto& bind = joint.bindLocalTransform;
				ImGui::Text("Bind position: %.3f, %.3f, %.3f", bind.position.x, bind.position.y, bind.position.z);
				ImGui::Text("Bind rotation: %.3f, %.3f, %.3f, %.3f", bind.rotation.x, bind.rotation.y, bind.rotation.z,
				            bind.rotation.w);
				ImGui::Text("Bind scale: %.3f, %.3f, %.3f", bind.scale.x, bind.scale.y, bind.scale.z);
				ImGui::TextUnformatted("Inverse bind matrix:");
				for (Int32 row = 0; row < 4; ++row)
				{
					ImGui::Text("%.3f  %.3f  %.3f  %.3f", joint.inverseBindMatrix.At(row, 0),
					            joint.inverseBindMatrix.At(row, 1), joint.inverseBindMatrix.At(row, 2),
					            joint.inverseBindMatrix.At(row, 3));
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		return {};
	}
	if (clip)
	{
		ImGui::Text("Clip: %s", clip->name.c_str());
		ImGui::Text("Duration: %.3f seconds | Channels: %zu", clip->durationSeconds, clip->channels.size());
		for (const auto& channel : clip->channels)
		{
			ImGui::BulletText("Node %llu: position %zu, rotation %zu, scale %zu keys", channel.nodeLocalFileId,
			                  channel.positions.size(), channel.rotations.size(), channel.scales.size());
		}
		return {};
	}
	try
	{
		Bindings::EditorInvocation invocation(context, 1);
		Scripting::Bindings::CallbackResult<UInt32> drawn;
		drawn.status = Scripting::Bindings::Editor::NativeEditorBindings_DrawDefaultInspector(
		    invocation.Token(), 1, &drawn.value, &drawn.outcome);
		return drawn ? Result<void>{} : Result<void>::Failure(drawn.Error());
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Editor
