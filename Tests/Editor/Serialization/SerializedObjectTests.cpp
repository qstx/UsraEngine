#include <Serialization/SerializedObject.hpp>
#include <Assets/ImportSettingsDraft.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <algorithm>
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

void AtomicAdapters()
{
	using Json = nlohmann::json;
	Json first = {{"Fields", {{"value", 1}}},
	              {"FieldTypes", {{"value", {{"Kind", "SignedInteger"}, {"ElementKind", "Unsupported"}}}}}};
	Json second = {{"Fields", {{"value", 2}}},
	               {"FieldTypes", {{"value", {{"Kind", "SignedInteger"}, {"ElementKind", "Unsupported"}}}}}};
	bool failSecondPublish = false;
	int firstPublishes = 0;
	const auto makeTarget = [&](Json& state, bool secondTarget)
	{
		SerializedTargetAdapter adapter;
		adapter.capture = [&state]() { return Result<Json>::Success(state); };
		adapter.apply = [&state](const Json& candidate)
		{
			state = candidate;
			return Result<Json>::Success(state);
		};
		adapter.prepare = [](const Json& original, const Json& candidate)
		{
			SerializedTargetCheckpoint checkpoint;
			checkpoint.serializedPayload = original;
			checkpoint.backendToken = {{"Original", original}, {"Candidate", candidate}};
			return Result<SerializedTargetCheckpoint>::Success(std::move(checkpoint));
		};
		adapter.validate = [](const SerializedTargetCheckpoint& prepared)
		{
			return prepared.backendToken.at("Candidate").at("Fields").at("value").is_number_integer()
			           ? Result<void>{}
			           : Result<void>::Failure("prepared value must be an integer");
		};
		adapter.publish =
		    [&state, secondTarget, &failSecondPublish, &firstPublishes](const SerializedTargetCheckpoint& prepared)
		{
			state = prepared.backendToken.at("Candidate");
			if (secondTarget && failSecondPublish)
			{
				return Result<Json>::Failure("injected second-target publish failure");
			}
			if (!secondTarget)
			{
				++firstPublishes;
			}
			return Result<Json>::Success(state);
		};
		adapter.restore = [&state](const SerializedTargetCheckpoint& prepared)
		{
			state = prepared.backendToken.at("Original");
			return Result<void>{};
		};
		return Take(SerializedObject::Create(std::move(adapter)));
	};
	auto firstObject = makeTarget(first, false);
	auto secondObject = makeTarget(second, true);
	Done(Take(firstObject->FindProperty("/Fields/value")).SetValue(10));
	Done(Take(secondObject->FindProperty("/Fields/value")).SetValue(20));
	second["Fields"]["value"] = 3;
	Expect(!SerializedObject::ApplyAtomically({firstObject.get(), secondObject.get()}) &&
	           first["Fields"]["value"] == 1 && firstPublishes == 0,
	       "Atomic batch did not reject a conflict before publishing any target");
	Done(firstObject->Update());
	Done(secondObject->Update());
	Done(Take(firstObject->FindProperty("/Fields/value")).SetValue(10));
	Done(Take(secondObject->FindProperty("/Fields/value")).SetValue(20));
	failSecondPublish = true;
	Expect(!SerializedObject::ApplyAtomically({firstObject.get(), secondObject.get()}) &&
	           first["Fields"]["value"] == 1 && second["Fields"]["value"] == 3,
	       "Atomic batch did not restore the published target and the failing target");
	failSecondPublish = false;
	Done(firstObject->Update());
	Done(secondObject->Update());
	Done(Take(firstObject->FindProperty("/Fields/value")).SetValue(10));
	Done(Take(secondObject->FindProperty("/Fields/value")).SetValue(20));
	Done(SerializedObject::ApplyAtomically({firstObject.get(), secondObject.get()}));
	Expect(first["Fields"]["value"] == 10 && second["Fields"]["value"] == 20 && !firstObject->HasModifiedProperties() &&
	           !secondObject->HasModifiedProperties(),
	       "Successful atomic batch did not commit and refresh every target");
}

