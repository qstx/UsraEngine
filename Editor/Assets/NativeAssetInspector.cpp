#include <Assets/NativeAssetInspector.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/SceneDraft.hpp>
#include <Scripting/Bindings/EditorServices.hpp>
#include <Editor.generated.hpp>
#include <Ui/EditorIcons.hpp>
#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <Ui/ImGuiScopes.hpp>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;
namespace B = Scripting::Bindings;

Json CaptureRoot(const GameObject::Ptr& root)
{
	if (!root || !root->IsValid())
	{
		throw std::runtime_error("Prefab Inspector target is closed");
	}
	const auto transform = B::TakeBindingResult(root->GetLocalTransform());
	return {{"Fields",
	         {{"name", ToStdString(B::TakeBindingResult(root->Name()))},
	          {"active", B::TakeBindingResult(root->ActiveSelf())},
	          {"layer", B::TakeBindingResult(root->Layer())},
	          {"tag", ToStdString(B::TakeBindingResult(root->Tag()))},
	          {"static", B::TakeBindingResult(root->IsStatic())},
	          {"position", {transform.position.x, transform.position.y, transform.position.z}},
	          {"rotation", {transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w}},
	          {"scale", {transform.scale.x, transform.scale.y, transform.scale.z}}}},
	        {"FieldTypes",
	         {{"name", {{"Kind", "String"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.String"}}},
	          {"active", {{"Kind", "Boolean"}, {"ElementKind", "Unsupported"}}},
	          {"layer", {{"Kind", "UnsignedInteger"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.UInt32"}}},
	          {"tag", {{"Kind", "String"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.String"}}},
	          {"static", {{"Kind", "Boolean"}, {"ElementKind", "Unsupported"}}},
	          {"position", {{"Kind", "Vector3"}, {"ElementKind", "Unsupported"}}},
	          {"rotation", {{"Kind", "Quaternion"}, {"ElementKind", "Unsupported"}}},
	          {"scale", {{"Kind", "Vector3"}, {"ElementKind", "Unsupported"}}}}}};
}

Result<void> ApplyRoot(const GameObject::Ptr& root, const Json& payload)
{
	const auto& fields = payload.at("Fields");
	const auto name = ToUrsaString(fields.at("name").get<std::string>());
	const auto layer = fields.at("layer").get<UInt32>();
	const auto tag = ToUrsaString(fields.at("tag").get<std::string>());
	if (name.size() > 256 || tag.empty() || tag.size() > 256 || layer > 31)
	{
		return Result<void>::Failure("Prefab name, tag, or layer is invalid");
	}
	const auto vector = [](const Json& value)
	{ return Vec3{value.at(0).get<Float32>(), value.at(1).get<Float32>(), value.at(2).get<Float32>()}; };
	LocalTransform transform;
	transform.position = vector(fields.at("position"));
	transform.scale = vector(fields.at("scale"));
	const auto& rotation = fields.at("rotation");
	transform.rotation = {rotation.at(0).get<Float32>(), rotation.at(1).get<Float32>(), rotation.at(2).get<Float32>(),
	                      rotation.at(3).get<Float32>()};
	// SerializedObject validates field types and finite math components before this commit.
	B::CompleteBindingResult(root->SetLocalTransform(transform));
	B::CompleteBindingResult(root->SetName(name));
	B::CompleteBindingResult(root->SetLayer(layer));
	B::CompleteBindingResult(root->SetTag(tag));
	B::CompleteBindingResult(root->SetStatic(fields.at("static").get<bool>()));
	return root->SetActive(fields.at("active").get<bool>());
}
} // namespace

Result<std::shared_ptr<NativeAssetInspector>> NativeAssetInspector::Open(ProjectWorkspace& workspace,
                                                                         const VirtualPath& source, ResourceKind kind,
                                                                         const ComponentRegistry* registry,
                                                                         EditorDocuments* existingDocuments)
{
	try
	{
		if ((kind != ResourceKind::Material && kind != ResourceKind::Prefab) || source.scheme == editorLibraryScheme)
		{
			return Result<std::shared_ptr<NativeAssetInspector>>::Failure(
			    "Native asset editing requires a Material or Prefab source");
		}
		auto target = std::shared_ptr<NativeAssetInspector>(new NativeAssetInspector());
		target->workspace = &workspace;
		if (registry)
		{
			target->components = *registry;
		}
		else
		{
			B::CompleteBindingResult(RegisterBuiltinComponents(target->components));
		}
		if (existingDocuments && existingDocuments->Current() && existingDocuments->Current()->Path() == source &&
		    existingDocuments->Current()->Kind() == kind)
		{
			target->documents = existingDocuments;
		}
		else
		{
			target->ownedDocuments = B::TakeBindingResult(EditorDocuments::Create(workspace));
			target->documents = target->ownedDocuments.get();
			auto document = kind == ResourceKind::Material
			                    ? OpenMaterialDocument(source, target->documents->Session(), workspace)
			                    : OpenSceneDocument(source, kind, target->components, target->documents->Session());
			B::CompleteBindingResult(target->documents->RequestOpen(B::TakeBindingResult(std::move(document))));
		}
		target->sourceDocument = target->documents->Current();
		target->loadContext = std::make_unique<ComponentLoadContext>(target->documents->Session());
		target->baselineBytes = B::TakeBindingResult(workspace.Index().Filesystem().Read(source));
		const auto sidecar = B::TakeBindingResult(VirtualPath::Parse(source.ToString() + ".meta"));
		if (B::TakeBindingResult(workspace.Index().Filesystem().Exists(sidecar)))
		{
			target->baselineMetadata = B::TakeBindingResult(workspace.Index().Filesystem().Read(sidecar));
			const auto metadata = Json::parse(target->baselineMetadata->begin(), target->baselineMetadata->end());
			if (B::TakeBindingResult(ResourceId::Parse(ToUrsaString(metadata.at("guid").get<std::string>()))) !=
			    target->Document().Id())
			{
				return Result<std::shared_ptr<NativeAssetInspector>>::Failure(
				    "Source asset header and metadata GUID differ");
			}
		}
		return Result<std::shared_ptr<NativeAssetInspector>>::Success(std::move(target));
	}
	catch (const std::exception& error)
	{
		return Result<std::shared_ptr<NativeAssetInspector>>::Failure(ToUrsaString(error.what()));
	}
}

Result<void> NativeAssetInspector::ValidateSource() const
{
	if (!Active())
	{
		return Result<void>::Failure("The source asset document was closed or replaced");
	}
	const auto& document = Document();
	const auto bytes = workspace->Index().Filesystem().Read(document.Path());
	if (!bytes || bytes.value != baselineBytes)
	{
		return Result<void>::Failure("Source asset changed externally; reopen its Inspector");
	}
	const auto sidecar = B::TakeBindingResult(VirtualPath::Parse(document.Path().ToString() + ".meta"));
	const auto exists = workspace->Index().Filesystem().Exists(sidecar);
	if (!exists || exists.value != baselineMetadata.has_value())
	{
		return Result<void>::Failure("Source asset metadata changed");
	}
	if (baselineMetadata)
	{
		const auto current = workspace->Index().Filesystem().Read(sidecar);
		if (!current || current.value != *baselineMetadata)
		{
			return Result<void>::Failure("Source asset metadata changed");
		}
	}
	const auto metadata = workspace->Database() && document.Path().scheme == projectScheme
	                          ? workspace->Database()->Metadata(document.Path())
	                          : Result<AssetMetadata>::Failure("No project metadata");
	if (metadata && metadata.value.guid != document.Id())
	{
		return Result<void>::Failure("Source asset GUID changed");
	}
	return {};
}

SerializedTargetAdapter NativeAssetInspector::Properties()
{
	const auto owner = weak_from_this();
	const auto validate = [owner]() -> Result<void>
	{
		const auto target = owner.lock();
		return target ? target->ValidateSource() : Result<void>::Failure("Native asset Inspector is closed");
	};
	if (!Active())
	{
		SerializedTargetAdapter expired;
		expired.capture = [] { return Result<Json>::Failure("The source asset document was closed or replaced"); };
		return expired;
	}
	const bool writable = static_cast<bool>(workspace->CheckWritable(Document().Path()));
	if (const auto material = Document().Payload<MaterialDraft>())
	{
		return material->Properties(documents->Session(), writable, validate);
	}
	SerializedTargetAdapter adapter;
	adapter.resources = &documents->Session();
	adapter.capture = [owner, validate]() -> Result<Json>
	{
		try
		{
			B::CompleteBindingResult(validate());
			return Result<Json>::Success(
			    CaptureRoot(owner.lock()->Document().Payload<SceneDraft>()->GetPrefabAsset()->Root()));
		}
		catch (const std::exception& error)
		{
			return Result<Json>::Failure(ToUrsaString(error.what()));
		}
	};
	if (writable)
	{
		adapter.apply = [owner, validate](const Json& payload) -> Result<Json>
		{
			try
			{
				B::CompleteBindingResult(validate());
				const auto target = owner.lock();
				const auto root = target->Document().Payload<SceneDraft>()->GetPrefabAsset()->Root();
				B::CompleteBindingResult(ApplyRoot(root, payload));
				return Result<Json>::Success(CaptureRoot(root));
			}
			catch (const std::exception& error)
			{
				return Result<Json>::Failure(ToUrsaString(error.what()));
			}
		};
	}
	return adapter;
}

Result<void> NativeAssetInspector::Save()
{
	try
	{
		B::CompleteBindingResult(ValidateSource());
		B::CompleteBindingResult(workspace->CheckWritable(Document().Path()));
		const auto lease = B::TakeBindingResult(workspace->Index().Filesystem().AcquireExclusiveLease(
		    B::TakeBindingResult(VirtualPath::Parse("lib:Import.lock"))));
		B::CompleteBindingResult(ValidateSource());
		B::CompleteBindingResult(documents->SaveCurrent());
		baselineBytes = B::TakeBindingResult(workspace->Index().Filesystem().Read(Document().Path()));
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
}

Result<bool> NativeAssetInspector::Draw(const std::function<void(const VirtualPath&)>& requestImport,
                                        const AssetImporterInspector::CustomDraw& custom, const DefaultDraw& fallback)
{
	try
	{
		B::CompleteBindingResult(ValidateSource());
		ManagedEditorContext context;
		context.workspace = workspace;
		context.document = &Document();
		context.documents = documents;
		context.registry = &components;
		context.serializedTarget = Properties();
		context.assetSelection = AssetReference{Document().Id(), 0};
		context.canEditTarget = static_cast<bool>(workspace->CheckWritable(Document().Path()));
		context.targetIdentity =
		    Document().Kind() == ResourceKind::Material ? "asset:ursa.Material" : "asset:ursa.Prefab";
		// Disk conflicts are checked by capture/commit/save; individual GUI calls only need the live document lease.
		context.validateTarget = [this]
		{ return Active() ? Result<void>{} : Result<void>::Failure("Source document is closed"); };
		context.saveTarget = [this, &requestImport]
		{
			const auto saved = Save();
			if (saved && requestImport)
			{
				requestImport(Document().Path());
			}
			return saved;
		};
		if (const auto material = Document().Payload<MaterialDraft>())
		{
			context.targetMaterial = material->MaterialPtr();
		}
		else
		{
			context.selection.object = Document().Payload<SceneDraft>()->GetPrefabAsset()->Root();
			context.selection.roots = {context.selection.object};
			context.selection.prefab = Document().Payload<SceneDraft>()->GetPrefabAsset();
			context.selection.loadContext = loadContext.get();
		}
		bool handled = false;
		bool changed = false;
		if (custom)
		{
			const auto invoked = custom(context);
			handled = !invoked || invoked.value;
			if (!invoked)
			{
				diagnostic = invoked.error;
			}
		}
		{
			Ui::DisabledScope fieldsScope(!context.canEditTarget);
			if (!handled)
			{
				const auto overridden = fallback && Document().Kind() == ResourceKind::Material
				                            ? fallback(Document(), *documents)
				                            : std::nullopt;
				if (overridden)
				{
					const auto& drawn = *overridden;
					if (!drawn)
					{
						diagnostic = drawn.error;
					}
					else
					{
						changed = drawn.value;
						if (changed)
						{
							Document().MarkDirty();
						}
					}
				}
				else
				{
					Bindings::EditorInvocation invocation(context, 1);
					B::CallbackResult<UInt32> drawn;
					drawn.status = B::Editor::NativeEditorBindings_DrawDefaultInspector(invocation.Token(), 1,
					                                                                    &drawn.value, &drawn.outcome);
					if (!drawn)
					{
						diagnostic = drawn.Error();
					}
					changed = drawn && drawn.value;
				}
			}
			{
				Ui::DisabledScope saveScope(!Document().Dirty());
				if (Icons::Button("Save Asset"))
				{
					const auto saved = context.saveTarget();
					diagnostic = saved ? String{} : saved.error;
				}
			}
		}
		if (Document().Dirty())
		{
			ImGui::TextDisabled("Dirty");
		}
		if (!diagnostic.empty())
		{
			ImGui::TextWrapped("%s", diagnostic.c_str());
		}
		return Result<bool>::Success(changed);
	}
	catch (const std::exception& error)
	{
		diagnostic = ToUrsaString(error.what());
		ImGui::TextWrapped("%s", diagnostic.c_str());
		return Result<bool>::Failure(diagnostic);
	}
}
} // namespace Ursa::Editor
