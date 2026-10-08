#include <Serialization/SerializedObject.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Core/StringInterop.hpp>
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
} // namespace

int main()
{
	try
	{
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
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