void TransactionCheckpointRestore()
{
	using Json = nlohmann::json;

	struct ResourceLease
	{
		int identity = 0;
	};

	struct State
	{
		Json payload;
		Json sourceData;
		Json prefabOverrides;
		UInt64 revision = 0;
		bool dirty = false;
		std::shared_ptr<ResourceLease> resource;
	};

	Vector<State> states;
	for (int index = 0; index < 3; ++index)
	{
		states.push_back({{{"Fields", {{"value", index}}},
		                   {"FieldTypes", {{"value", {{"Kind", "SignedInteger"}, {"ElementKind", "Unsupported"}}}}}},
		                  {index, index + 1},
		                  {{"/Root/position", {index, 0, 0}}},
		                  static_cast<UInt64>(7 + index),
		                  index != 1,
		                  std::make_shared<ResourceLease>(ResourceLease{index})});
	}
	Vector<Json> originalSource;
	Vector<Json> originalOverrides;
	Vector<UInt64> originalRevisions;
	Vector<bool> originalDirty;
	Vector<std::shared_ptr<ResourceLease>> originalResources;
	for (const State& state : states)
	{
		originalSource.push_back(state.sourceData);
		originalOverrides.push_back(state.prefabOverrides);
		originalRevisions.push_back(state.revision);
		originalDirty.push_back(state.dirty);
		originalResources.push_back(state.resource);
	}
	bool failSecondValidation = false;
	bool failThirdPublish = false;
	Vector<int> refreshCounts(3, 0);
	Vector<std::unique_ptr<SerializedObject>> objects;
	for (Size index = 0; index < states.size(); ++index)
	{
		State& state = states[index];
		SerializedTargetAdapter adapter;
		adapter.capture = [&state]() { return Result<Json>::Success(state.payload); };
		adapter.apply = [&state](const Json& candidate)
		{
			state.payload = candidate;
			return Result<Json>::Success(candidate);
		};
		adapter.prepare = [&state](const Json& original, const Json& candidate)
		{
			SerializedTargetCheckpoint checkpoint;
			checkpoint.serializedPayload = original;
			checkpoint.sourceData = state.sourceData;
			checkpoint.prefabOverrides = state.prefabOverrides;
			checkpoint.revision = state.revision;
			checkpoint.dirty = state.dirty;
			checkpoint.retainedResources.push_back(std::static_pointer_cast<void>(state.resource));
			checkpoint.backendToken = {{"candidate", candidate}};
			return Result<SerializedTargetCheckpoint>::Success(std::move(checkpoint));
		};
		adapter.validate = [index, &failSecondValidation](const SerializedTargetCheckpoint&)
		{
			return index == 1 && failSecondValidation ? Result<void>::Failure("injected target two validation failure")
			                                          : Result<void>{};
		};
		adapter.publish = [index, &states, &failThirdPublish](const SerializedTargetCheckpoint& checkpoint)
		{
			State& target = states[index];
			target.payload = checkpoint.backendToken.at("candidate");
			target.sourceData = {"changed", index};
			target.prefabOverrides = {{"/Root/position", {99, 99, 99}}};
			++target.revision;
			target.dirty = true;
			target.resource.reset();
			if (index == 2 && failThirdPublish)
			{
				return Result<Json>::Failure("injected target three publish failure");
			}
			return Result<Json>::Success(target.payload);
		};
		adapter.restore = [&state](const SerializedTargetCheckpoint& checkpoint)
		{
			state.payload = checkpoint.serializedPayload;
			state.sourceData = checkpoint.sourceData;
			state.prefabOverrides = checkpoint.prefabOverrides;
			state.revision = checkpoint.revision;
			state.dirty = checkpoint.dirty;
			state.resource = std::static_pointer_cast<ResourceLease>(checkpoint.retainedResources.front());
			return Result<void>{};
		};
		adapter.refresh = [index, &refreshCounts](const Json&) { ++refreshCounts[index]; };
		objects.push_back(Take(SerializedObject::Create(std::move(adapter))));
	}
	Vector<SerializedObject*> targets;
	for (auto& object : objects)
	{
		targets.push_back(object.get());
		Done(Take(object->FindProperty("/Fields/value")).SetValue(50));
	}
	failSecondValidation = true;
	Expect(!SerializedObject::ApplyAtomically(targets) && states[0].payload["Fields"]["value"] == 0 &&
	           states[0].revision == originalRevisions[0] && states[0].sourceData == originalSource[0],
	       "Nth-target validation failure published an earlier target");
	failSecondValidation = false;
	failThirdPublish = true;
	Expect(!SerializedObject::ApplyAtomically(targets), "Injected Nth-target publish failure was not reported");
	for (Size index = 0; index < states.size(); ++index)
	{
		Expect(states[index].payload["Fields"]["value"] == index && states[index].sourceData == originalSource[index] &&
		           states[index].prefabOverrides == originalOverrides[index] &&
		           states[index].revision == originalRevisions[index] && states[index].dirty == originalDirty[index] &&
		           states[index].resource == originalResources[index],
		       "Atomic rollback did not restore source data, resources, overrides, revision and Dirty state");
	}
	failThirdPublish = false;
	Done(SerializedObject::ApplyAtomically(targets));
	const Vector<UInt64> committedRevisions{states[0].revision, states[1].revision, states[2].revision};
	bool activeSceneDirty = false;
	Expect(states[0].dirty && states[1].dirty && states[2].dirty && !activeSceneDirty,
	       "Cross-asset publish changed the active Scene Dirty state or failed to dirty its asset drafts");
	Expect(refreshCounts[0] == 1 && refreshCounts[1] == 1 && refreshCounts[2] == 1,
	       "Successful atomic publish did not refresh each changed target exactly once");
	states[0].dirty = false;
	Done(Take(objects[0]->FindProperty("/Fields/value")).SetValue(50));
	Done(SerializedObject::ApplyAtomically(targets));
	Expect(!states[0].dirty && states[0].revision == committedRevisions[0] && refreshCounts[0] == 1,
	       "A no-change commit marked a draft Dirty or advanced its revision");
}

