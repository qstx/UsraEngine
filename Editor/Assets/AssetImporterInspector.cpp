#include <Assets/AssetImporterInspector.hpp>
#include <Assets/TextureImporter.hpp>
#include <Assets/ModelImporter.hpp>
#include <Scripting/Bindings/EditorServices.hpp>
#include <Editor.generated.hpp>
#include <Ui/EditorIcons.hpp>
#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <filesystem>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

Json Field(const char* kind, const char* type = "")
{
	return {{"Kind", kind}, {"ElementKind", "Unsupported"}, {"TypeName", type}};
}

Json EnumField(std::initializer_list<const char*> names)
{
	Json value = Field("Enumeration", "System.Int32");
	value["EnumValues"] = Json::array();
	value["EnumNumericValues"] = Json::array();
	value["EnumUnderlyingType"] = "System.Int32";
	Int64 index = 0;
	for (const auto* name : names)
	{
		value["EnumValues"].push_back(name);
		value["EnumNumericValues"].push_back(index++);
	}
	return value;
}

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}
} // namespace

Result<std::shared_ptr<AssetImporterInspector>> AssetImporterInspector::Open(AssetDatabase& database,
                                                                             const VirtualPath& source)
{
	try
	{
		String extension = PathToUrsaString(PathFromUrsaString(source.relative).extension());
		const auto rules = Take(AssetImportRules::Create());
		const auto descriptor = *Take(rules.FindImporter(extension));
		auto result = std::shared_ptr<AssetImporterInspector>(new AssetImporterInspector());
		result->database = &database;
		result->source = source;
		result->guid = Take(database.Metadata(source)).guid;
		result->identity = descriptor.identity;
		result->version = descriptor.version;
		if (descriptor.identity == "ursa.Texture")
		{
			result->kind = Kind::Texture;
			result->draft =
			    Take(ImportSettingsDraft::Open(database, source, descriptor, TextureImportSettings{}.ToJson(),
			                                   [](const Json& value)
			                                   {
				                                   const auto parsed = TextureImportSettings::FromJson(value);
				                                   return parsed ? Result<void>{} : Result<void>::Failure(parsed.error);
			                                   }));
		}
		else if (descriptor.identity == "ursa.Model")
		{
			result->kind = Kind::Model;
			result->draft =
			    Take(ImportSettingsDraft::Open(database, source, descriptor, ModelImportSettings{}.ToJson(),
			                                   [](const Json& value)
			                                   {
				                                   const auto parsed = ModelImportSettings::FromJson(value);
				                                   return parsed ? Result<void>{} : Result<void>::Failure(parsed.error);
			                                   }));
		}
		else
		{
			return Result<std::shared_ptr<AssetImporterInspector>>::Failure("No typed native importer Inspector for " +
			                                                                source.ToString());
		}
		const auto recovered = result->Recover();
		if (!recovered)
		{
			result->diagnostic = recovered.error;
		}
		return Result<std::shared_ptr<AssetImporterInspector>>::Success(std::move(result));
	}
	catch (const std::exception& error)
	{
		return Result<std::shared_ptr<AssetImporterInspector>>::Failure(ToUrsaString(error.what()));
	}
}

Result<std::shared_ptr<AssetImporterInspector>> AssetImporterInspector::Open(ProjectWorkspace& workspace,
                                                                             const VirtualPath& source)
{
	if (!workspace.Database())
	{
		return Result<std::shared_ptr<AssetImporterInspector>>::Failure("Project AssetDatabase is unavailable");
	}
	try
	{
		const auto rules = workspace.ImportRuleDescriptions();
		const auto metadata = Take(workspace.Database()->Metadata(source));
		const auto descriptor =
		    *Take(rules->FindImporter(PathToUrsaString(PathFromUrsaString(source.relative).extension()),
		                              metadata.importer.find("script:") == 0 ? metadata.importer : String{}));
		auto opened = descriptor.scriptType ? OpenScripted(*workspace.Database(), source, descriptor,
		                                                   Json::parse(ToStdString(descriptor.defaultSettingsJson)))
		                                    : Open(*workspace.Database(), source);
		if (!opened)
		{
			return opened;
		}
		opened.value->resources = Take(ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver()));
		return opened;
	}
	catch (const std::exception& error)
	{
		return Result<std::shared_ptr<AssetImporterInspector>>::Failure(ToUrsaString(error.what()));
	}
}

