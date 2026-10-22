#include "TestSupport.hpp"
#include <Scripting/ManagedSceneApi.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#if defined(URSA_DOTNET_TEST_EDITOR)
#include <Scripting/Bindings/ImportServices.hpp>
#include <Import.generated.hpp>
#include <Assets/ManagedImportRules.hpp>
#include <Assets/ScriptedAssetImporter.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Assets/AssetImporterInspector.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/TextureResource.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <Vfs/MemoryFileProvider.hpp>
#include <Scripting/Bindings/BindingRequirements.hpp>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#endif

namespace Ursa::Test
{
#if defined(URSA_DOTNET_TEST_EDITOR)
namespace
{
String ImportFingerprint(const Vector<UInt8>& bytes)
{
	UInt64 value = 14695981039346656037ull;
	for (UInt8 byte : bytes)
	{
		value = (value ^ byte) * 1099511628211ull;
	}
	std::ostringstream text;
	text << std::hex << std::setw(16) << std::setfill('0') << value;
	return ToUrsaString(text.str());
}

Vector<UInt8> ImportJson(const nlohmann::json& value)
{
	const auto text = value.dump();
	const auto* begin = reinterpret_cast<const UInt8*>(text.data());
	return {begin, begin + text.size()};
}

Vector<UInt8> ImportPng()
{
	Vector<UInt8> bytes;
	Vector<UInt8> pixels(8 * 8 * 4, 255);
	const auto append = [](void* context, void* data, int size)
	{
		auto& output = *static_cast<Vector<UInt8>*>(context);
		const auto* begin = static_cast<const UInt8*>(data);
		output.insert(output.end(), begin, begin + size);
	};
	Expect(stbi_write_png_to_func(append, &bytes, 8, 8, 4, pixels.data(), 8 * 4) != 0, "PNG fixture encoding failed");
	return bytes;
}

Vector<UInt8> ImportFile(const char* filename)
{
	std::ifstream input(std::filesystem::u8path(filename), std::ios::binary | std::ios::ate);
	Expect(input && input.tellg() > 0, "Import FBX test fixture is missing");
	Vector<UInt8> bytes(static_cast<Size>(input.tellg()));
	input.seekg(0);
	input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Expect(bool(input), "Import FBX test fixture could not be read");
	return bytes;
}
} // namespace

void TestManagedImporterRegistration(DotNetHost& host, VirtualFileSystem& filesystem, const VirtualPath& gameContent)
{
	Done(host.LoadEditorApi(Path("ursa:Ursa.Editor.dll")));
	const auto catalogPath = Path("user:ImportRules/Catalog.json");
	const auto contentPath = Path("user:ImportRules/Editor.json");
	const auto assemblyPath = Path("ursa:TestScripts/Ursa.EditorFixtures.dll");
	const auto assembly = Take(filesystem.Read(assemblyPath));
	auto catalog =
	    nlohmann::json::parse(ToStdString(Take(filesystem.ReadText(Path("prj:Managed/ScriptTypeCatalog.json")))));
	catalog["Scripts"].push_back({{"SourceGuid", "22222222-2222-4222-8222-222222222222"},
	                              {"LocalFileId", 101u},
	                              {"Kind", "ScriptedImporter"},
	                              {"ScriptType", "test.editor-importer"},
	                              {"Version", 1u},
	                              {"Group", "Editor"},
	                              {"ClrTypeName", "Ursa.Examples.DatasetImporter, Ursa.EditorFixtures"},
	                              {"Fields", nlohmann::json::array()}});
	for (const auto& description : Vector<std::pair<String, String>>{{"ScriptedImporter", "ValidationImporter"},
	                                                                 {"ScriptableObject", "EditorOnlyImportAsset"}})
	{
		const UInt64 localId = description.first == "ScriptedImporter" ? 102 : 103;
		catalog["Scripts"].push_back(
		    {{"SourceGuid", "22222222-2222-4222-8222-222222222222"},
		     {"LocalFileId", localId},
		     {"Kind", ToStdString(description.first)},
		     {"ScriptType", "test." + ToStdString(description.second)},
		     {"Version", 1u},
		     {"Group", "Editor"},
		     {"ClrTypeName", "Ursa.EditorFixtures." + ToStdString(description.second) + ", Ursa.EditorFixtures"},
		     {"Fields", nlohmann::json::array()}});
	}
	UInt64 processorId = 201;
	for (const String name : {"FirstTexturePostprocessor", "TextureAndModelPostprocessor", "LastModelPostprocessor"})
	{
		catalog["Scripts"].push_back(
		    {{"SourceGuid", "22222222-2222-4222-8222-222222222222"},
		     {"LocalFileId", processorId++},
		     {"Kind", "AssetPostprocessor"},
		     {"ScriptType", "test." + ToStdString(name)},
		     {"Version", 1u},
		     {"Group", "Editor"},
		     {"ClrTypeName", "Ursa.EditorFixtures." + ToStdString(name) + ", Ursa.EditorFixtures"},
		     {"Fields", nlohmann::json::array()}});
	}
	for (const String name : {"TextureImportRules", "ModelImportRules"})
	{
		catalog["Scripts"].push_back({{"SourceGuid", "22222222-2222-4222-8222-222222222222"},
		                              {"LocalFileId", processorId++},
		                              {"Kind", "AssetPostprocessor"},
		                              {"ScriptType", "test." + ToStdString(name)},
		                              {"Version", 1u},
		                              {"Group", "Editor"},
		                              {"ClrTypeName", "Ursa.Examples." + ToStdString(name) + ", Ursa.EditorFixtures"},
		                              {"Fields", nlohmann::json::array()}});
	}
	auto closureAssemblies =
	    nlohmann::json::parse(ToStdString(Take(filesystem.ReadText(gameContent)))).at("Assemblies");
	closureAssemblies.push_back({{"Name", "Ursa.EditorFixtures"},
	                             {"Path", ToStdString(assemblyPath.ToString())},
	                             {"Fingerprint", ToStdString(ImportFingerprint(assembly))}});
	const auto catalogBytes = ImportJson(catalog);
	Done(filesystem.WriteAtomic(catalogPath, catalogBytes));
	Done(filesystem.WriteAtomic(contentPath,
	                            ImportJson({{"Version", 2u},
	                                        {"Group", "Editor"},
	                                        {"RequiredBindings", Scripting::Bindings::ScriptBindingRequirements(true)},
	                                        {"TypeCatalog", ToStdString(catalogPath.ToString())},
	                                        {"CatalogFingerprint", ToStdString(ImportFingerprint(catalogBytes))},
	                                        {"Assemblies", closureAssemblies}})));
	const UInt64 session = Take(host.CreateImportSession());
	Done(host.LoadScriptContent(session, gameContent));
	Done(host.LoadScriptContent(session, contentPath));
	auto rules = Take(Ursa::Editor::DiscoverManagedImportRules(host, session, "compiled-closure", 5));
	const auto textureProcessors = rules.PostprocessorsFor("ursa.Texture", projectScheme);
	const auto modelProcessors = rules.PostprocessorsFor("ursa.Model", projectScheme);
	Expect(textureProcessors.size() == 4 && textureProcessors[0].type.localFileId == 201 &&
	           textureProcessors[0].version == 7 && textureProcessors[0].order == -10 &&
	           textureProcessors[1].type.localFileId == 202 && modelProcessors.size() == 3 &&
	           modelProcessors[0].type.localFileId == 202 && modelProcessors[1].type.localFileId == 203 &&
	           rules.PostprocessorsFor("ursa.Texture", engineScheme).empty(),
	       "Managed postprocessor discovery lost version, callback targets or stable order");
	const auto descriptor = Take(rules.FindImporter(".ursadataset"));
	Expect(descriptor->version == 5 && descriptor->identity == "script:22222222-2222-4222-8222-222222222222:101",
	       "Managed ScriptedImporter registration lost stable identity or attribute version");
	Expect(!host.PrepareScriptedImporters(session, {}), "Frozen import registry was replaced in place");
	const auto native = Take(Ursa::Editor::AssetImportRules::Create());
	Expect(!native.FindImporter(".ursadataset", descriptor->identity),
	       "Removed required importer silently fell back to default");
	{
		Ursa::Editor::Bindings::ImportInvocation scope(session, 5, {"prj", "Fixture.ursadataset"},
		                                               Ursa::Editor::Bindings::ImportPhase::ScriptedImport,
		                                               [] { return UInt64(5); });
		Done(host.BindScriptSession(session, scope.CpuApi()));
		scope.SetSourceBytes(ImportJson({{"size", 2}, {"label", "Generated dataset"}}));
		const Scripting::Bindings::BindingModuleReference capability{
		    Scripting::Bindings::Import::moduleId, &Scripting::Bindings::Import::NativeDescriptor(), scope.Token()};
		Done(host.BeginImportInvocation(session, 5, scope.Identity(), capability));
		Done(host.InvokeScriptedImporter(session, descriptor->identity));
		Expect(!host.InvokeScriptedImporter(session, "script:22222222-2222-4222-8222-222222222222:999"),
		       "Missing importer invocation succeeded");
		Done(host.EndImportInvocation(session, 5, scope.Identity()));
		Done(host.DetachImportResources(session));
	}
	VirtualFileSystem inputs;
	Done(inputs.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(inputs.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const VirtualPath source{projectScheme, "Generated.ursadataset"};
	const VirtualPath definition{projectScheme, "Editor/DatasetImporter.cs"};
	const auto sourceBytes = ImportJson({{"size", 2}, {"label", "Generated dataset"}});
	Done(inputs.WriteAtomic(source, sourceBytes));
	Done(inputs.WriteAtomic(definition, sourceBytes));
	auto database = Take(Ursa::Editor::AssetDatabase::Open(inputs));
	Done(database->Refresh());
	Done(database->ApplyGuidMigration({{definition, Take(ResourceId::Parse("22222222-2222-4222-8222-222222222222"))}}));
	Ursa::Editor::ProjectAssetImporter service;
	const auto sharedRules = std::make_shared<const Ursa::Editor::AssetImportRules>(rules);
	Done(Ursa::Editor::RegisterScriptedAssetImporters(service, sharedRules, host, session));
	const auto imported = Take(service.Import(*database, source));
	{
		const VirtualPath configured{projectScheme, "Settings.ursadataset"};
		Done(inputs.WriteAtomic(configured,
		                        ImportJson({{"size", 2}, {"label", "Configured settings"}, {"main", "data"}})));
		Done(database->Refresh());
		const auto initial = Take(service.Import(*database, configured));
		auto inspector = Take(Ursa::Editor::AssetImporterInspector::OpenScripted(
		    *database, configured, *descriptor, nlohmann::json::parse(ToStdString(descriptor->defaultSettingsJson))));
		auto properties = Take(Ursa::Editor::SerializedObject::Create(inspector->Properties()));
		Done(Take(properties->FindProperty("/Fields/scale")).SetValue(2.0));
		Done(properties->Apply());
		Expect(inspector->HasModified() && Take(database->Metadata(configured)).importSettings.empty(),
		       "Scripted importer field edit persisted before Apply");
		Expect(Take(inspector->Apply()), "Scripted importer settings Apply did not save its typed fields");
		const auto rebuilt = Take(service.Import(*database, configured));
		Expect(!rebuilt.cacheHit && rebuilt.mainObjectLocalFileId == initial.mainObjectLocalFileId,
		       "Scripted importer settings reused its old result or changed its output identity");
		auto view = Take(ResourceSession::Open(inputs, [&](ResourceId guid, UInt64 local)
		                                       { return database->Resolve(guid, local); }));
		const auto graph = Take(LoadScriptableAssetGraph(*view, {rebuilt.guid, 0}));
		Expect(graph.assets.front().Payload()["Fields"]["size"] == 4 &&
		           Take(service.Import(*database, configured)).cacheHit,
		       "Persisted ScriptedImporter fields did not reach the CLR callback or its cache");
		Done(properties->Update());
		Done(Take(properties->FindProperty("/Fields/scale")).SetValue(0.0));
		Done(properties->Apply());
		Expect(Take(inspector->Apply()) && !service.Import(*database, configured) &&
		           Take(database->Metadata(configured)).importSettings["Fields"]["scale"] == 0,
		       "A callback failure rolled back successfully saved scripted importer settings");
		for (const auto& artifact : rebuilt.artifacts)
		{
			Expect(Take(database->ResolveImportedArtifact(rebuilt.guid, artifact.localFileId)).contentFingerprint ==
			           artifact.contentFingerprint,
			       "A configured importer failure replaced its old complete outputs");
		}
	}
	Expect(!imported.cacheHit && imported.artifacts.size() == 5 && imported.mainObjectLocalFileId != 0,
	       "Scripted format did not publish its complete typed output set");
	Expect(Take(service.Import(*database, source)).cacheHit, "Unchanged scripted format missed its cache");
	const auto outputId = [&](const String& key)
	{ return Take(Ursa::Editor::StableImportObjectId("script:22222222-2222-4222-8222-222222222222:101", key)); };
	const auto meshId = outputId("mesh");
	const auto materialId = outputId("material");
	const auto textureId = outputId("texture");
	const auto dataId = outputId("data");
	const auto prefabId = outputId("prefab");
	const auto formal = Take(database->CurrentImport(source));
	Expect(formal.mainObjectLocalFileId == prefabId &&
	           Take(database->Resolve(imported.guid)) == Take(database->Resolve(imported.guid, prefabId)),
	       "Scripted main object alias did not select its stable prefab output");
	auto resources = Take(
	    ResourceSession::Open(inputs, [&](ResourceId guid, UInt64 local) { return database->Resolve(guid, local); }));
	Expect(Take(Take(resources->LoadMesh(imported.guid, meshId))->GetData()).positions[1].x == 2,
	       "Published generated Mesh data differs");
	Expect(Take(resources->LoadMaterial(imported.guid, materialId))->Parameters().baseColor.green == 1,
	       "Published generated Material data differs");
	const auto generatedMaterial = Take(resources->LoadMaterial(imported.guid, materialId));
	Expect(generatedMaterial->TextureReference(MaterialTextureSlot::BaseColor).localFileId == textureId &&
	           generatedMaterial->ResolvedTexture(MaterialTextureSlot::BaseColor) &&
	           generatedMaterial->ResolvedTexture(MaterialTextureSlot::BaseColor)->GetPixels()[0] == 255,
	       "Generated Material did not retain its candidate Texture2D reference and pixels");
	Expect(Take(resources->LoadTexture(imported.guid, textureId))->GetPixels()[0] == 255,
	       "Published generated Texture2D pixels differ");
	const VirtualPath linkedMaterialPath{projectScheme, "LinkedMaterial.ursamat"};
	const auto linkedMaterialGuid = ResourceId::Generate();
	Done(SaveMaterialResource(inputs, linkedMaterialPath, linkedMaterialGuid, *generatedMaterial, {}));
	auto linkedDraft = Take(Ursa::Editor::MaterialDraft::Load(inputs, linkedMaterialPath, *resources));
	Done(linkedDraft->Save(inputs, linkedMaterialPath, linkedMaterialGuid, resources->Index(),
	                       [&](ResourceId guid, UInt64 local) { return database->Resolve(guid, local); }));
	const auto reopenedDraft = Take(Ursa::Editor::MaterialDraft::Load(inputs, linkedMaterialPath, *resources));
	Expect(reopenedDraft->Material().TextureReference(MaterialTextureSlot::BaseColor).localFileId == textureId &&
	           reopenedDraft->Material().ResolvedTexture(MaterialTextureSlot::BaseColor)->GetPixels()[0] == 255,
	       "Saving and reopening an author material lost its imported texture sub-asset reference");
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	ComponentLoadContext load(*resources);
	const auto prefab =
	    Take(LoadPrefabResource(inputs, Take(database->Resolve(imported.guid, prefabId)), registry, load));
	Expect(prefab.scene->RootGameObjects().size() == 1 && prefab.scene->RootGameObjects()[0]->Children().size() == 1,
	       "Published prefab hierarchy differs");
	const auto data = Take(LoadScriptableAssetGraph(*resources, {imported.guid, dataId}));
	Expect(data.assets.size() == 1 &&
	           data.assets[0].Payload()["Fields"]["mesh"]["$ursaReference"]["Resource"]["kind"] == "asset",
	       "Published ScriptableObject retained an internal candidate reference");
	const auto forced = Take(service.Import(*database, source, true));
	Expect(!forced.cacheHit && forced.mainObjectLocalFileId == prefabId,
	       "Forced import changed stable output identity");
	const auto afterForce = Take(database->CurrentImport(source));
	for (Size index = 0; index < formal.artifacts.size(); ++index)
	{
		Expect(formal.artifacts[index].contentFingerprint == afterForce.artifacts[index].contentFingerprint,
		       "Repeated scripted generation changed deterministic output bytes");
	}
	Ursa::Editor::ProjectAssetImporter removed;
	Expect(!removed.Import(*database, source),
	       "Missing required importer fell through the project service's default importer");
	Done(inputs.WriteAtomic(source, ImportJson({{"size", 2}, {"label", "Generated dataset"}, {"main", "data"}})));
	const auto withMainData = Take(service.Import(*database, source));
	Expect(withMainData.mainObjectLocalFileId == dataId,
	       "Scripted importer could not select a ScriptableObject as main");
	resources = Take(
	    ResourceSession::Open(inputs, [&](ResourceId guid, UInt64 local) { return database->Resolve(guid, local); }));
	const auto aliasGraph = Take(LoadScriptableAssetGraph(*resources, {imported.guid, 0}));
	Expect(aliasGraph.root.localFileId == dataId && aliasGraph.assets.size() == 1,
	       "ScriptableObject main alias was not canonicalized during runtime graph loading");
	// Dependency declarations are exercised through the same public context used by project scripts.
	const VirtualPath dependent{projectScheme, "Dependent.ursadataset"};
	const VirtualPath configuration{projectScheme, "ImportConfig.txt"};
	Done(inputs.WriteAtomic(configuration, Vector<UInt8>{'o', 'n', 'e'}));
	nlohmann::json dependentInput{{"size", 2},
	                              {"label", "Dependent"},
	                              {"sourceDependency", "prj:ImportConfig.txt"},
	                              {"artifactGuid", ToStdString(imported.guid.ToString())}};
	Done(inputs.WriteAtomic(dependent, ImportJson(dependentInput)));
	Done(database->Refresh());
	const auto firstDependent = Take(service.Import(*database, dependent));
	Expect(!firstDependent.cacheHit && Take(service.Import(*database, dependent)).cacheHit,
	       "Declared source and artifact dependencies did not produce a reusable import");
	const auto firstStatus = Take(database->CurrentImport(dependent));
	Expect(std::find(firstStatus.dependencies.begin(), firstStatus.dependencies.end(), configuration) !=
	           firstStatus.dependencies.end(),
	       "Context source read was not tracked in the dependency set");
	Done(inputs.WriteAtomic(configuration, Vector<UInt8>{'t', 'w', 'o'}));
	Expect(!Take(service.Import(*database, dependent)).cacheHit, "Changed source dependency reused stale outputs");
	Done(inputs.WriteAtomic(source, ImportJson({{"size", 2}, {"label", "Changed dependency"}, {"main", "data"}})));
	Expect(!Take(service.Import(*database, dependent)).cacheHit,
	       "Outdated artifact dependency was not completed before rebuilding its consumer");
	Expect(Take(database->CurrentImport(source)).upToDate, "Scheduler did not publish the prerequisite import");
	// Changes that reproduce the same artifact bytes must not invalidate a content dependency.
	Done(inputs.WriteAtomic(
	    source, ImportJson({{"size", 2}, {"label", "Changed dependency"}, {"main", "data"}, {"unused", true}})));
	Expect(Take(service.Import(*database, dependent)).cacheHit,
	       "Identical completed artifact content invalidated its consumer");
	const VirtualPath leaf{projectScheme, "Leaf.ursadataset"};
	Done(inputs.WriteAtomic(leaf, ImportJson({{"size", 2}, {"label", "Leaf"}})));
	Done(database->Refresh());
	const auto leafGuid = Take(database->Metadata(leaf)).guid;
	Done(inputs.WriteAtomic(source, ImportJson({{"size", 2},
	                                            {"label", "Changed dependency"},
	                                            {"main", "data"},
	                                            {"artifactGuid", ToStdString(leafGuid.ToString())}})));
	Expect(Take(service.Import(*database, dependent)).cacheHit,
	       "Scheduling a new transitive dependency changed an identical direct artifact");
	Done(inputs.WriteAtomic(leaf, ImportJson({{"size", 3}, {"label", "Leaf"}})));
	Expect(Take(service.Import(*database, dependent)).cacheHit && Take(database->CurrentImport(leaf)).upToDate,
	       "Transitive dependency was not checked or leaked its content hash into the direct consumer");
	const auto declared = dependentInput;
	dependentInput["declaredSourceDependency"] = "prj:ImportConfig.txt";
	Done(inputs.WriteAtomic(dependent, ImportJson(dependentInput)));
	Take(service.Import(*database, dependent));
	Done(inputs.WriteAtomic(configuration, Vector<UInt8>{'t', 'h', 'r', 'e', 'e'}));
	Expect(!Take(service.Import(*database, dependent)).cacheHit, "Explicit source declaration was not tracked");
	dependentInput = declared;
	Take(service.Import(*database, dependent));
	const auto successfulDependent = Take(database->CurrentImport(dependent));
	const auto expectPreserved = [&]
	{
		const auto retained = Take(database->ResolveImportedArtifact(firstDependent.guid, dataId));
		const auto old = std::find_if(successfulDependent.artifacts.begin(), successfulDependent.artifacts.end(),
		                              [&](const auto& item) { return item.localFileId == dataId; });
		Expect(old != successfulDependent.artifacts.end() && old->contentFingerprint == retained.contentFingerprint,
		       "Failed import replaced a previously complete output set");
	};
	dependentInput["sourceDependency"] = "prj:Missing.txt";
	Done(inputs.WriteAtomic(dependent, ImportJson(dependentInput)));
	const auto missing = service.Import(*database, dependent);
	Expect(!missing && missing.error.find("Missing.txt") != String::npos &&
	           missing.error.find("DatasetImporter") != String::npos &&
	           missing.error.find("ScriptedImport") != String::npos,
	       "Missing dependency diagnostic omitted source, type or phase");
	expectPreserved();
	dependentInput.erase("sourceDependency");
	dependentInput["artifactGuid"] = "33333333-3333-4333-8333-333333333333";
	Done(inputs.WriteAtomic(dependent, ImportJson(dependentInput)));
	Expect(!service.Import(*database, dependent), "Missing artifact dependency was accepted");
	expectPreserved();
	dependentInput["artifactGuid"] = ToStdString(firstDependent.guid.ToString());
	Done(inputs.WriteAtomic(dependent, ImportJson(dependentInput)));
	const auto selfCycle = service.Import(*database, dependent);
	Expect(!selfCycle && selfCycle.error.find("Cyclic") != String::npos, "Self artifact dependency was accepted");
	dependentInput["artifactGuid"] = ToStdString(imported.guid.ToString());
	Done(inputs.WriteAtomic(dependent, ImportJson(dependentInput)));
	Done(inputs.WriteAtomic(
	    source,
	    ImportJson({{"size", 2}, {"label", "Cycle"}, {"artifactGuid", ToStdString(firstDependent.guid.ToString())}})));
	const auto cycle = service.Import(*database, dependent);
	Expect(!cycle && cycle.error.find("Cyclic") != String::npos &&
	           cycle.error.find("Generated.ursadataset") != String::npos &&
	           cycle.error.find("Dependent.ursadataset") != String::npos,
	       "Multi-asset cycle did not report its dependency chain");
	expectPreserved();
	dependentInput.erase("artifactGuid");
	dependentInput["error"] = "Rejected completed candidate";
	Done(inputs.WriteAtomic(dependent, ImportJson(dependentInput)));
	const auto explicitError = service.Import(*database, dependent);
	Expect(!explicitError && explicitError.error.find("Rejected completed candidate") != String::npos,
	       "Explicit import error did not reject the entire output set");
	expectPreserved();
	dependentInput.erase("error");
	dependentInput["warning"] = "Review generated geometry";
	Done(inputs.WriteAtomic(dependent, ImportJson(dependentInput)));
	const auto warned = Take(service.Import(*database, dependent));
	Expect(warned.diagnostics.size() == 1 && warned.diagnostics[0].find("DatasetImporter") != String::npos,
	       "Import warning was lost or omitted its extension type");
	Done(inputs.WriteAtomic(source, ImportJson({{"size", 2}, {"label", "Generated dataset"}, {"main", "data"}})));
	Take(service.Import(*database, source));
	const VirtualPath validationSource{projectScheme, "Validation.ursavalidation"};
	const String validMode = "valid";
	Done(inputs.WriteAtomic(validationSource, Vector<UInt8>(validMode.begin(), validMode.end())));
	Done(database->Refresh());
	const auto validOutput = Take(service.Import(*database, validationSource));
	Expect(validOutput.artifacts.size() == 1, "Validation fixture failed its valid control import");
	const auto retainedOutput = validOutput.artifacts[0];
	for (const String mode : {"duplicate-key", "duplicate-object", "no-main", "bad-index", "bad-pixels", "destroyed",
	                          "editor-type", "unregistered-reference", "expired"})
	{
		Done(inputs.WriteAtomic(validationSource, Vector<UInt8>(mode.begin(), mode.end())));
		const auto rejected = service.Import(*database, validationSource);
		Expect(!rejected && rejected.error.find("ValidationImporter") != String::npos,
		       "Invalid output set was accepted or lost its importer diagnostic");
		const auto retained = Take(database->ResolveImportedArtifact(validOutput.guid));
		Expect(retained.localFileId == retainedOutput.localFileId &&
		           retained.contentFingerprint == retainedOutput.contentFingerprint &&
		           Take(inputs.Read(retained.path)) == Take(inputs.Read(retainedOutput.path)),
		       "Rejected candidate replaced its last complete output");
	}
	Done(inputs.WriteAtomic(validationSource, Vector<UInt8>(validMode.begin(), validMode.end())));
	Expect(Take(service.Import(*database, validationSource)).artifacts.size() == 1,
	       "Rejected extension was disabled instead of retrying its required rule");
	const auto persistentInput = Take(service.Import(*database, source));
	const VirtualPath persistentSource{projectScheme, "Persistent.ursavalidation"};
	const String referenceSuffix = "|" + persistentInput.guid.ToString() + "|" +
	                               ToUrsaString(std::to_string(persistentInput.mainObjectLocalFileId));
	const auto persistentMode = "read-persistent" + referenceSuffix;
	Done(inputs.WriteAtomic(persistentSource, Vector<UInt8>(persistentMode.begin(), persistentMode.end())));
	Done(database->Refresh());
	const auto persistent = Take(service.Import(*database, persistentSource));
	Expect(persistent.artifacts.size() == 1 &&
	           Take(database->CurrentImport(persistentSource)).settings["artifacts"].size() >= 4,
	       "Typed persistent resource loading did not complete or record its artifact dependencies");
	const auto originalPersistent = Take(database->ResolveImportedArtifact(persistent.guid));
	for (const String mode : {"republish-persistent", "mutate-persistent", "destroy-persistent"})
	{
		const auto input = mode + referenceSuffix;
		Done(inputs.WriteAtomic(persistentSource, Vector<UInt8>(input.begin(), input.end())));
		const auto rejected = service.Import(*database, persistentSource);
		Expect(!rejected, "A persistent asset was republished, modified or destroyed during import");
		Expect(Take(database->ResolveImportedArtifact(persistent.guid)).contentFingerprint ==
		           originalPersistent.contentFingerprint,
		       "A rejected persistent asset operation replaced the previous output");
	}
	const VirtualPath image{projectScheme, "Processed.png"};
	const VirtualPath pixelConfig{projectScheme, "PixelConfig.txt"};
	const auto png = ImportPng();
	Done(inputs.WriteAtomic(image, png));
	Done(inputs.WriteAtomic(pixelConfig, Vector<UInt8>{'0', '.', '5'}));
	Done(database->Refresh());
	Done(Ursa::Editor::TextureImporter::SetSettings(*database, image, {}));
	const auto persistentSettings = Take(inputs.Read(Path("prj:Processed.png.meta")));
	const auto imageImport = Take(service.Import(*database, image));
	const auto imageStatus = Take(database->CurrentImport(image));
	const auto processedTexture = Take(LoadTextureResource(inputs, Take(database->Resolve(imageImport.guid)))).texture;
	Expect(processedTexture->GetWidth() == 2 && processedTexture->GetHeight() == 2 &&
	           processedTexture->GetColorSpace() == TextureColorSpace::Linear &&
	           processedTexture->GetMipLevels() == 2 && !processedTexture->DefaultSampler().linear &&
	           !processedTexture->DefaultSampler().repeat,
	       "Preprocessor effective dimensions, color space, sampler or mip settings did not reach the output");
	for (const auto& pixels :
	     Vector<Vector<UInt8>>{processedTexture->GetPixels(), processedTexture->GetMipmaps()[0].pixels})
	{
		for (Size index = 0; index < pixels.size(); index += 4)
		{
			Expect(pixels[index] == 128 && pixels[index + 1] == 255 && pixels[index + 2] == 0 &&
			           pixels[index + 3] == 128,
			       "Base pixels or lower mip did not reflect the ordered postprocessor changes");
		}
	}
	Expect(Take(inputs.Read(image)) == png && Take(inputs.Read(Path("prj:Processed.png.meta"))) == persistentSettings &&
	           imageStatus.settings["settings"]["maxDimension"] == 4096 &&
	           imageStatus.settings["effective"]["maxDimension"] == 2,
	       "Import callbacks changed source bytes or persistent settings instead of the effective copy");
	Expect(Take(service.Import(*database, image)).cacheHit, "Unchanged processed texture did not hit its cache");
	Done(inputs.WriteAtomic(pixelConfig, Vector<UInt8>{'0', '.', '2', '5'}));
	Expect(!Take(service.Import(*database, image)).cacheHit, "Postprocessor source read was not tracked");
	const auto changedTexture = Take(LoadTextureResource(inputs, Take(database->Resolve(imageImport.guid)))).texture;
	Expect(changedTexture->GetPixels()[0] == 64 && changedTexture->GetMipmaps()[0].pixels[0] == 64,
	       "Postprocessor did not retain its per-invocation state from pre to post or rebuild the mip chain");
	const auto lastImage = Take(database->ResolveImportedArtifact(imageImport.guid));
	for (const String invalid : {"error", "NaN", "invalid-number"})
	{
		Done(inputs.WriteAtomic(pixelConfig, Vector<UInt8>(invalid.begin(), invalid.end())));
		const auto rejected = service.Import(*database, image);
		Expect(!rejected && rejected.error.find("TextureAndModelPostprocessor") != String::npos,
		       "Processor error was swallowed or lost its source extension diagnostic");
		Expect(Take(database->ResolveImportedArtifact(imageImport.guid)).contentFingerprint ==
		           lastImage.contentFingerprint,
		       "Failed texture postprocessor replaced the successful output");
	}
	const VirtualPath unrelatedImage{projectScheme, "Unaffected.png"};
	Done(inputs.WriteAtomic(unrelatedImage, png));
	Done(database->Refresh());
	Expect(Take(service.Import(*database, unrelatedImage)).artifacts.size() == 1,
	       "Processor failure prevented unrelated texture imports");
	Done(inputs.WriteAtomic(pixelConfig, Vector<UInt8>{'0', '.', '5'}));
	Expect(!Take(service.Import(*database, image)).cacheHit &&
	           Take(LoadTextureResource(inputs, Take(database->Resolve(imageImport.guid)))).texture->GetPixels()[0] ==
	               128,
	       "Failed processor was skipped on retry instead of executing its required chain");
	const String validModelMode = "valid";
	Done(inputs.WriteAtomic({projectScheme, "ModelConfig.txt"},
	                        Vector<UInt8>(validModelMode.begin(), validModelMode.end())));
	for (const auto& fixture : Vector<std::pair<const char*, String>>{
	         {URSA_IMPORT_ANIMATED_FBX, "AnimatedProcessed.fbx"}, {URSA_IMPORT_EMBEDDED_FBX, "EmbeddedProcessed.fbx"}})
	{
		const VirtualPath modelPath{projectScheme, fixture.second};
		Done(inputs.WriteAtomic(modelPath, ImportFile(fixture.first)));
		Done(database->Refresh());
		Done(Ursa::Editor::ModelImporter::SetSettings(*database, modelPath, {}));
		const auto modelMetadata = Take(inputs.Read(Take(VirtualPath::Parse(modelPath.ToString() + ".meta"))));
		Ursa::Editor::ModelImportSettings expectedSettings;
		expectedSettings.scaleFactor = 2;
		const auto baseline = Take(Ursa::Editor::ModelImporter::BuildCandidate(*database, modelPath, expectedSettings));
		const auto modelImport = Take(service.Import(*database, modelPath));
		Expect(!modelImport.cacheHit && Take(service.Import(*database, modelPath)).cacheHit,
		       "Managed model processing did not reuse unchanged inputs");
		const auto modelStatus = Take(database->CurrentImport(modelPath));
		const auto modelBytes = Take(inputs.Read(Take(database->Resolve(modelImport.guid))));
		const auto model = nlohmann::json::from_cbor(modelBytes.begin(), modelBytes.end());
		Expect(model["version"] == 2 && modelStatus.settings["effective"]["scaleFactor"] == 2 &&
		           modelStatus.settings["settings"]["scaleFactor"] == 1 &&
		           Take(inputs.Read(Take(VirtualPath::Parse(modelPath.ToString() + ".meta")))) == modelMetadata,
		       "Model preprocessing changed persistent settings or failed to record the effective copy");
		const auto nodeNamed = [&](const char* name)
		{
			return std::find_if(model["nodes"].begin(), model["nodes"].end(),
			                    [&](const auto& node) { return node["name"] == name; });
		};
		const auto editedRoot = nodeNamed("Processed model");
		Expect(editedRoot != model["nodes"].end() && editedRoot->at("localTransform")["position"][0] == 3 &&
		           nodeNamed("Added model node") != model["nodes"].end() &&
		           nodeNamed("Deleted model node") == model["nodes"].end(),
		       "Model postprocessing lost hierarchy edits");
		Expect(model["animationClipLocalFileIds"].size() == baseline.animationOrder.size() &&
		           model["skeletonLocalFileIds"].size() == baseline.skeletons.size(),
		       "Model postprocessing discarded skeletons or animations");
		auto modelResources = Take(ResourceSession::Open(inputs, [&](ResourceId guid, UInt64 local)
		                                                 { return database->Resolve(guid, local); }));
		for (const auto meshId : baseline.meshOrder)
		{
			const auto modified = Take(modelResources->LoadMesh(modelImport.guid, meshId));
			Expect(modified->Snapshot()->data.positions[0].z >=
			               baseline.meshes.at(meshId)->Snapshot()->data.positions[0].z + 0.99f &&
			           modified->Snapshot()->data.skinInfluences.size() ==
			               baseline.meshes.at(meshId)->Snapshot()->data.skinInfluences.size(),
			       "Postprocessed Mesh edits or skin weights were lost");
			const auto slots = model["meshMaterialBindings"].at(std::to_string(meshId));
			Expect(slots.size() == 3 && slots[1] == 0 && slots[0] == slots[2],
			       "Model material slot order or empty slot was lost");
			Expect(Take(modelResources->LoadMaterial(modelImport.guid, slots[0].get<UInt64>()))
			               ->Parameters()
			               .baseColor.green == 1,
			       "Model postprocessor material replacement did not reach the runtime artifact");
		}
	}
	const VirtualPath animatedPath{projectScheme, "AnimatedProcessed.fbx"};
	const auto animatedGuid = Take(database->Metadata(animatedPath)).guid;
	const auto successfulModel = Take(database->ResolveImportedArtifact(animatedGuid));
	for (const String invalid : {"pre-error", "post-error", "destroy-root", "bad-mesh", "remove-joints"})
	{
		Done(inputs.WriteAtomic({projectScheme, "ModelConfig.txt"}, Vector<UInt8>(invalid.begin(), invalid.end())));
		const auto rejected = service.Import(*database, animatedPath);
		const String phase = invalid == "pre-error" ? "OnPreprocessModel" : "OnPostprocessModel";
		Expect(!rejected && rejected.error.find("AnimatedProcessed.fbx") != String::npos &&
		           rejected.error.find(phase) != String::npos,
		       "Model failure was accepted or lost its source and phase diagnostic");
		Expect(rejected.error.find("TextureAndModelPostprocessor") != String::npos,
		       ToStdString("Model failure omitted the processing type (" + invalid + "): " + rejected.error).c_str());
		Expect(Take(database->ResolveImportedArtifact(animatedGuid)).contentFingerprint ==
		           successfulModel.contentFingerprint,
		       "Failed model processor replaced the successful candidate set");
	}
	Done(inputs.WriteAtomic({projectScheme, "ModelConfig.txt"},
	                        Vector<UInt8>(validModelMode.begin(), validModelMode.end())));
	Expect(!Take(service.Import(*database, animatedPath, true)).cacheHit,
	       "Model processor was disabled instead of executing its required rule on retry");
	const VirtualPath texturedModel{projectScheme, "Textured.obj"};
	const String obj = "mtllib Textured.mtl\no Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl "
	                   "Color\nf 1/1 2/2 3/3\n";
	const String mtl = "newmtl Color\nKd 1 1 1\nmap_Kd ColorProcessed.png\n";
	Done(inputs.WriteAtomic(texturedModel, Vector<UInt8>(obj.begin(), obj.end())));
	Done(inputs.WriteAtomic({projectScheme, "Textured.mtl"}, Vector<UInt8>(mtl.begin(), mtl.end())));
	Done(inputs.WriteAtomic({projectScheme, "ColorProcessed.png"}, png));
	Done(database->Refresh());
	const auto textured = Take(service.Import(*database, texturedModel));
	Expect(Take(service.Import(*database, texturedModel)).cacheHit,
	       "Textured model failed its unchanged cache control");
	const auto colorGuid = Take(database->Metadata({projectScheme, "ColorProcessed.png"})).guid;
	Expect(Take(database->CurrentImport({projectScheme, "ColorProcessed.png"})).target == "texture-processed-v1",
	       "Model texture dependency bypassed the project postprocessor chain");
	Done(inputs.WriteAtomic(pixelConfig, Vector<UInt8>{'0', '.', '2', '5'}));
	Expect(!Take(service.Import(*database, texturedModel)).cacheHit &&
	           Take(LoadTextureResource(inputs, Take(database->Resolve(colorGuid)))).texture->GetPixels()[0] == 64,
	       "Model did not wait for the newly processed texture artifact");
	Done(inputs.WriteAtomic(pixelConfig, Vector<UInt8>{'0', '.', '2', '5', '0'}));
	Expect(Take(service.Import(*database, texturedModel)).cacheHit,
	       "Unchanged completed texture content invalidated its model consumer");
	Expect(Take(database->CurrentImport(texturedModel)).settings["artifacts"].size() == 1 &&
	           textured.artifacts.size() > 1,
	       "Model texture dependency was not recorded as a completed artifact input");
	Done(host.CloseScriptSession(session));
	Scene runtimeScene;
	ManagedSceneApi runtimeApi(runtimeScene, resources.get());
	const UInt64 runtimeSession = Take(host.CreateScriptSession(false, &runtimeApi));
	Done(host.LoadScriptContent(runtimeSession, gameContent));
	const auto owner = Take(runtimeScene.CreateGameObject("Runtime asset reader"));
	const UInt64 reader = Take(host.CreateBehaviour("Ursa.SessionFixtures.GeneratedFormatReader, Ursa.SessionFixtures",
	                                                Take(runtimeApi.Register(owner))));
	Done(host.DeserializeBehaviour(
	    reader, ToUrsaString(nlohmann::json{
	                {"FormatVersion", 1},
	                {"ScriptType", "test.generated-reader"},
	                {"Version", 1},
	                {"Fields", {{"assetGuid", ToStdString(imported.guid.ToString())}, {"assetLocalId", UInt64(0)}}}}
	                             .dump())));
	Done(host.InvokeBehaviour(reader, ManagedBehaviourEvent::Awake));
	const auto readback = nlohmann::json::parse(ToStdString(Take(host.SerializeBehaviour(reader))));
	Expect(readback["Fields"]["validated"] == 1,
	       "Runtime managed loading did not restore generated resource wrappers and references");
	Done(host.DestroyBehaviour(reader));
	Done(host.CloseScriptSession(runtimeSession));
}
#endif

void TestManagedImportSession(DotNetHost& host, VirtualFileSystem& filesystem, const ManagedHostFiles& files,
                              const VirtualPath& sessionContentPath)
{
	Scene scene;
	ManagedSceneApi sceneApi(scene);
#if defined(URSA_DOTNET_TEST_EDITOR)
	Done(host.LoadEditorApi(Path("ursa:Ursa.Editor.dll")));
#endif
	auto duplicate = Take(DotNetHost::Create());
	Expect(!duplicate->Connect(filesystem, files, [](UInt32, const String&) {}),
	       "Two endpoints occupied the same owner thread");
	const UInt64 mainSession = Take(host.CreateScriptSession(false, &sceneApi));
	std::exception_ptr workerFailure;
	std::thread worker(
	    [&]
	    {
		    try
		    {
			    ManagedHostFiles workerFiles = files;
			    workerFiles.scriptAssemblies.clear();
			    for (int pass = 0; pass < 2; ++pass)
			    {
				    Scene workerScene;
				    ManagedSceneApi workerApi(workerScene);
				    auto endpoint = Take(DotNetHost::Create());
				    Done(endpoint->Connect(filesystem, workerFiles, [](UInt32, const String&) {}, &workerApi));
				    Expect(!endpoint->ValidateScriptSession(mainSession), "Import owner accepted an Editor session");
#if defined(URSA_DOTNET_TEST_EDITOR)
				    const UInt64 editorSession = Take(endpoint->CreateScriptSession(true));
				    {
					    Ursa::Editor::Bindings::ImportInvocation ordinary(
					        editorSession, 11, {"prj", "Tests/Owner.custom"},
					        Ursa::Editor::Bindings::ImportPhase::ScriptedImport, [] { return UInt64(11); });
					    const Bindings::BindingModuleReference ordinaryCapability{
					        Bindings::Import::moduleId, &Bindings::Import::NativeDescriptor(), ordinary.Token()};
					    Expect(!endpoint->BeginImportInvocation(editorSession, 11, ordinary.Identity(),
					                                            ordinaryCapability),
					           "Import invocation borrowed an ordinary Editor session");
				    }
				    Done(endpoint->CloseScriptSession(editorSession));
				    const UInt64 session = Take(endpoint->CreateImportSession());
				    Done(endpoint->LoadScriptContent(session, sessionContentPath));
				    Ursa::Editor::Bindings::ImportInvocation import(session, 11, {"prj", "Tests/Owner.custom"},
				                                                    Ursa::Editor::Bindings::ImportPhase::ScriptedImport,
				                                                    [] { return UInt64(11); });
				    Done(endpoint->BindScriptSession(session, import.CpuApi()));
				    const Bindings::BindingModuleReference capability{
				        Bindings::Import::moduleId, &Bindings::Import::NativeDescriptor(), import.Token()};
				    Expect(!endpoint->BeginImportInvocation(session, 12, import.Identity(), capability),
				           "Managed import entered an obsolete native generation");
				    Done(endpoint->BeginImportInvocation(session, 11, import.Identity(), capability));
				    Expect(!endpoint->PrepareEditorExtensions(session) && !endpoint->ConfigureRenderPipeline(session) &&
				               !endpoint->RenderScene(session, 1.0f) &&
				               !endpoint->InvokeEditorExtension(session, "forbidden", capability) &&
				               !endpoint->InvokeBehaviour(0, ManagedBehaviourEvent::Awake),
				           "Import session accepted GUI, Play or render capabilities");
				    const UInt64 asset = Take(endpoint->CreateImportScriptableObject(
				        session, "Ursa.SessionFixtures.ImportProbeAsset, Ursa.SessionFixtures"));
				    const String fields = Take(endpoint->SerializeImportScriptableObject(session, asset));
				    const auto payload = nlohmann::json::parse(ToStdString(fields));
				    Expect(payload["Fields"]["validations"] == 1 && payload["Fields"]["capabilityRejections"] == 2 &&
				               payload["Fields"]["gameLifecycleCalls"] == 0 &&
				               payload["Fields"]["mesh"]["$ursaReference"]["Resource"]["kind"] == "import",
				           "Import candidate fields or resource references were not serialized");
				    Done(endpoint->DeserializeImportScriptableObject(session, asset, fields));
				    const auto references = Take(endpoint->GetImportReferences(session));
				    Expect(references.size() == 5, "Import reference graph omitted an owned candidate");
				    Expect(import.SceneGraph().RootGameObjects().size() == 1 &&
				               import.SceneGraph().RootGameObjects()[0]->Children().size() == 1,
				           "Import candidate hierarchy escaped its private Scene");
				    Expect(!endpoint->BeginImportInvocation(session, 11, import.Identity(), capability),
				           "Managed import accepted synchronous reentry");
				    Expect(!endpoint->CreateScriptSession(true) && !endpoint->CreateImportSession(),
				           "Import invocation allowed its bindings to be replaced by another session");
				    Expect(!endpoint->EndImportInvocation(session, 12, import.Identity()),
				           "Managed import accepted an obsolete generation");
				    Done(endpoint->EndImportInvocation(session, 11, import.Identity()));
				    Expect(!endpoint->SerializeImportScriptableObject(session, asset),
				           "Completed import retained a ScriptableObject");
				    Expect(!endpoint->EndImportInvocation(session, 11, import.Identity()),
				           "Managed import ended the same invocation twice");
				    // Endpoint teardown must also close an invocation abandoned by an exception.
				    Expect(!endpoint->BeginImportInvocation(session, 11, import.Identity(), capability),
				           "Completed native invocation was reused");
				    Ursa::Editor::Bindings::ImportInvocation abandoned(
				        session, 11, {"prj", "Tests/Owner.custom"}, Ursa::Editor::Bindings::ImportPhase::ScriptedImport,
				        [] { return UInt64(11); });
				    Done(endpoint->BindScriptSession(session, abandoned.CpuApi()));
				    const Bindings::BindingModuleReference abandonedCapability{
				        Bindings::Import::moduleId, &Bindings::Import::NativeDescriptor(), abandoned.Token()};
				    Done(endpoint->BeginImportInvocation(session, 11, abandoned.Identity(), abandonedCapability));
				    const UInt64 replacement = Take(endpoint->CreateImportScriptableObject(
				        session, "Ursa.SessionFixtures.ImportProbeAsset, Ursa.SessionFixtures"));
				    Expect(!endpoint->DeserializeImportScriptableObject(session, replacement, fields),
				           "Another invocation accepted retired candidate references");
#else
				    const UInt64 session = Take(endpoint->CreateScriptSession(false));
#endif
				    Expect(session != mainSession, "Managed session identities collided between owners");
				    Done(endpoint->ValidateScriptSession(session));
				    Done(endpoint->CloseEndpoint());
				    Expect(!endpoint->IsInitialized() && !endpoint->ValidateScriptSession(session),
				           "Closed endpoint retained its script session");
			    }
		    }
		    catch (...)
		    {
			    workerFailure = std::current_exception();
		    }
	    });
	worker.join();
	if (workerFailure)
	{
		std::rethrow_exception(workerFailure);
	}
	Done(host.ValidateScriptSession(mainSession));
	Done(host.CloseScriptSession(mainSession));
}
} // namespace Ursa::Test
