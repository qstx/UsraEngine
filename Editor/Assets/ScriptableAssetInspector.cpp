#include <Assets/ScriptableAssetInspector.hpp>
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
template <class T> T Take(Result<T> result)
{
	return Scripting::Bindings::TakeBindingResult(std::move(result));
}
} // namespace

Result<std::shared_ptr<ScriptableAssetInspector>>
ScriptableAssetInspector::Open(ProjectWorkspace& workspace, const VirtualPath& source, const ScriptTypeDescriptor& type)
{
	try
	{
		if (PathFromUrsaString(source.relative).extension() != ".ursaasset" || source.scheme == editorLibraryScheme)
		{
			return Result<std::shared_ptr<ScriptableAssetInspector>>::Failure(
			    "ScriptableObject editing requires a source asset, not an imported artifact");
		}
		auto database = Take(AssetDatabase::Open(workspace.Index().Filesystem(), source.scheme));
		Scripting::Bindings::CompleteBindingResult(database->Refresh());
		const auto metadata = Take(database->Metadata(source));
		auto asset = Take(LoadScriptableAssetSource(workspace.Index().Filesystem(), source, metadata.guid));
		const auto defaults = Take(CreateScriptableAssetWithDefaults(metadata.guid, type)).Payload();
		const auto& saved = asset.Payload();
		if (asset.TypeId() != type.scriptType || asset.TypeVersion() != type.version ||
		    saved.value("SourceGuid", std::string{}) != ToStdString(type.sourceGuid.ToString()) ||
		    saved.value("LocalFileId", UInt64{0}) != type.localFileId || !saved.contains("Fields") ||
		    !saved["Fields"].is_object())
		{
			return Result<std::shared_ptr<ScriptableAssetInspector>>::Failure(
			    "ScriptableObject source does not match the current Game type identity/version; original fields are "
			    "preserved");
		}
		auto result = std::shared_ptr<ScriptableAssetInspector>(new ScriptableAssetInspector());
		result->workspace = &workspace;
		result->database = std::move(database);
		result->source = source;
		result->payload = defaults;
		result->payload["Fields"] = saved["Fields"];
		for (const auto& field : type.fields)
		{
			const auto name = ToStdString(field.name);
			if (!result->payload["Fields"].contains(name))
			{
				result->payload["Fields"][name] = defaults["Fields"][name];
				for (const auto& alias : field.formerNames)
				{
					if (saved["Fields"].contains(ToStdString(alias)))
					{
						result->payload["Fields"][name] = saved["Fields"][ToStdString(alias)];
						break;
					}
				}
			}
		}
		result->asset = std::move(asset);
		result->baselinePayload = result->payload;
		result->baselineBytes = Take(workspace.Index().Filesystem().Read(source));
		result->resources = Take(ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver()));
		return Result<std::shared_ptr<ScriptableAssetInspector>>::Success(std::move(result));
	}
	catch (const std::exception& error)
	{
		return Result<std::shared_ptr<ScriptableAssetInspector>>::Failure(ToUrsaString(error.what()));
	}
}

Result<void> ScriptableAssetInspector::ValidateSource() const
{
	const auto metadata = database->Metadata(source);
	const auto bytes = workspace->Index().Filesystem().Read(source);
	return metadata && metadata.value.guid == asset.SourceGuid() && bytes && bytes.value == baselineBytes
	           ? Result<void>{}
	           : Result<void>::Failure(
	                 "ScriptableObject source was changed, removed or replaced; reopen its Inspector");
}