Result<std::shared_ptr<AssetImporterInspector>>
AssetImporterInspector::OpenScripted(AssetDatabase& database, const VirtualPath& source,
                                     AssetImporterDescriptor descriptor, Json defaults,
                                     ImportSettingsDraft::Validator validate)
{
	if (!descriptor.scriptType || !defaults.contains("Fields") || !defaults["Fields"].is_object() ||
	    !defaults.contains("FieldTypes") || !defaults["FieldTypes"].is_object())
	{
		return Result<std::shared_ptr<AssetImporterInspector>>::Failure(
		    "Scripted importer Inspector requires its typed settings schema");
	}
	const auto opened = ImportSettingsDraft::Open(database, source, descriptor, defaults, std::move(validate));
	const auto metadata = database.Metadata(source);
	if (!opened || !metadata)
	{
		return Result<std::shared_ptr<AssetImporterInspector>>::Failure(!opened ? opened.error : metadata.error);
	}
	auto result = std::shared_ptr<AssetImporterInspector>(new AssetImporterInspector());
	result->database = &database;
	result->source = source;
	result->guid = metadata.value.guid;
	result->identity = descriptor.identity;
	result->version = descriptor.version;
	result->clrTypeName = descriptor.scriptType->clrTypeName;
	result->draft = opened.value;
	result->defaultPayload = std::move(defaults);
	const auto recovered = result->Recover();
	if (!recovered)
	{
		result->diagnostic = recovered.error;
	}
	return Result<std::shared_ptr<AssetImporterInspector>>::Success(std::move(result));
}

Result<Json> AssetImporterInspector::Capture() const
{
	try
	{
		if (kind == Kind::Scripted)
		{
			Json normalized = defaultPayload;
			const auto& saved = draft.Settings();
			if (!saved.empty())
			{
				if (!saved.contains("Fields") || !saved["Fields"].is_object())
				{
					return Result<Json>::Failure("Scripted importer settings have no Fields object");
				}
				normalized["Fields"] = saved["Fields"];
				for (const auto& field : defaultPayload.at("Fields").items())
				{
					if (normalized["Fields"].contains(field.key()))
					{
						continue;
					}
					bool found = false;
					if (defaultPayload.contains("FormerFieldNames") &&
					    defaultPayload["FormerFieldNames"].contains(field.key()))
					{
						for (const auto& alias : defaultPayload["FormerFieldNames"][field.key()])
						{
							if (saved["Fields"].contains(alias.get<std::string>()))
							{
								normalized["Fields"][field.key()] = saved["Fields"][alias.get<std::string>()];
								found = true;
								break;
							}
						}
					}
					if (!found)
					{
						normalized["Fields"][field.key()] = field.value();
					}
				}
			}
			return Result<Json>::Success(std::move(normalized));
		}
		if (pendingPayload)
		{
			return Result<Json>::Success(*pendingPayload);
		}
		Json fields;
		Json schema = Json::object();
		if (kind == Kind::Texture)
		{
			const auto settings = Take(TextureImportSettings::FromJson(draft.Settings()));
			fields = settings.ToJson();
			fields["type"] = static_cast<Int32>(settings.type);
			fields["filter"] = static_cast<Int32>(settings.filter);
			fields["wrap"] = static_cast<Int32>(settings.wrap);
			schema["type"] = EnumField({"Default", "Normal Map", "Linear Data"});
			schema["filter"] = EnumField({"Point", "Linear"});
			schema["wrap"] = EnumField({"Repeat", "Clamp"});
		}
		else
		{
			fields = Take(ModelImportSettings::FromJson(draft.Settings())).ToJson();
		}
		for (const auto& field : fields.items())
		{
			if (schema.contains(field.key()))
			{
				continue;
			}
			schema[field.key()] = field.value().is_boolean()           ? Field("Boolean", "System.Boolean")
			                      : field.value().is_number_unsigned() ? Field("UnsignedInteger", "System.UInt32")
			                                                           : Field("FloatingPoint", "System.Single");
		}
		return Result<Json>::Success({{"Fields", fields}, {"FieldTypes", schema}});
	}
	catch (const std::exception& error)
	{
		return Result<Json>::Failure(ToUrsaString(error.what()));
	}
}

