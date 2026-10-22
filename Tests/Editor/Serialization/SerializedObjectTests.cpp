#include <Serialization/SerializedObject.hpp>
#include <Assets/ImportSettingsDraft.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

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

void ImporterAdapter()
{
	VirtualFileSystem files;
	Done(files.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(files.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const auto path = Take(VirtualPath::Parse("prj:Example.settings"));
	Done(files.WriteAtomic(path, {1}));
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	using Json = nlohmann::json;
	const Json defaults{{"quality", 1}, {"tags", Json::array({"first"})}, {"source", nullptr}};
	auto draft = Take(ImportSettingsDraft::Open(*database, path, {"test.Settings", 1, {"settings"}}, defaults));
	const auto metadata = Take(files.Read(Take(VirtualPath::Parse("prj:Example.settings.meta"))));
	const Json schema{{"quality", {{"Kind", "SignedInteger"}, {"ElementKind", "Unsupported"}, {"TypeName", "int"}}},
	                  {"tags", {{"Kind", "Array"}, {"ElementKind", "String"}, {"ElementTypeName", "string"}}},
	                  {"source", {{"Kind", "AssetReference"}, {"ElementKind", "Unsupported"}}}};
	bool alive = true;
	const auto capture = [&]() -> Result<Json>
	{
		return alive ? Result<Json>::Success({{"Fields", draft.Settings()}, {"FieldTypes", schema}})
		             : Result<Json>::Failure("Importer target belongs to a closed callback");
	};
	auto resources = Take(ResourceSession::Open(files));
	SerializedTargetAdapter adapter;
	adapter.capture = capture;
	adapter.apply = [&](const Json& payload) -> Result<Json>
	{
		const auto edited = draft.Edit(payload.at("Fields"));
		return edited ? capture() : Result<Json>::Failure(edited.error);
	};
	adapter.resources = resources.get();
	auto properties = Take(SerializedObject::Create(adapter));
	Done(Take(properties->FindProperty("/Fields/quality")).SetValue(2));
	Done(Take(properties->FindProperty("/Fields/tags")).SetValue(Json::array({"first", "second"})));
	Done(properties->Apply());
	Expect(draft.HasModified() && draft.Settings()["quality"] == 2 && draft.Settings()["tags"].size() == 2 &&
	           Take(files.Read(Take(VirtualPath::Parse("prj:Example.settings.meta")))) == metadata,
	       "Property Apply wrote importer metadata instead of the settings draft");
	Done(draft.Revert());
	Done(properties->Update());
	Expect(Take(Take(properties->FindProperty("/Fields/quality")).Value()) == 1,
	       "Revert did not refresh the unified property snapshot");
	Done(Take(properties->FindProperty("/Fields/quality")).SetValue(3));
	Done(Take(properties->FindProperty("/Fields/source"))
	         .SetValue(Json{{"guid", ToStdString(ResourceId::Generate().ToString())}, {"localFileId", UInt64(0)}}));
	Expect(!properties->Apply() && !draft.HasModified(),
	       "Invalid asset reference partially applied an importer field transaction");
	Done(properties->Update());
	Done(Take(properties->FindProperty("/Fields/quality")).SetValue(4));
	Done(draft.Edit(Json{{"quality", 5}, {"tags", Json::array({"external"})}, {"source", nullptr}}));
	Expect(!properties->Apply() && draft.Settings()["quality"] == 5,
	       "An old property snapshot overwrote a changed importer draft");
	Done(properties->Update());
	adapter.apply = {};
	auto readOnly = Take(SerializedObject::Create(adapter));
	Done(Take(readOnly->FindProperty("/Fields/quality")).SetValue(6));
	Expect(!readOnly->Apply() && draft.Settings()["quality"] == 5, "A read-only source adapter accepted writes");
	alive = false;
	Expect(!properties->Update() && !readOnly->Apply(), "An expired asset target accepted property operations");
}
} // namespace

int main()
{
	try
	{
		ImporterAdapter();
		Scripting::ScriptBehaviourComponent component("Demo.Movement");
		nlohmann::json payload = {
		    {"FormatVersion", 1u},
		    {"ScriptType", "demo.movement"},
		    {"Version", 1u},
		};
		payload["Fields"] = {
		    {"step", 1.0},
		    {"enabled", true},
		    {"position", {1.0, 2.0, 3.0}},
		    {"target", {{"$ursaReference", {{"Kind", "GameObject"}, {"Id", "11111111-1111-4111-8111-111111111111"}}}}}};
		payload["FieldTypes"] = {{"step", {{"Kind", "FloatingPoint"}, {"ElementKind", "Unsupported"}}},
		                         {"enabled", {{"Kind", "Boolean"}, {"ElementKind", "Unsupported"}}},
		                         {"position", {{"Kind", "Vector3"}, {"ElementKind", "Unsupported"}}},
		                         {"target", {{"Kind", "GameObjectReference"}, {"ElementKind", "Unsupported"}}}};
		component.SetSerializedPayload(ToUrsaString(payload.dump()));

		auto serializedObject = Take(SerializedObject::Create(component));
		auto speed = Take(serializedObject->FindProperty("/Fields/step"));
		auto enabled = Take(serializedObject->FindProperty("/Fields/enabled"));
		auto positionX = Take(serializedObject->FindProperty("/Fields/position/0"));
		Expect(Take(speed.Value()).get<double>() == 1.0, "SerializedProperty returned an incorrect initial value");
		Done(speed.SetValue(2.75));
		Done(enabled.SetValue(false));
		Done(positionX.SetValue(4.0));
		Expect(serializedObject->HasModifiedProperties(), "SerializedObject did not track pending modifications");
		Done(serializedObject->Apply());
		auto saved = nlohmann::json::parse(ToStdString(component.SerializedPayload()));
		Expect(saved["Fields"]["step"] == 2.75 && saved["Fields"]["enabled"] == false &&
		           saved["Fields"]["position"][0] == 4.0 && !serializedObject->HasModifiedProperties(),
		       "SerializedObject did not atomically apply valid script field changes");

		const String committedPayload = component.SerializedPayload();
		Done(speed.SetValue(99.0));
		Done(serializedObject->Update());
		Expect(!serializedObject->HasModifiedProperties() && Take(speed.Value()).get<double>() == 2.75 &&
		           component.SerializedPayload() == committedPayload,
		       "Update did not discard pending edits without changing the target");
		Expect(Take(serializedObject->DescribeProperty("/Fields/step")).at("Kind") == "FloatingPoint" &&
		           Take(serializedObject->DescribeProperty("/Fields/position/0")).at("Kind") == "FloatingPoint" &&
		           serializedObject->PropertyPaths().size() == 4,
		       "Serialized property descriptors or field paths differ");
		Done(speed.SetValue(7.0));
		auto external = saved;
		external["Fields"]["enabled"] = true;
		component.SetSerializedPayload(ToUrsaString(external.dump()));
		Expect(!serializedObject->Apply() &&
		           nlohmann::json::parse(ToStdString(component.SerializedPayload())) == external,
		       "An old property snapshot overwrote an external edit");
		Done(serializedObject->Update());
		Expect(Take(enabled.Value()).get<bool>() && !serializedObject->HasModifiedProperties(),
		       "Update did not capture the new target state");
		component.SetSerializedPayload(committedPayload);
		auto invalidObject = Take(SerializedObject::Create(component));
		auto invalidSpeed = Take(invalidObject->FindProperty("/Fields/step"));
		Done(invalidSpeed.SetValue("not a number"));
		auto rejected = invalidObject->Apply();
		Expect(!rejected && component.SerializedPayload() == committedPayload,
		       "Invalid script property value changed the serialized component payload");

		auto invalidReferenceObject = Take(SerializedObject::Create(component));
		auto invalidReference = Take(invalidReferenceObject->FindProperty("/Fields/target"));
		Done(invalidReference.SetValue({{"$ursaReference", {{"Kind", "Component"}, {"Id", "not-a-guid"}}}}));
		rejected = invalidReferenceObject->Apply();
		Expect(!rejected && component.SerializedPayload() == committedPayload,
		       "Invalid script object reference changed the serialized component payload");
		payload["Fields"]["small"] = 12;
		payload["FieldTypes"]["small"] = {
		    {"Kind", "SignedInteger"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.SByte"}};
		payload["Fields"]["mode"] = 1;
		payload["FieldTypes"]["mode"] = {{"Kind", "Enumeration"},
		                                 {"ElementKind", "Unsupported"},
		                                 {"EnumValues", {"First", "Second"}},
		                                 {"EnumNumericValues", {1, 3}},
		                                 {"EnumUnderlyingType", "System.Byte"}};
		payload["Fields"]["bytes"] = {0, 255};
		payload["FieldTypes"]["bytes"] = {
		    {"Kind", "Array"}, {"ElementKind", "UnsignedInteger"}, {"ElementTypeName", "System.Byte"}};
		component.SetSerializedPayload(ToUrsaString(payload.dump()));
		const String beforeRangeChecks = component.SerializedPayload();
		const auto arrayView = Take(SerializedObject::Create(component));
		const auto elementDescriptor = Take(arrayView->DescribeProperty("/Fields/bytes/1"));
		Expect(elementDescriptor.at("Kind") == "UnsignedInteger" && elementDescriptor.at("TypeName") == "System.Byte",
		       "Array element property lost its declared scalar type");
		for (const auto& candidate : Vector<std::pair<String, nlohmann::json>>{
		         {"/Fields/small", 128}, {"/Fields/mode", 256}, {"/Fields/bytes/1", 256}, {"/Fields/bytes/0", nullptr}})
		{
			auto rangeObject = Take(SerializedObject::Create(component));
			auto property = Take(rangeObject->FindProperty(candidate.first));
			Done(property.SetValue(candidate.second));
			Expect(!rangeObject->Apply() && component.SerializedPayload() == beforeRangeChecks,
			       "Numeric range, enum or nested array validation was bypassed");
		}
		auto validRangeObject = Take(SerializedObject::Create(component));
		Done(Take(validRangeObject->FindProperty("/Fields/bytes/1")).SetValue(128));
		Done(Take(validRangeObject->FindProperty("/Fields/small")).SetValue(127u));
		Done(validRangeObject->Apply());
		// Live reference validation applies to both an attached scene and a detached Prefab tree.
		for (bool attached : {false, true})
		{
			Scene scene;
			auto root = GameObject::Create("Reference root");
			auto destination = GameObject::Create("Reference target");
			Done(destination->SetParent(root));
			auto script = Take(root->AddComponent<Scripting::ScriptBehaviourComponent>("Demo.Movement"));
			script->SetSerializedPayload(ToUrsaString(payload.dump()));
			if (attached)
			{
				Done(scene.AttachGameObject(root));
			}
			auto live = Take(SerializedObject::Create(*script));
			auto target = Take(live->FindProperty("/Fields/target"));
			Done(target.SetValue(
			    {{"$ursaReference", {{"Kind", "GameObject"}, {"Id", ToStdString(destination->Id().ToString())}}}}));
			Done(live->Apply());
			const String beforeMissingTarget = script->SerializedPayload();
			Done(target.SetValue({{"$ursaReference",
			                       {{"Kind", "GameObject"}, {"Id", ToStdString(ResourceId::Generate().ToString())}}}}));
			Expect(!live->Apply() && script->SerializedPayload() == beforeMissingTarget,
			       "A missing graph reference modified the editing payload");
		}
		VirtualFileSystem files;
		Done(files.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		auto resources = Take(ResourceSession::Open(files));
		ComponentRegistry registry;
		Done(RegisterBuiltinComponents(registry));
		ComponentLoadContext nativeContext(*resources);
		CameraComponent camera;
		auto nativeProperties = Take(SerializedObject::Create(camera, registry, nativeContext));
		auto nearPlane = Take(nativeProperties->FindProperty("/Fields/nearPlane"));
		Done(nearPlane.SetValue(0.123456789));
		Done(nativeProperties->Apply());
		Expect(Take(nearPlane.Value()).get<Float64>() == static_cast<Float64>(camera.nearPlane),
		       "Applied snapshot did not reflect the native component's canonical float value");
		Done(Take(nativeProperties->FindProperty("/Fields/farPlane")).SetValue(250.0));
		Done(nativeProperties->Apply());
		Expect(camera.farPlane == 250.f && !nativeProperties->HasModifiedProperties(),
		       "A valid second Apply conflicted with the first Apply's native normalization");
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