void AggregateProperties()
{
	using Json = nlohmann::json;
	const Json floating{{"Kind", "FloatingPoint"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.Single"}};
	const Json integer{{"Kind", "SignedInteger"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.Int32"}};
	const Json string{{"Kind", "String"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.String"}};
	const Json assetReference{{"Kind", "AssetReference"}, {"ElementKind", "Unsupported"}};
	const Json missingReference{{"missing", true}};
	Json first = {{"Fields",
	               {{"speed", 1.0},
	                {"collision", 4},
	                {"asset", {{"guid", "ABCDEF00-0000-4000-8000-000000000001"}, {"localFileId", 0}}},
	                {"empty", nullptr},
	                {"missing", missingReference}}},
	              {"FieldTypes",
	               {{"speed", floating},
	                {"collision", integer},
	                {"asset", assetReference},
	                {"empty", assetReference},
	                {"missing", {{"Kind", "GameObjectReference"}, {"ElementKind", "Unsupported"}}}}}};
	Json second = {{"Fields",
	                {{"speed", 2.0},
	                 {"collision", "four"},
	                 {"asset", {{"guid", "abcdef00-0000-4000-8000-000000000001"}}},
	                 {"empty", Json::object()},
	                 {"missing", missingReference}}},
	               {"FieldTypes",
	                {{"speed", floating},
	                 {"collision", string},
	                 {"asset", assetReference},
	                 {"empty", assetReference},
	                 {"missing", {{"Kind", "GameObjectReference"}, {"ElementKind", "Unsupported"}}}}}};
	const auto create = [](Json& state)
	{
		SerializedTargetAdapter adapter;
		adapter.capture = [&state]() { return Result<Json>::Success(state); };
		adapter.apply = [&state](const Json& candidate)
		{
			state = candidate;
			return Result<Json>::Success(state);
		};
		return Take(SerializedObject::Create(std::move(adapter)));
	};
	auto firstObject = create(first);
	auto secondObject = create(second);
	auto group = Take(SerializedObjectGroup::Create({firstObject.get(), secondObject.get()}));
	const auto common = group.CommonPropertyPaths();
	Expect(common.size() == 4 && std::find(common.begin(), common.end(), "/Fields/collision") == common.end(),
	       "Aggregate field intersection included a same-named property with a different type");
	const Json speed = Take(group.DescribeProperty("/Fields/speed"));
	const Json asset = Take(group.DescribeProperty("/Fields/asset"));
	const Json empty = Take(group.DescribeProperty("/Fields/empty"));
	const Json missing = Take(group.DescribeProperty("/Fields/missing"));
	const Json assetCheckpoint = Take(group.TargetValues("/Fields/asset"));
	const auto equivalentAssetChanges = Take(group.CompareTargetValues(
	    "/Fields/asset", assetCheckpoint, Json{{"guid", "abcdef00-0000-4000-8000-000000000001"}, {"localFileId", 0}}));
	Expect(equivalentAssetChanges == Vector<bool>({false, false}),
	       "Equivalent main-asset aliases were treated as changed picker assignments");
	Expect(speed.at("Value") == 1.0 && speed.at("HasMultipleDifferentValues") == true && speed.at("TargetCount") == 2,
	       "Aggregate property did not expose the primary value and mixed state");
	Expect(asset.at("HasMultipleDifferentValues") == false,
	       "Main-asset reference aliases were not compared by canonical identity");
	Expect(empty.at("ValueState") == "Null" && empty.at("HasMultipleDifferentValues") == true,
	       "Null and empty object reference states were collapsed");
	Expect(missing.at("ValueState") == "MissingReference" && missing.at("HasMultipleDifferentValues") == false,
	       "Missing references were not distinguished from null values");
	Expect(!firstObject->HasModifiedProperties() && !secondObject->HasModifiedProperties() &&
	           first["Fields"]["speed"] == 1.0,
	       "Reading aggregate properties mutated a target or registered a pending edit");
}

void GroupPathPatches()
{
	using Json = nlohmann::json;
	const Json positionDescriptor{
	    {"Kind", "Vector3"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.Numerics.Vector3"}};
	const Json referenceDescriptor{
	    {"Kind", "GameObjectReference"}, {"ElementKind", "Unsupported"}, {"TypeName", "Ursa.GameObject"}};
	Scripting::ScriptBehaviourComponent first("Demo.TransformData");
	Scripting::ScriptBehaviourComponent second("Demo.TransformData");
	Json firstPayload{
	    {"FormatVersion", 1},
	    {"ScriptType", "demo.transformdata"},
	    {"Version", 1},
	    {"Fields",
	     {{"position", {1.0, 2.0, 3.0}},
	      {"target", {{"$ursaReference", {{"Kind", "GameObject"}, {"Id", "11111111-1111-4111-8111-111111111111"}}}}}}},
	    {"FieldTypes", {{"position", positionDescriptor}, {"target", referenceDescriptor}}},
	    {"FutureData", {{"preserve", true}}}};
	Json secondPayload = firstPayload;
	secondPayload["Fields"]["position"] = {4.0, 2.0, 8.0};
	secondPayload["Fields"]["target"]["$ursaReference"]["Id"] = "22222222-2222-4222-8222-222222222222";
	first.SetSerializedPayload(ToUrsaString(firstPayload.dump()));
	second.SetSerializedPayload(ToUrsaString(secondPayload.dump()));
	auto firstObject = Take(SerializedObject::Create(first));
	auto secondObject = Take(SerializedObject::Create(second));
	auto group = Take(SerializedObjectGroup::Create({firstObject.get(), secondObject.get()}));
	const auto paths = group.CommonPropertyPaths();
	Expect(std::find(paths.begin(), paths.end(), "/Fields/position/0") != paths.end(),
	       "Vector component paths were not exposed for a common field");
	const Json before = Take(group.DescribeProperty("/Fields/position/0"));
	Expect(before.at("HasMultipleDifferentValues") == true && !firstObject->HasModifiedProperties() &&
	           !secondObject->HasModifiedProperties(),
	       "Reading a vector component changed the targets or hid its mixed value");
	Done(group.SetValue("/Fields/position/0", 10.0));
	Done(group.Apply());
	const Json savedFirst = Json::parse(ToStdString(first.SerializedPayload()));
	const Json savedSecond = Json::parse(ToStdString(second.SerializedPayload()));
	Expect(savedFirst["Fields"]["position"] == Json::array({10.0, 2.0, 3.0}) &&
	           savedSecond["Fields"]["position"] == Json::array({10.0, 2.0, 8.0}),
	       "A component patch replaced unedited vector components from another target");
	Expect(savedFirst["Fields"]["target"] == firstPayload["Fields"]["target"] &&
	           savedSecond["Fields"]["target"] == secondPayload["Fields"]["target"] &&
	           savedFirst["FutureData"] == firstPayload["FutureData"] &&
	           savedSecond["FutureData"] == secondPayload["FutureData"],
	       "A path patch overwrote target-specific references or unknown serialized data");
	const Json originalTargets = Take(group.TargetValues("/Fields/target"));
	const auto primaryReferenceChanges =
	    Take(group.CompareTargetValues("/Fields/target", originalTargets, firstPayload["Fields"]["target"]));
	Expect(primaryReferenceChanges == Vector<bool>({false, true}),
	       "A picker candidate matching the original primary reference was not treated as a per-target no-op");
	Done(
	    group.SetValue("/Fields/target",
	                   {{"$ursaReference", {{"Kind", "GameObject"}, {"Id", "33333333-3333-4333-8333-333333333333"}}}}));
	Done(group.Apply());
	const auto previewedPrimaryChanges =
	    Take(group.CompareTargetValues("/Fields/target", originalTargets, firstPayload["Fields"]["target"]));
	Expect(previewedPrimaryChanges == Vector<bool>({false, true}),
	       "A live preview changed the final candidate's per-target no-op comparison");
	Done(group.RestoreTargetValues("/Fields/target", originalTargets));
	const Json restoredFirst = Json::parse(ToStdString(first.SerializedPayload()));
	const Json restoredSecond = Json::parse(ToStdString(second.SerializedPayload()));
	Expect(restoredFirst["Fields"]["target"] == firstPayload["Fields"]["target"] &&
	           restoredSecond["Fields"]["target"] == secondPayload["Fields"]["target"],
	       "A per-target property checkpoint did not restore mixed reference values independently");
}

void CollectionOperations()
{
	using Json = nlohmann::json;
	const Json numberArray{{"Kind", "Array"}, {"ElementKind", "SignedInteger"}, {"ElementTypeName", "System.Int32"}};
	const Json referenceArray{
	    {"Kind", "List"}, {"ElementKind", "GameObjectReference"}, {"ElementTypeName", "Ursa.GameObject"}};
	Scripting::ScriptBehaviourComponent first("Demo.Collections");
	Scripting::ScriptBehaviourComponent second("Demo.Collections");
	const auto payload = [&](Json numbers, Json references)
	{
		return Json{{"FormatVersion", 1},
		            {"ScriptType", "demo.collections"},
		            {"Version", 1},
		            {"Fields", {{"numbers", std::move(numbers)}, {"references", std::move(references)}}},
		            {"FieldTypes", {{"numbers", numberArray}, {"references", referenceArray}}}};
	};
	const Json retainedReference{
	    {"$ursaReference", {{"Kind", "GameObject"}, {"Id", ToStdString(ResourceId::Generate().ToString())}}}};
	first.SetSerializedPayload(ToUrsaString(payload({10, 20}, nullptr).dump()));
	second.SetSerializedPayload(ToUrsaString(payload({30, 40, 50, 60}, Json::array({retainedReference})).dump()));
	auto firstObject = Take(SerializedObject::Create(first));
	auto secondObject = Take(SerializedObject::Create(second));
	auto group = Take(SerializedObjectGroup::Create({firstObject.get(), secondObject.get()}));
	const Json initialNumbers = Take(group.DescribeProperty("/Fields/numbers"));
	const Json initialReferences = Take(group.DescribeProperty("/Fields/references"));
	const auto paths = group.CommonPropertyPaths();
	Expect(initialNumbers.at("ArraySize") == 2 && initialNumbers.at("HasMultipleDifferentArraySizes") == true &&
	           std::find(paths.begin(), paths.end(), "/Fields/numbers/1") != paths.end() &&
	           std::find(paths.begin(), paths.end(), "/Fields/numbers/2") == paths.end(),
	       "Collection aggregation did not expose only the shared index range");
	const Json unchangedFirst = Json::parse(ToStdString(first.SerializedPayload()));
	const Json unchangedSecond = Json::parse(ToStdString(second.SerializedPayload()));
	Expect(initialReferences.at("HasMultipleDifferentArraySizes") == true &&
	           unchangedFirst["Fields"]["references"].is_null() &&
	           unchangedSecond["Fields"]["references"] == Json::array({retainedReference}),
	       "Reading collection state implicitly converted null and empty collections");
	Done(group.SetValue("/Fields/numbers/1", 99));
	Done(group.Apply());
	Json savedFirst = Json::parse(ToStdString(first.SerializedPayload()));
	Json savedSecond = Json::parse(ToStdString(second.SerializedPayload()));
	Expect(savedFirst["Fields"]["numbers"] == Json::array({10, 99}) &&
	           savedSecond["Fields"]["numbers"] == Json::array({30, 99, 50, 60}),
	       "Editing a common collection index discarded the longer target's tail");
	Done(firstObject->Update());
	Done(secondObject->Update());
	Done(group.ResizeCollection("/Fields/numbers", 3));
	Done(group.Apply());
	savedFirst = Json::parse(ToStdString(first.SerializedPayload()));
	savedSecond = Json::parse(ToStdString(second.SerializedPayload()));
	Expect(savedFirst["Fields"]["numbers"] == Json::array({10, 99, 0}) &&
	           savedSecond["Fields"]["numbers"] == Json::array({30, 99, 50}),
	       "Explicit resize did not preserve each target prefix and apply its element default");
	Done(firstObject->Update());
	Done(secondObject->Update());
	Done(group.MoveCollectionElement("/Fields/numbers", 0, 1));
	const Json invalidReference = AssetReferenceToJson({ResourceId::Generate(), 0});
	const auto referencesBeforeAppend = Take(group.TargetValues("/Fields/references"));
	Expect(!group.AppendReferences("/Fields/references", {retainedReference, invalidReference}) &&
	           Take(group.TargetValues("/Fields/references")) == referencesBeforeAppend,
	       "A mixed-type append partially changed collection targets");
	Done(group.AppendReferences("/Fields/references", {nullptr}));
	Done(group.Apply());
	savedFirst = Json::parse(ToStdString(first.SerializedPayload()));
	savedSecond = Json::parse(ToStdString(second.SerializedPayload()));
	Expect(savedFirst["Fields"]["numbers"] == Json::array({99, 10, 0}) &&
	           savedSecond["Fields"]["numbers"] == Json::array({99, 30, 50}) &&
	           savedFirst["Fields"]["references"] == Json::array({nullptr}) &&
	           savedSecond["Fields"]["references"] == Json::array({retainedReference, nullptr}),
	       "Common-range reorder or explicit reference append produced inconsistent targets");
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
		AtomicAdapters();
		TransactionCheckpointRestore();
		AggregateProperties();
		GroupPathPatches();
		CollectionOperations();
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
			auto camera = Take(destination->AddComponent<CameraComponent>());
			auto script = Take(root->AddComponent<Scripting::ScriptBehaviourComponent>("Demo.Movement"));
			auto referencePayload = payload;
			referencePayload["Fields"]["targetComponent"] = {
			    {"$ursaReference", {{"Kind", "Component"}, {"Id", ToStdString(camera->PersistentId().ToString())}}}};
			referencePayload["FieldTypes"]["targetComponent"] = {
			    {"Kind", "ComponentReference"}, {"ElementKind", "Unsupported"}, {"TypeName", "Ursa.CameraComponent"}};
			script->SetSerializedPayload(ToUrsaString(referencePayload.dump()));
			if (attached)
			{
				Done(scene.AttachGameObject(root));
			}
			auto live = Take(SerializedObject::Create(*script));
			auto target = Take(live->FindProperty("/Fields/target"));
			Done(target.SetValue(
			    {{"$ursaReference", {{"Kind", "GameObject"}, {"Id", ToStdString(destination->Id().ToString())}}}}));
			auto componentTarget = Take(live->FindProperty("/Fields/targetComponent"));
			Done(live->Apply());
			const String beforeMissingTarget = script->SerializedPayload();
			Scene foreignScene;
			auto foreignOwner = Take(foreignScene.CreateGameObject("Foreign component owner"));
			const ComponentId foreignCameraId = Take(foreignScene.AddComponent<CameraComponent>(foreignOwner));
			auto* foreignCamera = foreignScene.GetComponent(foreignCameraId);
			Done(componentTarget.SetValue(
			    {{"$ursaReference",
			      {{"Kind", "Component"}, {"Id", ToStdString(foreignCamera->PersistentId().ToString())}}}}));
			Expect(!live->Apply() && script->SerializedPayload() == beforeMissingTarget,
			       "A component from another editing graph modified the serialized payload");
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
		CameraComponent secondCamera;
		auto firstNative = Take(SerializedObject::Create(camera, registry, nativeContext));
		auto secondNative = Take(SerializedObject::Create(secondCamera, registry, nativeContext));
		Done(Take(firstNative->FindProperty("/Fields/farPlane")).SetValue(300.0));
		Done(Take(secondNative->FindProperty("/Fields/farPlane")).SetValue(400.0));
		Done(SerializedObject::ApplyAtomically({firstNative.get(), secondNative.get()}));
		Expect(camera.farPlane == 300.f && secondCamera.farPlane == 400.f,
		       "Native components did not publish a prepared multi-target edit");
		Done(firstNative->Update());
		Done(secondNative->Update());
		Done(Take(firstNative->FindProperty("/Fields/farPlane")).SetValue(350.0));
		Done(Take(secondNative->FindProperty("/Fields/farPlane")).SetValue(450.0));
		secondCamera.farPlane = 500.f;
		Expect(!SerializedObject::ApplyAtomically({firstNative.get(), secondNative.get()}) &&
		           camera.farPlane == 300.f && secondCamera.farPlane == 500.f,
		       "Native batch conflict changed one of its targets");
		PointLightComponent firstPointLight;
		PointLightComponent secondPointLight;
		auto firstPointProperties = Take(SerializedObject::Create(firstPointLight, registry, nativeContext));
		auto secondPointProperties = Take(SerializedObject::Create(secondPointLight, registry, nativeContext));
		auto pointGroup =
		    Take(SerializedObjectGroup::Create({firstPointProperties.get(), secondPointProperties.get()}, 1));
		Done(pointGroup.SetValue(ToUrsaString("/Fields/intensity"), 4.0));
		Done(pointGroup.SetValue(ToUrsaString("/Fields/range"), -1.0));
		Expect(!pointGroup.Apply() && firstPointLight.Intensity() == 1.f && secondPointLight.Intensity() == 1.f &&
		           firstPointLight.Range() == 10.f && secondPointLight.Range() == 10.f,
		       "Invalid PointLight range partially published a multi-target edit");
		Done(pointGroup.SetValue(ToUrsaString("/Fields/range"), 25.0));
		Done(pointGroup.Apply());
		Expect(firstPointLight.Intensity() == 4.f && secondPointLight.Intensity() == 4.f &&
		           firstPointLight.Range() == 25.f && secondPointLight.Range() == 25.f,
		       "Valid PointLight aggregate edit did not publish to every target");
		CameraComponent constrainedCameraA;
		CameraComponent constrainedCameraB;
		auto constrainedA = Take(SerializedObject::Create(constrainedCameraA, registry, nativeContext));
		auto constrainedB = Take(SerializedObject::Create(constrainedCameraB, registry, nativeContext));
		auto cameraGroup = Take(SerializedObjectGroup::Create({constrainedA.get(), constrainedB.get()}));
		Done(cameraGroup.SetValue(ToUrsaString("/Fields/nearPlane"), 200.0));
		Expect(!cameraGroup.Apply() && constrainedCameraA.nearPlane == 0.1f && constrainedCameraB.nearPlane == 0.1f,
		       "Camera projection range validation allowed a partial or invalid group edit");
		MeshRendererComponent firstRenderer;
		MeshRendererComponent secondRenderer;
		auto firstRendererProperties = Take(SerializedObject::Create(firstRenderer, registry, nativeContext));
		auto secondRendererProperties = Take(SerializedObject::Create(secondRenderer, registry, nativeContext));
		auto rendererGroup =
		    Take(SerializedObjectGroup::Create({firstRendererProperties.get(), secondRendererProperties.get()}));
		Done(rendererGroup.SetValue(ToUrsaString("/Fields/castShadows"), false));
		Done(rendererGroup.Apply());
		Expect(!firstRenderer.CastShadows() && !secondRenderer.CastShadows(),
		       "MeshRenderer aggregate shadow setting did not update all targets");
		Done(rendererGroup.ResizeCollection(ToUrsaString("/Fields/materials"), 2));
		Done(rendererGroup.Apply());
		Expect(firstRenderer.SharedMaterials().size() == 2 && secondRenderer.SharedMaterials().size() == 2 &&
		           !firstRenderer.SharedMaterials()[0] && !secondRenderer.SharedMaterials()[1],
		       "MeshRenderer material slot resize did not create null slots on every target");
		MeshFilterComponent firstFilter;
		MeshFilterComponent secondFilter;
		auto firstFilterProperties = Take(SerializedObject::Create(firstFilter, registry, nativeContext));
		auto secondFilterProperties = Take(SerializedObject::Create(secondFilter, registry, nativeContext));
		auto filterGroup =
		    Take(SerializedObjectGroup::Create({firstFilterProperties.get(), secondFilterProperties.get()}));
		Done(filterGroup.SetValue(ToUrsaString("/Fields/mesh"), nullptr));
		Done(filterGroup.Apply());
		Expect(!firstFilter.SharedMesh() && !secondFilter.SharedMesh(),
		       "MeshFilter group clear should preserve a null binding on every target");
		MeshFilterComponent referencedFilterA;
		MeshFilterComponent referencedFilterB;
		auto referencedMeshA = Take(Mesh::Create());
		auto referencedMeshB = Take(Mesh::Create());
		Done(referencedMeshA->BindAssetIdentity({ResourceId::Generate(), 0}));
		Done(referencedMeshB->BindAssetIdentity({ResourceId::Generate(), 0}));
		Done(referencedFilterA.SetSharedMesh(referencedMeshA));
		Done(referencedFilterB.SetSharedMesh(referencedMeshB));
		auto referencedPropertiesA = Take(SerializedObject::Create(referencedFilterA, registry, nativeContext));
		auto referencedPropertiesB = Take(SerializedObject::Create(referencedFilterB, registry, nativeContext));
		auto referenceGroup =
		    Take(SerializedObjectGroup::Create({referencedPropertiesA.get(), referencedPropertiesB.get()}));
		const auto meshReferenceState = Take(referenceGroup.DescribeProperty(ToUrsaString("/Fields/mesh")));
		const auto referencePaths = referenceGroup.CommonPropertyPaths();
		Expect(meshReferenceState.value("HasMultipleDifferentValues", false) &&
		           std::find(referencePaths.begin(), referencePaths.end(), ToUrsaString("/Fields/mesh")) !=
		               referencePaths.end(),
		       "MeshFilter bindings should be a typed common property with mixed-reference state");
		auto invalidMeshBinding = referencedPropertiesA->Snapshot().at("Fields").at("mesh");
		invalidMeshBinding["asset"]["guid"] = ToStdString(ResourceId::Generate().ToString());
		Done(referenceGroup.SetValue(ToUrsaString("/Fields/mesh"), invalidMeshBinding));
		Expect(!referenceGroup.Apply() && referencedFilterA.SharedMesh() == referencedMeshA &&
		           referencedFilterB.SharedMesh() == referencedMeshB,
		       "Missing MeshFilter asset reference should reject the group without changing either target");
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