Result<Json> AssetImporterInspector::Edit(const Json& payload)
{
	try
	{
		Json settings = payload.at("Fields");
		if (kind == Kind::Scripted)
		{
			settings = payload;
		}
		else if (kind == Kind::Texture)
		{
			const auto type = settings.at("type").get<Int32>();
			const auto filter = settings.at("filter").get<Int32>();
			const auto wrap = settings.at("wrap").get<Int32>();
			if (type < 0 || type > 2 || filter < 0 || filter > 1 || wrap < 0 || wrap > 1)
			{
				return Result<Json>::Failure("Unknown texture importer enum value");
			}
			settings["type"] = type == 0 ? "Default" : type == 1 ? "NormalMap" : "LinearData";
			settings["filter"] = filter == 0 ? "Point" : "Linear";
			settings["wrap"] = wrap == 0 ? "Repeat" : "Clamp";
			if (type != 0)
			{
				settings["sRgb"] = false;
			}
		}
		Json canonical = payload;
		if (kind == Kind::Texture)
		{
			canonical["Fields"]["sRgb"] = settings["sRgb"];
		}
		const bool changed = draft.Settings() != settings;
		const auto edited = draft.Edit(std::move(settings));
		if (!edited)
		{
			return Result<Json>::Failure(edited.error);
		}
		pendingPayload = canonical;
		if (changed)
		{
			const auto retained = SaveRecovery();
			if (!retained)
			{
				diagnostic = retained.error;
			}
		}
		return Result<Json>::Success(std::move(canonical));
	}
	catch (const std::exception& error)
	{
		return Result<Json>::Failure(ToUrsaString(error.what()));
	}
}

SerializedTargetAdapter AssetImporterInspector::Properties()
{
	const auto owner = weak_from_this();
	SerializedTargetAdapter adapter;
	adapter.resources = resources.get();
	adapter.capture = [owner]
	{
		const auto target = owner.lock();
		return target ? target->Capture() : Result<Json>::Failure("Importer Inspector target was closed");
	};
	if (database->Filesystem().CheckWritable(source))
	{
		adapter.apply = [owner](const Json& payload)
		{
			const auto target = owner.lock();
			return target ? target->Edit(payload) : Result<Json>::Failure("Importer Inspector target was closed");
		};
	}
	return adapter;
}

Result<bool> AssetImporterInspector::Apply()
{
	if (recoveryConflict)
	{
		return Result<bool>::Failure(diagnostic);
	}
	if (kind == Kind::Scripted && draft.HasModified())
	{
		const auto properties = SerializedObject::Create(Properties());
		const auto valid = properties ? properties.value->ValidateAll() : Result<void>::Failure(properties.error);
		if (!valid)
		{
			diagnostic = valid.error;
			return Result<bool>::Failure(diagnostic);
		}
		const auto payload = Capture();
		if (!payload)
		{
			return Result<bool>::Failure(payload.error);
		}
		const auto normalized = draft.Edit(payload.value);
		if (!normalized)
		{
			return Result<bool>::Failure(normalized.error);
		}
	}
	auto result = draft.Apply();
	if (result)
	{
		pendingPayload.reset();
	}
	diagnostic = result ? String{} : result.error;
	if (result)
	{
		const auto cleared = ClearRecovery();
		if (!cleared)
		{
			diagnostic = cleared.error;
		}
	}
	return result;
}

Result<void> AssetImporterInspector::Revert()
{
	auto reverted = draft;
	auto result = reverted.Revert();
	diagnostic = result ? String{} : result.error;
	if (result)
	{
		const auto cleared = ClearRecovery();
		if (!cleared)
		{
			diagnostic = cleared.error;
			return cleared;
		}
		draft = std::move(reverted);
		pendingPayload.reset();
		recoveryConflict = false;
	}
	return result;
}

Result<void> AssetImporterInspector::Discard()
{
	const auto cleared = ClearRecovery();
	if (!cleared)
	{
		diagnostic = cleared.error;
		return cleared;
	}
	draft.Discard();
	pendingPayload.reset();
	recoveryConflict = false;
	diagnostic.clear();
	return {};
}

VirtualPath AssetImporterInspector::RecoveryPath() const
{
	return VirtualPath::Parse("lib:EditorDrafts/Importers/" + guid.ToString() + ".json").value;
}

Result<void> AssetImporterInspector::ClearRecovery() const
{
	auto& files = database->Filesystem();
	if (!files.IsMounted(editorLibraryScheme))
	{
		return {};
	}
	const auto exists = files.Exists(RecoveryPath());
	return !exists ? Result<void>::Failure(exists.error) : exists.value ? files.Remove(RecoveryPath()) : Result<void>{};
}