SerializedTargetAdapter ScriptableAssetInspector::Properties()
{
	const std::weak_ptr<ScriptableAssetInspector> owner = shared_from_this();
	SerializedTargetAdapter adapter;
	adapter.resources = resources.get();
	adapter.capture = [owner]() -> Result<nlohmann::json>
	{
		const auto target = owner.lock();
		if (!target)
		{
			return Result<nlohmann::json>::Failure("ScriptableObject Inspector target is closed");
		}
		const auto valid = target->ValidateSource();
		return valid ? Result<nlohmann::json>::Success(target->payload) : Result<nlohmann::json>::Failure(valid.error);
	};
	if (workspace->CheckWritable(source))
	{
		adapter.apply = [owner](const nlohmann::json& value) -> Result<nlohmann::json>
		{
			const auto target = owner.lock();
			if (!target)
			{
				return Result<nlohmann::json>::Failure("ScriptableObject Inspector target is closed");
			}
			const auto valid = target->ValidateSource();
			if (!valid)
			{
				return Result<nlohmann::json>::Failure(valid.error);
			}
			const auto writable = target->workspace->CheckWritable(target->source);
			if (!writable)
			{
				return Result<nlohmann::json>::Failure(writable.error);
			}
			target->payload = value;
			return Result<nlohmann::json>::Success(value);
		};
		adapter.prepare = [owner](const nlohmann::json& original,
		                          const nlohmann::json& candidate) -> Result<SerializedTargetCheckpoint>
		{
			const auto target = owner.lock();
			if (!target)
			{
				return Result<SerializedTargetCheckpoint>::Failure("ScriptableObject Inspector target is closed");
			}
			auto valid = target->ValidateSource();
			if (!valid)
			{
				return Result<SerializedTargetCheckpoint>::Failure(valid.error);
			}
			const auto writable = target->workspace->CheckWritable(target->source);
			if (!writable)
			{
				return Result<SerializedTargetCheckpoint>::Failure(writable.error);
			}
			SerializedTargetCheckpoint checkpoint;
			checkpoint.serializedPayload = original;
			checkpoint.backendToken = {{"Candidate", candidate}};
			return Result<SerializedTargetCheckpoint>::Success(std::move(checkpoint));
		};
		adapter.validate = [owner](const SerializedTargetCheckpoint&)
		{
			const auto target = owner.lock();
			if (!target)
			{
				return Result<void>::Failure("ScriptableObject Inspector target is closed");
			}
			auto valid = target->ValidateSource();
			if (!valid)
			{
				return valid;
			}
			return target->workspace->CheckWritable(target->source);
		};
		adapter.publish = [owner](const SerializedTargetCheckpoint& checkpoint) -> Result<nlohmann::json>
		{
			const auto target = owner.lock();
			if (!target)
			{
				return Result<nlohmann::json>::Failure("ScriptableObject Inspector target is closed");
			}
			target->payload = checkpoint.backendToken.at("Candidate");
			return Result<nlohmann::json>::Success(target->payload);
		};
		adapter.restore = [owner](const SerializedTargetCheckpoint& checkpoint)
		{
			const auto target = owner.lock();
			if (!target)
			{
				return Result<void>::Failure("ScriptableObject Inspector target is closed");
			}
			target->payload = checkpoint.serializedPayload;
			return Result<void>{};
		};
	}
	return adapter;
}

Result<void> ScriptableAssetInspector::Save()
{
	try
	{
		auto& files = workspace->Index().Filesystem();
		Scripting::Bindings::CompleteBindingResult(workspace->CheckWritable(source));
		const auto lease = Take(files.AcquireExclusiveLease(Take(VirtualPath::Parse("lib:Import.lock"))));
		Scripting::Bindings::CompleteBindingResult(ValidateSource());
		auto properties = Take(SerializedObject::Create(Properties()));
		Scripting::Bindings::CompleteBindingResult(properties->ValidateAll());
		ScriptableAsset candidate;
		Scripting::Bindings::CompleteBindingResult(candidate.SetDescription(
		    asset.SourceGuid(), asset.LocalFileId(), asset.TypeId(), asset.TypeVersion(), payload));
		Scripting::Bindings::CompleteBindingResult(SaveScriptableAssetSource(files, source, candidate));
		baselineBytes = Take(files.Read(source));
		baselinePayload = payload;
		asset = std::move(candidate);
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
}

Result<bool> ScriptableAssetInspector::Draw(const std::function<void(const VirtualPath&)>& requestImport,
                                            const AssetImporterInspector::CustomDraw& custom,
                                            const Vector<ScriptTypeDescriptor>* scriptTypes)
{
	try
	{
		ManagedEditorContext context;
		context.workspace = workspace;
		context.scriptTypes = scriptTypes;
		context.serializedTarget = Properties();
		context.targetUsesDocument = false;
		context.assetSelection = AssetReference{asset.SourceGuid(), asset.LocalFileId()};
		context.targetIdentity = "asset:" + asset.TypeId();
		// Property transactions and Save verify source bytes. The callback itself owns this target.
		context.validateTarget = [] { return Result<void>{}; };
		context.saveTarget = [this, &requestImport]
		{
			const auto result = Save();
			if (result && requestImport)
			{
				requestImport(source);
			}
			return result;
		};
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
			Ui::DisabledScope fieldsScope(!workspace->CheckWritable(source));
			if (!handled)
			{
				Bindings::EditorInvocation invocation(context, 1);
				Scripting::Bindings::CallbackResult<UInt32> drawn;
				drawn.status = Scripting::Bindings::Editor::NativeEditorBindings_DrawDefaultInspector(
				    invocation.Token(), 1, &drawn.value, &drawn.outcome);
				if (!drawn)
				{
					diagnostic = drawn.Error();
				}
				changed = drawn && drawn.value;
			}
			{
				Ui::DisabledScope saveScope(!Dirty());
				if (Icons::Button("Save ScriptableObject"))
				{
					const auto saved = context.saveTarget();
					diagnostic = saved ? String{} : saved.error;
				}
			}
		}
		if (Dirty())
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
