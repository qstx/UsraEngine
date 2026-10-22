#include <Assets/ScriptableAssetInspector.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
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
} // namespace

int main()
{
	try
	{
		VirtualFileSystem files;
		auto sources = std::make_shared<MemoryFileProvider>();
		Done(files.MountProvider(projectScheme, sources, MountAccess::ReadWrite));
		Done(files.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const auto path = Take(VirtualPath::Parse("prj:Settings.ursaasset"));
		Done(files.WriteAtomic(path, {}));
		auto workspace = Take(ProjectWorkspace::FromFilesystem({}, files));
		const auto guid = Take(workspace->Database()->Metadata(path)).guid;
		ScriptTypeDescriptor type;
		type.sourceGuid = ResourceId::Generate();
		type.localFileId = 42;
		type.scriptType = "test.PropertySettings";
		type.kind = ScriptTypeKind::ScriptableObject;
		type.fields.push_back({"strength", "FloatingPoint", "Unsupported", "System.Single", "2.5"});
		type.fields.push_back(
		    {"samples", "List", "SignedInteger", "System.Collections.Generic.List`1", "[1,2]", "System.Int32"});
		type.fields.push_back({"target", "ScriptableObjectReference", "Unsupported", "Ursa.ScriptableObject", "null"});
		ScriptFieldTypeDescriptor mode{"mode", "Enumeration", "Unsupported", "Tests.Mode", "7"};
		mode.enumValues = {"Off", "On"};
		mode.enumNumericValuesJson = {"2", "7"};
		mode.enumUnderlyingType = "System.Int32";
		type.fields.push_back(mode);
		auto asset = Take(CreateScriptableAssetWithDefaults(guid, type));
		auto payload = asset.Payload();
		payload["Fields"]["unknown"] = "preserved";
		Done(asset.SetDescription(guid, 42, type.scriptType, 1, payload));
		Done(SaveScriptableAssetSource(files, path, asset));
		const auto original = Take(files.Read(path));
		auto target = Take(ScriptableAssetInspector::Open(*workspace, path, type));
		auto properties = Take(SerializedObject::Create(target->Properties()));
		Expect(Take(properties->DescribeProperty("/Fields/mode"))["EnumNumericValues"] == nlohmann::json::array({2, 7}),
		       "Source asset Inspector dropped enum names or nonsequential values");
		Done(Take(properties->FindProperty("/Fields/strength")).SetValue(4.0));
		Done(Take(properties->FindProperty("/Fields/samples/1")).SetValue(9));
		Done(properties->Apply());
		Expect(target->Dirty() && Take(files.Read(path)) == original, "Property Apply bypassed the source draft");
		auto competing = Take(SerializedObject::Create(target->Properties()));
		Done(Take(properties->FindProperty("/Fields/strength")).SetValue(5.0));
		Done(properties->Apply());
		Done(Take(competing->FindProperty("/Fields/strength")).SetValue(6.0));
		Expect(!competing->Apply(), "An old source snapshot replaced a newer property transaction");
		Done(Take(properties->FindProperty("/Fields/strength")).SetValue(6.0));
		Done(Take(properties->FindProperty("/Fields/target"))
		         .SetValue({{"$ursaReference",
		                     {{"Kind", "ScriptableObject"},
		                      {"Id", ""},
		                      {"Resource",
		                       {{"guid", ToStdString(ResourceId::Generate().ToString())}, {"localFileId", 1u}}}}}}));
		Expect(!properties->Apply(), "An unresolved persistent reference committed part of a source transaction");
		Done(properties->Update());
		Expect(Take(Take(properties->FindProperty("/Fields/strength")).Value()) == 5.0,
		       "Rejected transaction partially changed the draft");
		Done(target->Save());
		const auto saved = Take(LoadScriptableAssetSource(files, path, guid));
		Expect(!target->Dirty() && saved.Payload()["Fields"]["samples"][1] == 9 &&
		           saved.Payload()["Fields"]["unknown"] == "preserved" && saved.SourceGuid() == guid,
		       "Unified source save lost collection data, unknown fields or asset identity");
		const auto imported = Take(VirtualPath::Parse("lib:Artifact.ursaasset"));
		Expect(!ScriptableAssetInspector::Open(*workspace, imported, type),
		       "An imported artifact was opened for source writes");
		Done(Take(properties->FindProperty("/Fields/strength")).SetValue(8.0));
		Done(properties->Apply());
		Done(files.WriteAtomic(path, original));
		Expect(!target->Save() && target->Dirty() && Take(files.Read(path)) == original,
		       "Source save overwrote an external modification or discarded the draft");
		target.reset();
		Expect(!properties->Update(), "Closed source Inspector retained a writable property adapter");

		VirtualFileSystem readOnly;
		Done(readOnly.MountProvider(projectScheme, sources, MountAccess::ReadOnly));
		Done(readOnly.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(),
		                            MountAccess::ReadWrite));
		auto lockedWorkspace = Take(ProjectWorkspace::FromFilesystem({}, readOnly));
		auto locked = Take(ScriptableAssetInspector::Open(*lockedWorkspace, path, type));
		auto lockedProperties = Take(SerializedObject::Create(locked->Properties()));
		Done(Take(lockedProperties->FindProperty("/Fields/strength")).SetValue(10.0));
		Expect(!lockedProperties->Apply() && !locked->Save() && !locked->Dirty(),
		       "Read-only source accepted property or save writes");
		std::cout
		    << "Source asset typed transactions, collections, references, conflicts and read-only leases passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