Result<void> AssetImporterInspector::SaveRecovery() const
{
	if (recoveryConflict)
	{
		return {};
	}
	if (!draft.HasModified())
	{
		return ClearRecovery();
	}
	Json value{{"version", 1},
	           {"guid", ToStdString(guid.ToString())},
	           {"importer", ToStdString(identity)},
	           {"importerVersion", version},
	           {"baseline", draft.PersistentSettings()},
	           {"draft", draft.Settings()}};
	if (pendingPayload)
	{
		value["payload"] = *pendingPayload;
	}
	const auto text = value.dump();
	return database->Filesystem().WriteAtomic(RecoveryPath(), Vector<UInt8>(text.data(), text.data() + text.size()));
}

Result<void> AssetImporterInspector::Recover()
{
	auto& files = database->Filesystem();
	if (!files.IsMounted(editorLibraryScheme))
	{
		return {};
	}
	const auto exists = files.Exists(RecoveryPath());
	if (!exists || !exists.value)
	{
		return exists ? Result<void>{} : Result<void>::Failure(exists.error);
	}
	try
	{
		const auto saved = Json::parse(ToStdString(Take(files.ReadText(RecoveryPath()))));
		if (saved.at("version") != 1 || saved.at("guid") != ToStdString(guid.ToString()) ||
		    saved.at("importer") != ToStdString(identity) || saved.at("baseline") != draft.PersistentSettings())
		{
			recoveryConflict = true;
			return Result<void>::Failure("Unapplied importer settings were retained, but the source settings or "
			                             "importer identity changed. Revert discards the retained draft.");
		}
		const auto edited = draft.Edit(saved.at("draft"));
		if (!edited)
		{
			return edited;
		}
		if (saved.contains("payload"))
		{
			pendingPayload = saved.at("payload");
		}
		return {};
	}
	catch (const std::exception& error)
	{
		recoveryConflict = true;
		return Result<void>::Failure("Cannot recover importer settings draft: " + ToUrsaString(error.what()));
	}
}

Result<bool> AssetImporterInspector::DrawDefault(const std::function<void(const VirtualPath&)>& requestImport,
                                                 bool canApply, ProjectWorkspace* workspace, const CustomDraw& custom)
{
	try
	{
		ManagedEditorContext context;
		context.workspace = workspace;
		context.serializedTarget = Properties();
		context.targetUsesDocument = false;
		context.assetSelection = AssetReference{guid, 0};
		context.importerInspector = shared_from_this();
		context.requestImport = requestImport;
		context.canApplyImporter = canApply;
		context.validateTarget = [this]
		{
			const auto current = database->Metadata(source);
			return current && current.value.guid == guid
			           ? Result<void>{}
			           : Result<void>::Failure("Importer Inspector source was removed or replaced");
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
			changed = drawn && drawn.value != 0;
		}
		if (!context.importerControlsDrawn)
		{
			DrawApplyRevert(requestImport, canApply);
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
		return Result<bool>::Failure(diagnostic);
	}
}

void AssetImporterInspector::DrawApplyRevert(const std::function<void(const VirtualPath&)>& requestImport,
                                             bool canApply)
{
	const bool writable = static_cast<bool>(database->Filesystem().CheckWritable(source));
	ImGui::BeginDisabled(!HasModified());
	if (Icons::Button("Revert"))
	{
		Revert();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!writable || !canApply);
	if (Icons::Button("Apply"))
	{
		const auto applied = Apply();
		if (applied && applied.value && requestImport)
		{
			requestImport(source);
		}
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();
}

ImporterSelectionResult
AssetImporterInspector::DrawSelectionPrompt(const std::function<void(const VirtualPath&)>& requestImport, bool canApply)
{
	if (!ImGui::BeginPopupModal("Unapplied Import Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return ImporterSelectionResult::Pending;
	}
	ImGui::TextUnformatted("Apply the current importer settings before changing selection?");
	ImGui::TextUnformatted(source.ToString().c_str());
	auto result = ImporterSelectionResult::Pending;
	ImGui::BeginDisabled(!canApply || !database->Filesystem().CheckWritable(source));
	if (Icons::Button("Apply"))
	{
		const auto applied = Apply();
		if (applied)
		{
			if (applied.value && requestImport)
			{
				requestImport(source);
			}
			result = ImporterSelectionResult::Accepted;
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (Icons::Button("Discard") && Discard())
	{
		result = ImporterSelectionResult::Accepted;
	}
	ImGui::SameLine();
	if (Icons::Button("Cancel"))
	{
		result = ImporterSelectionResult::Cancelled;
	}
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	if (result != ImporterSelectionResult::Pending)
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
	return result;
}
} // namespace Ursa::Editor
