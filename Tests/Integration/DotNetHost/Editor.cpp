#include "TestSupport.hpp"

#if defined(URSA_DOTNET_TEST_EDITOR)
#include <Core/Log.hpp>
#include <Scripting/ManagedEditorBridge.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Assets/SceneDraft.hpp>
#include <Assets/AssetImporterInspector.hpp>
#include <Assets/ScriptableAssetInspector.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/AssetDatabase.hpp>
#include <Assets/AssetPreviewExtension.hpp>
#include <Mesh/Mesh.hpp>
#include <Inspector/ReferencePicker.hpp>
#include <Inspector/SceneInspectorPanel.hpp>
#include <Scene/Lighting.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/ComponentEditors.hpp>
#include <Shader/ShaderAsset.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/PakArchive.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

namespace Ursa::Test
{
void TestEditorExtensions(DotNetHost& host, VirtualFileSystem& source, const std::filesystem::path& root,
                          bool builtinInspectorOnly)
{
	using namespace Ursa::Editor;
	VirtualFileSystem editorFiles;
	Done(editorFiles.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(
	    editorFiles.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	// Reuse the package's providers through a VFS copy, adding the writable project overlay first.
	auto archive = Take(PakArchive::Open(root / "package" / "Managed.pak"));
	Done(archive->Mount(editorFiles));
	Done(editorFiles.Mount(userDataScheme, root / "user", MountAccess::ReadWrite));
	auto workspace = Take(ProjectWorkspace::FromFilesystem(root / "EditorProject", editorFiles));
	auto documents = Take(EditorDocuments::Create(*workspace));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	auto document = Take(
	    NewSceneDocument(Path("prj:EditorFixture.ursascene"), ResourceKind::Scene, registry, documents->Session()));
	auto draft = document->Payload<SceneDraft>();
	auto owner = Take(draft->GetScene().CreateGameObject("Editor Probe"));
	const ComponentId scriptId = Take(draft->GetScene().AddComponent<ScriptBehaviourComponent>(
	    owner, "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
	auto* script = static_cast<ScriptBehaviourComponent*>(draft->GetScene().GetComponent(scriptId));
	script->SetSerializedPayload(
	    R"({"FormatVersion":1,"ScriptType":"test.session-probe","Version":1,
            "Fields":{"value":7,"position":[1.0,2.0,3.0],"modes":[1,3],"future":{"preserved":"unknown data"}},
            "FieldTypes":{
                "value":{"Kind":"SignedInteger","ElementKind":"Unsupported","TypeName":"System.Int32"},
                "position":{"Kind":"Vector3","ElementKind":"Unsupported","TypeName":"Ursa.Vector3"},
                "modes":{"Kind":"List","ElementKind":"Enumeration","ElementTypeName":"Ursa.SessionFixtures.ProbeMode",
                    "EnumUnderlyingType":"System.Byte","EnumValues":["First","Second"],"EnumNumericValues":[1,3]}}})");
	const ComponentId cameraId = Take(draft->GetScene().AddComponent<CameraComponent>(owner));
	auto* camera = static_cast<CameraComponent*>(draft->GetScene().GetComponent(cameraId));
	Done(documents->RequestOpen(std::move(document)));
	host.SetFilesystem(workspace->Index().Filesystem());
	Done(host.LoadEditorApi(Path("ursa:Ursa.Editor.dll")));
	if (builtinInspectorOnly)
	{
		const UInt64 builtinDomain = Take(host.CreateScriptSession(true));
		Done(host.LoadScriptContent(builtinDomain, Path("prj:Managed/Game.json")));
		Done(host.ValidateScriptSession(builtinDomain));
		const auto builtinDescriptors = Take(host.PrepareEditorExtensions(builtinDomain));
		Expect(std::any_of(builtinDescriptors.begin(), builtinDescriptors.end(),
		                   [](const auto& item) { return item.id == "inspector:ursa.Camera"; }),
		       "The built-in camera Inspector was not registered without project editor scripts");
		const auto otherOwner = Take(draft->GetScene().CreateGameObject("Second Camera"));
		const auto otherId = Take(draft->GetScene().AddComponent<CameraComponent>(otherOwner));
		auto* otherCamera = static_cast<CameraComponent*>(draft->GetScene().GetComponent(otherId));
		camera->nearPlane = 0.1f;
		camera->farPlane = 100.f;
		otherCamera->nearPlane = 0.8f;
		otherCamera->farPlane = 150.f;
		ComponentLoadContext loadContext(documents->Session());
		ManagedEditorContext context;
		context.target = camera;
		context.componentTargets = {camera, otherCamera};
		context.componentPrimaryIndex = 0;
		context.supportsMultipleObjects = true;
		context.registry = &registry;
		context.selection.object = owner;
		context.selection.scene = &draft->GetScene();
		context.selection.loadContext = &loadContext;
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.DisplaySize = {800, 600};
		io.DeltaTime = 1.f / 60.f;
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		String inspectorId = "inspector:ursa.Camera";
		String managedError;
		ImRect localCopyButtonRect;
		ImRect saveMeshAsButtonRect;
		const UInt64 logListener = Log::Subscribe(
		    [&](const LogRecord& record)
		    {
			    if (record.level == LogLevel::Error || record.level == LogLevel::Critical)
			    {
				    managedError = record.message;
			    }
		    });
		const auto drawBuiltin = [&]()
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowSize({600, 450}, ImGuiCond_Always);
			ImGui::Begin("Builtin multi-object fixture");
			auto result = InvokeManagedEditorExtension(host, builtinDomain, inspectorId, context);
			if (inspectorId == "inspector:ursa.MeshRenderer")
			{
				localCopyButtonRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			}
			else if (inspectorId == "inspector:ursa.MeshFilter")
			{
				saveMeshAsButtonRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			}
			ImGui::End();
			ImGui::Render();
			if (!result)
			{
				std::string message = "Built-in Inspector invocation failed for " + ToStdString(inspectorId) + ": " +
				                      ToStdString(result.error);
				if (!managedError.empty())
				{
					message += ": " + ToStdString(managedError);
				}
				throw std::runtime_error(message);
			}
		};
		drawBuiltin();
		auto* window = ImGui::FindWindowByName("Builtin multi-object fixture");
		Expect(window != nullptr, "Built-in Inspector window was not created");
		const auto editScalarId = [&](ImGuiID field, const char* text)
		{
			ImGui::ActivateItemByID(field);
			ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
			drawBuiltin();
			Expect(ImGui::GetCurrentContext()->TempInputId == field, "Inspector scalar input did not activate");
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			io.AddKeyEvent(ImGuiKey_A, true);
			drawBuiltin();
			io.AddKeyEvent(ImGuiKey_A, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			io.AddInputCharactersUTF8(text);
			drawBuiltin();
			io.AddKeyEvent(ImGuiKey_Enter, true);
			drawBuiltin();
			io.AddKeyEvent(ImGuiKey_Enter, false);
			drawBuiltin();
		};
		const auto selectionSeed = [&]()
		{
			const String key = ManagedInspectorSelectionKey(context);
			return key.empty() ? window->ID : ImHashStr(key.c_str(), 0, window->ID);
		};
		const auto editScalar = [&](const char* label, const char* text)
		{ editScalarId(ImHashStr(label, 0, selectionSeed()), text); };
		editScalar("Field of View", "60");
		Expect(std::abs(camera->verticalFovRadians - 1.04719755f) < 0.001f &&
		           std::abs(otherCamera->verticalFovRadians - 1.04719755f) < 0.001f,
		       "Editing the actual FOV control did not update both cameras");
		Expect(camera->nearPlane == 0.1f && camera->farPlane == 100.f && otherCamera->nearPlane == 0.8f &&
		           otherCamera->farPlane == 150.f,
		       "Editing FOV overwrote a different camera's unedited clipping planes");
		editScalar("Near", "0.1");
		Expect(camera->nearPlane == 0.1f && otherCamera->nearPlane == 0.1f && otherCamera->farPlane == 150.f,
		       "Entering the primary near plane in a mixed field did not update the other camera");
		const auto lightId = Take(draft->GetScene().AddComponent<DirectionalLightComponent>(owner));
		const auto otherLightId = Take(draft->GetScene().AddComponent<DirectionalLightComponent>(otherOwner));
		auto* light = static_cast<DirectionalLightComponent*>(draft->GetScene().GetComponent(lightId));
		auto* otherLight = static_cast<DirectionalLightComponent*>(draft->GetScene().GetComponent(otherLightId));
		Done(light->SetColor({1.f, 0.2f, 0.3f, 1.f}));
		Done(otherLight->SetColor({0.2f, 0.5f, 0.8f, 1.f}));
		Done(light->SetShadowStrength(0.25f));
		Done(otherLight->SetShadowStrength(0.75f));
		Done(otherLight->SetCastsShadows(false));
		context.target = light;
		context.componentTargets = {light, otherLight};
		inspectorId = "inspector:ursa.DirectionalLight";
		drawBuiltin();
		editScalar("Intensity", "2");
		Expect(light->Intensity() == 2.f && otherLight->Intensity() == 2.f,
		       "Editing directional light intensity did not update both lights");
		Expect(light->Color().red == 1.f && otherLight->Color().red == 0.2f && light->CastsShadows() &&
		           !otherLight->CastsShadows() && light->ShadowStrength() == 0.25f &&
		           otherLight->ShadowStrength() == 0.75f,
		       "Editing directional light intensity overwrote unedited color or shadow settings");
		editScalar("Shadow Strength", "0.5");
		Expect(light->ShadowStrength() == 0.5f && otherLight->ShadowStrength() == 0.5f && light->CastsShadows() &&
		           !otherLight->CastsShadows() && light->Color().red == 1.f && otherLight->Color().red == 0.2f,
		       "Editing shadow strength lost unedited light color or shadow enable state");
		const ImGuiID lightColorGroup = ImHashStr("Color", 0, selectionSeed());
		editScalarId(ImHashStr("##X", 0, lightColorGroup), "128");
		Expect(std::abs(light->Color().red - 128.f / 255.f) < 0.001f &&
		           std::abs(otherLight->Color().red - light->Color().red) < 0.0001f &&
		           std::abs(otherLight->Color().green - light->Color().green) < 0.0001f &&
		           std::abs(otherLight->Color().blue - light->Color().blue) < 0.0001f && light->CastsShadows() &&
		           !otherLight->CastsShadows() && light->ShadowStrength() == 0.5f &&
		           otherLight->ShadowStrength() == 0.5f,
		       "Editing a light color did not apply the selected color or changed unrelated shadow settings");
		Done(otherLight->SetColor({0.2f, 0.5f, 0.8f, 1.f}));
		drawBuiltin();
		const ImGuiID colorRedField = ImHashStr("##X", 0, lightColorGroup);
		ImGui::ActivateItemByID(colorRedField);
		ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
		drawBuiltin();
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_A, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_A, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("200");
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Escape, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Escape, false);
		drawBuiltin();
		Expect(std::abs(light->Color().red - 128.f / 255.f) < 0.001f && otherLight->Color().red == 0.2f &&
		           otherLight->Color().green == 0.5f,
		       "Cancelling a mixed color edit changed a light");
		const auto thirdLightOwner = Take(draft->GetScene().CreateGameObject("Third Light"));
		const auto thirdLightId = Take(draft->GetScene().AddComponent<DirectionalLightComponent>(thirdLightOwner));
		auto* thirdLight = static_cast<DirectionalLightComponent*>(draft->GetScene().GetComponent(thirdLightId));
		Done(thirdLight->SetColor({0.9f, 0.7f, 0.3f, 1.f}));
		ImGui::ActivateItemByID(colorRedField);
		ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
		drawBuiltin();
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_A, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_A, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("200");
		drawBuiltin();
		context.componentTargets = {light, thirdLight};
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		drawBuiltin();
		Expect(thirdLight->Color().red == 0.9f && thirdLight->Color().green == 0.7f && thirdLight->Color().blue == 0.3f,
		       "An unfinished color edit was applied to a newly selected light");
		context.componentTargets = {light, otherLight};
		const auto pointId = Take(draft->GetScene().AddComponent<PointLightComponent>(owner));
		const auto otherPointId = Take(draft->GetScene().AddComponent<PointLightComponent>(otherOwner));
		auto* point = static_cast<PointLightComponent*>(draft->GetScene().GetComponent(pointId));
		auto* otherPoint = static_cast<PointLightComponent*>(draft->GetScene().GetComponent(otherPointId));
		Done(point->SetColor({1.f, 0.2f, 0.3f, 1.f}));
		Done(otherPoint->SetColor({0.2f, 0.5f, 0.8f, 1.f}));
		Done(point->SetRange(5.f));
		Done(otherPoint->SetRange(20.f));
		context.target = point;
		context.componentTargets = {point, otherPoint};
		inspectorId = "inspector:ursa.PointLight";
		drawBuiltin();
		editScalar("Intensity", "3");
		Expect(point->Intensity() == 3.f && otherPoint->Intensity() == 3.f && point->Range() == 5.f &&
		           otherPoint->Range() == 20.f && point->Color().red == 1.f && otherPoint->Color().red == 0.2f,
		       "Editing point light intensity lost unedited color or range");
		editScalar("Range", "7");
		Expect(point->Range() == 7.f && otherPoint->Range() == 7.f && point->Intensity() == 3.f &&
		           otherPoint->Intensity() == 3.f && point->Color().red == 1.f && otherPoint->Color().red == 0.2f,
		       "Editing point light range lost unedited intensity or color");
		const ComponentId otherScriptId = Take(draft->GetScene().AddComponent<ScriptBehaviourComponent>(
		    otherOwner, "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
		auto* otherScript = static_cast<ScriptBehaviourComponent*>(draft->GetScene().GetComponent(otherScriptId));
		auto otherPayload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
		otherPayload["Fields"]["position"] = {4.f, 5.f, 6.f};
		otherScript->SetSerializedPayload(ToUrsaString(otherPayload.dump()));
		context.target = script;
		context.componentTargets = {script, otherScript};
		inspectorId = "inspector:test.session-probe";
		auto firstListPayload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
		auto secondListPayload = nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()));
		firstListPayload["Fields"]["modes"] = {1, 3};
		secondListPayload["Fields"]["modes"] = {3, 1};
		script->SetSerializedPayload(ToUrsaString(firstListPayload.dump()));
		otherScript->SetSerializedPayload(ToUrsaString(secondListPayload.dump()));
		const ImGuiID listFoldout = ImHashStr("modes (2)##modes", 0, selectionSeed());
		window->StateStorage.SetInt(listFoldout, 1);
		drawBuiltin();
		const ImGuiID listHandle0 = ImHashStr("::##modes.drag.0", 0, listFoldout);
		const ImGuiID listHandle1 = ImHashStr("::##modes.drag.1", 0, listFoldout);
		const auto findHandle = [&](ImGuiID handle)
		{
			// ImGui's locator draws the item's expanded outline in the foreground list.
			// The first opaque vertices are its upper edge on the standard test font atlas.
			io.AddMousePosEvent(window->Pos.x + 10.f, window->Pos.y + 10.f);
			drawBuiltin();
			ImGui::DebugLocateItem(handle);
			drawBuiltin();
			Expect(ImGui::GetCurrentContext()->DebugLocateId == 0, "Could not locate the collection drag handle");
			const ImDrawList* overlay = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
			ImVec2 minimum{FLT_MAX, FLT_MAX}, maximum{-FLT_MAX, -FLT_MAX};
			int greenVertices = 0;
			for (const auto& vertex : overlay->VtxBuffer)
			{
				if (vertex.col != IM_COL32(0, 255, 0, 255))
				{
					continue;
				}
				minimum.x = std::min(minimum.x, vertex.pos.x);
				minimum.y = std::min(minimum.y, vertex.pos.y);
				maximum.x = std::max(maximum.x, vertex.pos.x);
				maximum.y = std::max(maximum.y, vertex.pos.y);
				if (++greenVertices == 4)
				{
					break;
				}
			}
			Expect(greenVertices == 4, "The collection handle locator did not draw a rectangle");
			return ImVec2{(minimum.x + maximum.x) * 0.5f,
			              (minimum.y + maximum.y) * 0.5f + (maximum.y - minimum.y < 5.f ? 8.f : 0.f)};
		};
		const ImVec2 firstHandle = findHandle(listHandle0);
		const ImVec2 secondHandle = findHandle(listHandle1);
		io.AddMousePosEvent(firstHandle.x, firstHandle.y);
		drawBuiltin();
		Expect(ImGui::GetCurrentContext()->HoveredId == listHandle0, "The first collection handle was not hovered");
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		drawBuiltin();
		io.AddMousePosEvent(firstHandle.x + 12.f, firstHandle.y);
		drawBuiltin();
		io.AddMousePosEvent(secondHandle.x, secondHandle.y);
		drawBuiltin();
		Expect(ImGui::GetCurrentContext()->DragDropActive, "The collection handle did not begin dragging");
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		drawBuiltin();
		drawBuiltin();
		Expect(nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["modes"] ==
		               nlohmann::json::array({3, 1}) &&
		           nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()))["Fields"]["modes"] ==
		               nlohmann::json::array({1, 3}),
		       "Dragging an actual collection handle did not reorder each selected object's own values");
		io.AddMousePosEvent(firstHandle.x, firstHandle.y);
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		drawBuiltin();
		io.AddMousePosEvent(firstHandle.x + 12.f, firstHandle.y);
		drawBuiltin();
		Expect(ImGui::GetCurrentContext()->DragDropActive, "The collection cancel test did not start a drag");
		io.AddKeyEvent(ImGuiKey_Escape, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Escape, false);
		io.AddMousePosEvent(secondHandle.x, secondHandle.y);
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		drawBuiltin();
		Expect(nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["modes"] ==
		               nlohmann::json::array({3, 1}) &&
		           nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()))["Fields"]["modes"] ==
		               nlohmann::json::array({1, 3}),
		       "Cancelling a collection drag changed the selected objects");
		firstListPayload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
		secondListPayload = nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()));
		firstListPayload["Fields"]["modes"] = {1, 3, 1};
		secondListPayload["Fields"]["modes"] = {3, 1, 3, 1};
		script->SetSerializedPayload(ToUrsaString(firstListPayload.dump()));
		otherScript->SetSerializedPayload(ToUrsaString(secondListPayload.dump()));
		const ImGuiID mixedListFoldout = ImHashStr("modes (\xe2\x80\x94)##modes", 0, selectionSeed());
		window->StateStorage.SetInt(mixedListFoldout, 1);
		drawBuiltin();
		const ImVec2 mixedFirst = findHandle(ImHashStr("::##modes.drag.0", 0, mixedListFoldout));
		const ImVec2 mixedLast = findHandle(ImHashStr("::##modes.drag.2", 0, mixedListFoldout));
		io.AddMousePosEvent(mixedFirst.x, mixedFirst.y);
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		drawBuiltin();
		io.AddMousePosEvent(mixedFirst.x + 12.f, mixedFirst.y);
		drawBuiltin();
		io.AddMousePosEvent(mixedLast.x, mixedLast.y);
		drawBuiltin();
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		drawBuiltin();
		drawBuiltin();
		Expect(nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["modes"] ==
		               nlohmann::json::array({3, 1, 1}) &&
		           nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()))["Fields"]["modes"] ==
		               nlohmann::json::array({1, 3, 3, 1}),
		       "Reordering mixed-size collections changed values outside their common range");
		drawBuiltin();
		const ImGuiID propertyGroup = ImHashStr("/Fields/position", 0, selectionSeed());
		const ImGuiID vectorGroup = ImHashStr("position", 0, propertyGroup);
		const int axis = 0;
		const ImGuiID vectorAxis = ImHashData(&axis, sizeof(axis), vectorGroup);
		editScalarId(ImHashStr("", 0, vectorAxis), "9");
		const auto primaryFields = nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"];
		const auto secondaryFields = nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()))["Fields"];
		Expect(primaryFields["position"] == nlohmann::json::array({9.f, 2.f, 3.f}) &&
		           secondaryFields["position"] == nlohmann::json::array({9.f, 5.f, 6.f}),
		       "Editing the X vector control overwrote other objects' unedited Y/Z components");
		const int yAxis = 1;
		const ImGuiID vectorYAxis = ImHashData(&yAxis, sizeof(yAxis), vectorGroup);
		editScalarId(ImHashStr("", 0, vectorYAxis), "2");
		const auto afterEqualEdit = nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()))["Fields"];
		Expect(afterEqualEdit["position"] == nlohmann::json::array({9.f, 2.f, 6.f}),
		       "Editing a mixed axis to the primary value did not update the secondary target");
		auto resetSecondary = nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()));
		resetSecondary["Fields"]["position"][1] = 5.f;
		otherScript->SetSerializedPayload(ToUrsaString(resetSecondary.dump()));
		drawBuiltin();
		const ImGuiID yField = ImHashStr("", 0, vectorYAxis);
		ImGui::ActivateItemByID(yField);
		ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		drawBuiltin();
		Expect(nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()))["Fields"]["position"][1] == 5.f,
		       "Focusing a mixed axis without editing changed another target");
		ImGui::ActivateItemByID(yField);
		ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
		drawBuiltin();
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_A, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_A, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("2");
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Escape, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Escape, false);
		drawBuiltin();
		Expect(nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()))["Fields"]["position"][1] == 5.f,
		       "Cancelling a mixed axis edit changed another target");
		const auto thirdOwner = Take(draft->GetScene().CreateGameObject("Third Vector Owner"));
		const auto thirdScriptId = Take(draft->GetScene().AddComponent<ScriptBehaviourComponent>(
		    thirdOwner, "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
		auto* thirdScript = static_cast<ScriptBehaviourComponent*>(draft->GetScene().GetComponent(thirdScriptId));
		auto thirdPayload = nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()));
		thirdPayload["Fields"]["position"][1] = 8.f;
		thirdScript->SetSerializedPayload(ToUrsaString(thirdPayload.dump()));
		const auto firstListBeforeSwitch =
		    nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["modes"];
		const auto thirdListBeforeSwitch =
		    nlohmann::json::parse(ToStdString(thirdScript->SerializedPayload()))["Fields"]["modes"];
		io.AddMousePosEvent(mixedFirst.x, mixedFirst.y);
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		drawBuiltin();
		io.AddMousePosEvent(mixedFirst.x + 12.f, mixedFirst.y);
		drawBuiltin();
		Expect(ImGui::GetCurrentContext()->DragDropActive,
		       "The collection selection-switch test did not start dragging");
		context.componentTargets = {script, thirdScript};
		const ImGuiID switchedFoldout = ImHashStr("modes (\xe2\x80\x94)##modes", 0, selectionSeed());
		window->StateStorage.SetInt(switchedFoldout, 1);
		drawBuiltin();
		const auto switchedHandle = findHandle(ImHashStr("::##modes.drag.2", 0, switchedFoldout));
		io.AddMousePosEvent(switchedHandle.x, switchedHandle.y);
		drawBuiltin();
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		drawBuiltin();
		drawBuiltin();
		Expect(nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["modes"] ==
		               firstListBeforeSwitch &&
		           nlohmann::json::parse(ToStdString(thirdScript->SerializedPayload()))["Fields"]["modes"] ==
		               thirdListBeforeSwitch,
		       "A collection drag from the old selection changed the new selection");
		context.componentTargets = {script, otherScript};
		ImGui::ActivateItemByID(yField);
		ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
		drawBuiltin();
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_A, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_A, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("2");
		drawBuiltin();
		context.componentTargets = {script, thirdScript};
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		drawBuiltin();
		Expect(nlohmann::json::parse(ToStdString(thirdScript->SerializedPayload()))["Fields"]["position"][1] == 8.f,
		       "An unfinished mixed-axis edit was applied to a newly selected object");
		context.componentTargets = {script, otherScript};
		drawBuiltin();
		auto firstPayload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
		resetSecondary = nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()));
		for (const auto& vector : Vector<std::pair<std::string, int>>{{"probe2", 2}, {"probe4", 4}})
		{
			firstPayload["Fields"][vector.first] = nlohmann::json::array();
			resetSecondary["Fields"][vector.first] = nlohmann::json::array();
			for (int index = 0; index < vector.second; ++index)
			{
				firstPayload["Fields"][vector.first].push_back(index + 1.f);
				resetSecondary["Fields"][vector.first].push_back(index + 5.f);
			}
			firstPayload["FieldTypes"][vector.first] = {{"Kind", vector.second == 2 ? "Vector2" : "Vector4"},
			                                            {"ElementKind", "Unsupported"}};
			resetSecondary["FieldTypes"][vector.first] = firstPayload["FieldTypes"][vector.first];
		}
		script->SetSerializedPayload(ToUrsaString(firstPayload.dump()));
		otherScript->SetSerializedPayload(ToUrsaString(resetSecondary.dump()));
		drawBuiltin();
		for (const auto& vector : Vector<std::pair<const char*, int>>{{"probe2", 2}, {"probe4", 4}})
		{
			const String path = String("/Fields/") + vector.first;
			const ImGuiID property = ImHashStr(path.c_str(), 0, selectionSeed());
			const ImGuiID group = ImHashStr(vector.first, 0, property);
			const int firstAxis = 0;
			editScalarId(ImHashStr("", 0, ImHashData(&firstAxis, sizeof(firstAxis), group)), "7");
			const auto values =
			    nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()))["Fields"][vector.first];
			Expect(values[0] == 7.f && values[1] == 6.f &&
			           (vector.second == 2 || (values[2] == 7.f && values[3] == 8.f)),
			       "Editing a vector component changed unedited components on the other target");
		}
		auto firstTransform = Take(owner->GetLocalTransform());
		auto secondTransform = Take(otherOwner->GetLocalTransform());
		firstTransform.rotation = Quat::FromAxisAngle({0.f, 1.f, 0.f}, 20.f * 0.01745329252f);
		secondTransform.rotation = Quat::FromAxisAngle({0.f, 1.f, 0.f}, 60.f * 0.01745329252f);
		Done(owner->SetLocalTransform(firstTransform));
		Done(otherOwner->SetLocalTransform(secondTransform));
		Done(documents->SaveCurrent());
		auto firstProperties = Take(CreateGameObjectSerializedProperties(owner));
		auto secondProperties = Take(CreateGameObjectSerializedProperties(otherOwner));
		auto transformGroup = Take(SerializedObjectGroup::Create({firstProperties.get(), secondProperties.get()}, 0));
		context.target = nullptr;
		context.componentTargets.clear();
		context.gameObjectTargets = {owner, otherOwner};
		context.gameObjectPrimaryIndex = 0;
		context.document = documents->Current();
		context.serializedProperties = firstProperties.get();
		context.serializedPropertyGroup = &transformGroup;
		int rotationCommits = 0;
		int positionCommits = 0;
		context.commitPropertyGroup = [&](SerializedObjectGroup&, const String& path, const Vector<bool>& changed)
		{
			Expect(changed.size() == 2, "A multi-object property commit lost its target mask");
			if (path == "/Fields/rotation")
			{
				Expect(changed[0] || changed[1], "Euler editing did not report a changed target");
				++rotationCommits;
			}
			else if (path == "/Fields/position/0")
			{
				Expect(changed[0] && !changed[1], "Position editing did not report the changed target accurately");
				++positionCommits;
			}
			else
			{
				throw std::runtime_error("Unexpected multi-object Transform property commit");
			}
			return Result<void>{};
		};
		inspectorId = "inspector:scene:ursa.GameObject";
		drawBuiltin();
		const ImGuiID rotationGroup = ImHashStr("Rotation", 0, selectionSeed());
		const int rotationAxis = 0;
		editScalarId(ImHashStr("", 0, ImHashData(&rotationAxis, sizeof(rotationAxis), rotationGroup)), "30");
		const Quat firstRotation = Take(owner->GetLocalTransform()).rotation;
		const Quat secondRotation = Take(otherOwner->GetLocalTransform()).rotation;
		const auto expectedRotation = [](Float32 yaw, Float32 roll = 0.f, Float32 pitch = 30.f)
		{
			const Float32 halfX = pitch * 0.5f * 0.01745329252f;
			const Float32 halfY = yaw * 0.5f * 0.01745329252f;
			const Float32 halfZ = roll * 0.5f * 0.01745329252f;
			const Float32 sx = std::sin(halfX), cx = std::cos(halfX), sy = std::sin(halfY), cy = std::cos(halfY);
			const Float32 sz = std::sin(halfZ), cz = std::cos(halfZ);
			return Quat{cy * sx * cz + sy * cx * sz, sy * cx * cz - cy * sx * sz, cy * cx * sz - sy * sx * cz,
			            cy * cx * cz + sy * sx * sz};
		};
		const auto sameRotation = [](Quat actual, Quat expected)
		{
			return std::abs(std::abs(actual.x * expected.x + actual.y * expected.y + actual.z * expected.z +
			                         actual.w * expected.w) -
			                1.f) < 0.001f;
		};
		Expect(sameRotation(firstRotation, expectedRotation(20.f)) &&
		           sameRotation(secondRotation, expectedRotation(60.f)),
		       "Editing a Transform rotation axis overwrote another object's unedited Euler axes");
		Expect(rotationCommits > 0 && documents->Current()->Dirty(),
		       "Euler editing skipped the document Dirty or property group commit path");
		const auto rotationsBeforeFailure = Take(transformGroup.TargetValues("/Fields/rotation"));
		Expect(
		    !transformGroup.SetTargetValues("/Fields/rotation", nlohmann::json::array({rotationsBeforeFailure[0]})) &&
		        Take(transformGroup.TargetValues("/Fields/rotation")) == rotationsBeforeFailure,
		    "A mismatched per-target rotation edit changed pending values");
		auto invalidRotations = rotationsBeforeFailure;
		invalidRotations[0] = {0.f, 0.f, 0.f, 1.f};
		invalidRotations[1] = {"invalid", 0.f, 0.f, 1.f};
		Done(transformGroup.SetTargetValues("/Fields/rotation", invalidRotations));
		Expect(!transformGroup.Apply() && sameRotation(Take(owner->GetLocalTransform()).rotation, firstRotation) &&
		           sameRotation(Take(otherOwner->GetLocalTransform()).rotation, secondRotation),
		       "An invalid per-target rotation partially applied to the selection");
		Done(transformGroup.Update());
		firstTransform = Take(owner->GetLocalTransform());
		secondTransform = Take(otherOwner->GetLocalTransform());
		firstTransform.position = {1.f, 2.f, 3.f};
		secondTransform.position = {4.f, 5.f, 6.f};
		Done(owner->SetLocalTransform(firstTransform));
		Done(otherOwner->SetLocalTransform(secondTransform));
		auto refreshedFirst = Take(CreateGameObjectSerializedProperties(owner));
		auto refreshedSecond = Take(CreateGameObjectSerializedProperties(otherOwner));
		auto secondPrimaryGroup = Take(SerializedObjectGroup::Create({refreshedFirst.get(), refreshedSecond.get()}, 1));
		context.gameObjectPrimaryIndex = 1;
		context.selection.object = otherOwner;
		context.serializedProperties = refreshedSecond.get();
		context.serializedPropertyGroup = &secondPrimaryGroup;
		drawBuiltin();
		const ImGuiID positionPath = ImHashStr("/Fields/position", 0, selectionSeed());
		const ImGuiID positionGroup = ImHashStr("Position", 0, positionPath);
		const int xAxis = 0;
		editScalarId(ImHashStr("", 0, ImHashData(&xAxis, sizeof(xAxis), positionGroup)), "4");
		const Vec3 firstPosition = Take(owner->GetLocalTransform()).position;
		const Vec3 secondPosition = Take(otherOwner->GetLocalTransform()).position;
		Expect(firstPosition.x == 4.f && firstPosition.y == 2.f && firstPosition.z == 3.f && secondPosition.x == 4.f &&
		           secondPosition.y == 5.f && secondPosition.z == 6.f && positionCommits > 0,
		       "A non-first primary target was not used for the mixed position edit");
		const ImGuiID secondRotationGroup = ImHashStr("Rotation", 0, selectionSeed());
		const int zAxis = 2;
		editScalarId(ImHashStr("", 0, ImHashData(&zAxis, sizeof(zAxis), secondRotationGroup)), "45");
		Expect(sameRotation(Take(owner->GetLocalTransform()).rotation, expectedRotation(20.f, 45.f)) &&
		           sameRotation(Take(otherOwner->GetLocalTransform()).rotation, expectedRotation(60.f, 45.f)),
		       "Editing the Z rotation with a non-first primary target lost an unedited axis");
		const int rotationY = 1;
		const ImGuiID rotationYField = ImHashStr("", 0, ImHashData(&rotationY, sizeof(rotationY), secondRotationGroup));
		editScalarId(rotationYField, "60");
		Expect(sameRotation(Take(owner->GetLocalTransform()).rotation, expectedRotation(60.f, 45.f)) &&
		           sameRotation(Take(otherOwner->GetLocalTransform()).rotation, expectedRotation(60.f, 45.f)),
		       "Entering the primary Euler angle did not update the mixed axis on the other target");
		firstTransform = Take(owner->GetLocalTransform());
		firstTransform.rotation = expectedRotation(20.f, 45.f);
		Done(owner->SetLocalTransform(firstTransform));
		Done(secondPrimaryGroup.Update());
		drawBuiltin();
		ImGui::ActivateItemByID(rotationYField);
		ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
		drawBuiltin();
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_A, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_A, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("75");
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Escape, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Escape, false);
		drawBuiltin();
		Expect(sameRotation(Take(owner->GetLocalTransform()).rotation, expectedRotation(20.f, 45.f)) &&
		           sameRotation(Take(otherOwner->GetLocalTransform()).rotation, expectedRotation(60.f, 45.f)),
		       "Cancelling a mixed rotation edit changed a target");
		auto thirdTransform = Take(thirdOwner->GetLocalTransform());
		thirdTransform.rotation = expectedRotation(120.f, 45.f);
		Done(thirdOwner->SetLocalTransform(thirdTransform));
		auto refreshedThird = Take(CreateGameObjectSerializedProperties(thirdOwner));
		auto thirdGroup = Take(SerializedObjectGroup::Create({refreshedFirst.get(), refreshedThird.get()}, 1));
		ImGui::ActivateItemByID(rotationYField);
		ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
		drawBuiltin();
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_A, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_A, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("60");
		drawBuiltin();
		context.gameObjectTargets = {owner, thirdOwner};
		context.selection.object = thirdOwner;
		context.serializedProperties = refreshedThird.get();
		context.serializedPropertyGroup = &thirdGroup;
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		drawBuiltin();
		Expect(sameRotation(Take(thirdOwner->GetLocalTransform()).rotation, expectedRotation(120.f, 45.f)),
		       "An unfinished rotation edit was applied to a newly selected object");
		firstTransform = Take(owner->GetLocalTransform());
		secondTransform = Take(otherOwner->GetLocalTransform());
		firstTransform.rotation = expectedRotation(350.f, 20.f, 89.f);
		secondTransform.rotation = expectedRotation(10.f, 40.f, 89.f);
		Done(owner->SetLocalTransform(firstTransform));
		Done(otherOwner->SetLocalTransform(secondTransform));
		Done(secondPrimaryGroup.Update());
		context.gameObjectTargets = {owner, otherOwner};
		context.selection.object = otherOwner;
		context.serializedProperties = refreshedSecond.get();
		context.serializedPropertyGroup = &secondPrimaryGroup;
		drawBuiltin();
		const ImGuiID nearGimbalGroup = ImHashStr("Rotation", 0, selectionSeed());
		editScalarId(ImHashStr("", 0, ImHashData(&rotationAxis, sizeof(rotationAxis), nearGimbalGroup)), "88");
		Expect(sameRotation(Take(owner->GetLocalTransform()).rotation, expectedRotation(350.f, 20.f, 88.f)) &&
		           sameRotation(Take(otherOwner->GetLocalTransform()).rotation, expectedRotation(10.f, 40.f, 88.f)),
		       "Editing near a singular rotation lost the other Euler axes or angle wrap");
		const ImGuiID nearGimbalX = ImHashStr("", 0, ImHashData(&rotationAxis, sizeof(rotationAxis), nearGimbalGroup));
		editScalarId(nearGimbalX, "90");
		Expect(sameRotation(Take(owner->GetLocalTransform()).rotation, expectedRotation(350.f, 20.f, 90.f)) &&
		           sameRotation(Take(otherOwner->GetLocalTransform()).rotation, expectedRotation(10.f, 40.f, 90.f)),
		       "Editing the rotation axis to 90 degrees did not update both targets");
		editScalarId(nearGimbalX, "88");
		Expect(sameRotation(Take(owner->GetLocalTransform()).rotation, expectedRotation(350.f, 20.f, 88.f)) &&
		           sameRotation(Take(otherOwner->GetLocalTransform()).rotation, expectedRotation(10.f, 40.f, 88.f)),
		       "Moving an Euler axis through 90 degrees lost the unedited axis hints");
		auto firstQuaternionPayload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
		auto secondQuaternionPayload = nlohmann::json::parse(ToStdString(otherScript->SerializedPayload()));
		const Quat firstScriptRotation = Quat::FromAxisAngle({0.f, 1.f, 0.f}, 20.f * 0.01745329252f);
		const Quat secondScriptRotation = Quat::FromAxisAngle({0.f, 1.f, 0.f}, 60.f * 0.01745329252f);
		firstQuaternionPayload["Fields"]["probeQuat"] = {firstScriptRotation.x, firstScriptRotation.y,
		                                                 firstScriptRotation.z, firstScriptRotation.w};
		secondQuaternionPayload["Fields"]["probeQuat"] = {secondScriptRotation.x, secondScriptRotation.y,
		                                                  secondScriptRotation.z, secondScriptRotation.w};
		firstQuaternionPayload["FieldTypes"]["probeQuat"] = {{"Kind", "Quaternion"}, {"ElementKind", "Unsupported"}};
		secondQuaternionPayload["FieldTypes"]["probeQuat"] = firstQuaternionPayload["FieldTypes"]["probeQuat"];
		script->SetSerializedPayload(ToUrsaString(firstQuaternionPayload.dump()));
		otherScript->SetSerializedPayload(ToUrsaString(secondQuaternionPayload.dump()));
		context.target = script;
		context.componentTargets = {script, otherScript};
		context.gameObjectTargets.clear();
		context.selection.object = owner;
		context.serializedProperties = nullptr;
		context.serializedPropertyGroup = nullptr;
		context.commitPropertyGroup = {};
		inspectorId = "inspector:test.session-probe";
		drawBuiltin();
		const ImGuiID scriptRotationGroup = ImHashStr("probeQuat", 0, selectionSeed());
		editScalarId(ImHashStr("", 0, ImHashData(&rotationAxis, sizeof(rotationAxis), scriptRotationGroup)), "30");
		const auto readScriptRotation = [](ScriptBehaviourComponent* component)
		{
			const auto value =
			    nlohmann::json::parse(ToStdString(component->SerializedPayload()))["Fields"]["probeQuat"];
			return Quat{value[0].get<Float32>(), value[1].get<Float32>(), value[2].get<Float32>(),
			            value[3].get<Float32>()};
		};
		Expect(sameRotation(readScriptRotation(script), expectedRotation(20.f)) &&
		           sameRotation(readScriptRotation(otherScript), expectedRotation(60.f)),
		       "Default Quaternion Inspector lost unedited axes on another script target");
		Done(owner->SetName("Header A"));
		Done(otherOwner->SetName("Header B"));
		Done(owner->SetStatic(true));
		Done(otherOwner->SetStatic(false));
		auto headerFirst = Take(CreateGameObjectSerializedProperties(owner));
		auto headerSecond = Take(CreateGameObjectSerializedProperties(otherOwner));
		auto headerGroup = Take(SerializedObjectGroup::Create({headerFirst.get(), headerSecond.get()}, 0));
		context.target = nullptr;
		context.componentTargets.clear();
		context.componentPrimaryIndex = 0;
		context.gameObjectTargets = {owner, otherOwner};
		context.gameObjectPrimaryIndex = 0;
		context.selection.object = owner;
		context.serializedProperties = headerFirst.get();
		context.serializedPropertyGroup = &headerGroup;
		context.projectTags = {"Untagged"};
		int headerNameCommits = 0;
		context.commitPropertyGroup = [&](SerializedObjectGroup&, const String& path, const Vector<bool>&)
		{
			if (path == "/Fields/name")
			{
				++headerNameCommits;
			}
			return Result<void>{};
		};
		inspectorId = "inspector-header:scene:ursa.GameObject";
		drawBuiltin();
		const ImGuiID headerName = ImHashStr("##GameObjectName", 0, selectionSeed());
		ImGui::ActivateItemByID(headerName);
		drawBuiltin();
		Expect(ImGui::GetCurrentContext()->ActiveId == headerName, "The GameObject name header did not activate");
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_A, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_A, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("Cancelled Header");
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Escape, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Escape, false);
		drawBuiltin();
		Expect(Take(owner->Name()) == "Header A" && Take(otherOwner->Name()) == "Header B" && headerNameCommits == 0,
		       "Cancelling a mixed GameObject header name edited another selected object");
		const auto typeHeaderName = [&](const char* text)
		{
			ImGui::ActivateItemByID(headerName);
			drawBuiltin();
			Expect(ImGui::GetCurrentContext()->ActiveId == headerName, "The GameObject name header did not reactivate");
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			io.AddKeyEvent(ImGuiKey_A, true);
			drawBuiltin();
			io.AddKeyEvent(ImGuiKey_A, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			io.AddInputCharactersUTF8(text);
			drawBuiltin();
		};
		typeHeaderName("Header A");
		io.AddKeyEvent(ImGuiKey_Enter, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		drawBuiltin();
		Expect(Take(owner->Name()) == "Header A" && Take(otherOwner->Name()) == "Header A" && headerNameCommits == 1 &&
		           Take(owner->IsStatic()) && !Take(otherOwner->IsStatic()),
		       "Explicitly entering the primary header name did not update only the mixed name");
		typeHeaderName("Committed Header");
		io.AddKeyEvent(ImGuiKey_Enter, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		drawBuiltin();
		Expect(Take(owner->Name()) == "Committed Header" && Take(otherOwner->Name()) == "Committed Header" &&
		           headerNameCommits == 2 && Take(owner->IsStatic()) && !Take(otherOwner->IsStatic()),
		       "Confirming a GameObject header name did not update the selection while preserving other fields");
		Done(owner->SetName("Header A"));
		Done(otherOwner->SetName("Header B"));
		Done(thirdOwner->SetName("Header C"));
		Done(headerGroup.Update());
		drawBuiltin();
		typeHeaderName("Pending Header");
		auto thirdHeader = Take(CreateGameObjectSerializedProperties(thirdOwner));
		auto switchedHeaderGroup = Take(SerializedObjectGroup::Create({headerFirst.get(), thirdHeader.get()}, 0));
		context.gameObjectTargets = {owner, thirdOwner};
		context.serializedPropertyGroup = &switchedHeaderGroup;
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		drawBuiltin();
		Expect(Take(owner->Name()) == "Header A" && Take(otherOwner->Name()) == "Header B" &&
		           Take(thirdOwner->Name()) == "Header C" && headerNameCommits == 2,
		       "An unfinished GameObject header edit was applied to a new selection");
		context.target = script;
		context.componentTargets = {script};
		context.componentPrimaryIndex = 0;
		context.supportsMultipleObjects = false;
		context.gameObjectTargets.clear();
		context.serializedProperties = nullptr;
		context.serializedPropertyGroup = nullptr;
		inspectorId = "inspector-component-header:test.session-probe";
		context.componentHeaderExpanded.reset();
		drawBuiltin();
		Expect(context.componentHeaderExpanded.has_value(),
		       "The managed Behaviour Inspector component-header callback did not report its expansion state");
		inspectorId = "inspector-component-context-menu:test.session-probe";
		context.componentMenuState = ComponentMenuShowRemove;
		context.componentMenuCommand = static_cast<UInt32>(ComponentMenuCommand::None);
		drawBuiltin();
		const ImGuiID removeComponentItem = window->GetID("Remove Component");
		ImGui::ActivateItemByID(removeComponentItem);
		drawBuiltin();
		Expect(context.componentMenuCommand == static_cast<UInt32>(ComponentMenuCommand::None),
		       "The managed component menu invoked Remove when the host disabled it");
		context.componentMenuState |= ComponentMenuCanRemove;
		context.componentMenuCommand = static_cast<UInt32>(ComponentMenuCommand::None);
		drawBuiltin();
		ImGui::ActivateItemByID(removeComponentItem);
		drawBuiltin();
		Expect(context.componentMenuCommand == static_cast<UInt32>(ComponentMenuCommand::Remove),
		       "The managed component menu did not return the enabled Remove command to its host");
		const ComponentId rendererId = Take(draft->GetScene().AddComponent<MeshRendererComponent>(owner));
		auto* renderer = static_cast<MeshRendererComponent*>(draft->GetScene().GetComponent(rendererId));
		auto sourceMaterial = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		Done(sourceMaterial->SetBaseColor({0.2f, 0.4f, 0.6f, 1.f}));
		auto materialShader = std::make_shared<Shader::ShaderAsset>();
		materialShader->id = ResourceId::Generate();
		Shader::PropertyDeclaration smoothness;
		smoothness.identifier = "_Smoothness";
		smoothness.displayName = "Smoothness";
		smoothness.type = Shader::PropertyType::Range;
		smoothness.rangeMinimum = 0.f;
		smoothness.rangeMaximum = 1.f;
		smoothness.defaultFloat = 0.25f;
		materialShader->properties.push_back(smoothness);
		Done(sourceMaterial->SetShaderAsset(materialShader));
		Done(sourceMaterial->BindAssetIdentity({ResourceId::Generate(), 0}));
		Done(renderer->SetSharedMaterials({sourceMaterial}));
		auto rendererProperties = Take(SerializedObject::Create(*renderer, registry, loadContext));
		context.target = renderer;
		context.componentTargets = {renderer};
		context.componentPrimaryIndex = 0;
		context.supportsMultipleObjects = false;
		context.serializedProperties = rendererProperties.get();
		context.serializedPropertyGroup = nullptr;
		inspectorId = "inspector:ursa.MeshRenderer";
		drawBuiltin();
		Expect(localCopyButtonRect.GetWidth() > 0.f && localCopyButtonRect.GetHeight() > 0.f,
		       "The Make Local Copy button was not drawn for an asset material");
		const ImVec2 localCopyButtonCenter{(localCopyButtonRect.Min.x + localCopyButtonRect.Max.x) * 0.5f,
		                                   (localCopyButtonRect.Min.y + localCopyButtonRect.Max.y) * 0.5f};
		io.AddMousePosEvent(localCopyButtonCenter.x, localCopyButtonCenter.y);
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		drawBuiltin();
		const auto localCopy = renderer->SharedMaterials().front();
		Expect(localCopy && localCopy != sourceMaterial && !localCopy->AssetIdentity().guid &&
		           localCopy->LocalIdentity() != sourceMaterial->LocalIdentity() &&
		           localCopy->Parameters().baseColor.red == sourceMaterial->Parameters().baseColor.red &&
		           localCopy->Parameters().baseColor.green == sourceMaterial->Parameters().baseColor.green &&
		           localCopy->Parameters().baseColor.blue == sourceMaterial->Parameters().baseColor.blue,
		       "Clicking Make Local Copy did not bind a distinct local material with copied values");
		const ImGuiID smoothnessField = window->GetID("Smoothness##_Smoothness");
		ImGui::ActivateItemByID(smoothnessField);
		ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
		drawBuiltin();
		Expect(ImGui::GetCurrentContext()->TempInputId == smoothnessField,
		       "The local material Smoothness property did not activate as a scalar input");
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_A, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_A, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("0.8");
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		drawBuiltin();
		const auto localSmoothness = localCopy->PropertyValue("_Smoothness", *materialShader);
		const auto sourceSmoothness = sourceMaterial->PropertyValue("_Smoothness", *materialShader);
		Expect(localSmoothness && sourceSmoothness && localSmoothness.value.floatValue == 0.8f &&
		           sourceSmoothness.value.floatValue == 0.25f,
		       "Editing a local material property changed the asset source or missed the local instance");
		auto meshObject = Take(draft->GetScene().CreateGameObject("Mesh Asset Source"));
		MeshData meshData;
		meshData.positions = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
		meshData.indices = {0, 1, 2};
		auto editableMesh = Take(Mesh::Create(std::move(meshData)));
		Done(editableMesh->BindAssetIdentity({ResourceId::Generate(), 0}));
		auto* filter = Take(meshObject->AddComponent<MeshFilterComponent>());
		Done(filter->SetSharedMesh(editableMesh));
		const MeshPtr sourceMesh = filter->SharedMesh();
		auto meshProperties = Take(SerializedObject::Create(*filter, registry, loadContext));
		context.document = documents->Current();
		context.documents = documents.get();
		context.workspace = workspace.get();
		context.target = filter;
		context.componentTargets = {filter};
		context.serializedProperties = meshProperties.get();
		context.selection.object = meshObject;
		context.selection.roots = draft->GetScene().RootGameObjects();
		inspectorId = "inspector:ursa.MeshFilter";
		drawBuiltin();
		const ImGuiID meshAssetPathField = ImHashStr("Asset path", 0, window->ID);
		ImGui::ActivateItemByID(meshAssetPathField);
		drawBuiltin();
		Expect(ImGui::GetCurrentContext()->ActiveId == meshAssetPathField,
		       "The Mesh Inspector asset path field did not activate");
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_A, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_A, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("prj:NewMesh.ursamesh");
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, true);
		drawBuiltin();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		drawBuiltin();
		Expect(saveMeshAsButtonRect.GetWidth() > 0.f && saveMeshAsButtonRect.GetHeight() > 0.f,
		       "The Save Mesh As button was not drawn for a valid Mesh");
		const ImVec2 saveMeshAsButtonCenter{(saveMeshAsButtonRect.Min.x + saveMeshAsButtonRect.Max.x) * 0.5f,
		                                    (saveMeshAsButtonRect.Min.y + saveMeshAsButtonRect.Max.y) * 0.5f};
		io.AddMousePosEvent(saveMeshAsButtonCenter.x, saveMeshAsButtonCenter.y);
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		drawBuiltin();
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		drawBuiltin();
		const VirtualPath savedMeshPath = Path("prj:NewMesh.ursamesh");
		const auto savedMesh = filter->SharedMesh();
		const auto savedMeshMetadata = workspace->Database()->Metadata(savedMeshPath);
		Expect(savedMesh && savedMesh != sourceMesh && savedMesh->AssetIdentity().guid && savedMeshMetadata &&
		           savedMesh->AssetIdentity().guid == savedMeshMetadata.value.guid && documents->Current()->Dirty(),
		       "Clicking Save Mesh As did not save an asset, replace the binding, and mark the Scene dirty");
		Log::Unsubscribe(logListener);
		ImGui::DestroyContext();
		Done(host.CloseScriptSession(builtinDomain));
		return;
	}
	const UInt64 domain = Take(host.CreateScriptSession(true));
	Done(host.LoadScriptContent(domain, Path("prj:Managed/Game.json")));
	Done(host.LoadScriptAssembly(Take(source.Read(Path("ursa:TestScripts/Ursa.EditorFixtures.dll")))));
	Done(host.ValidateScriptSession(domain));
	auto descriptors = Take(host.PrepareEditorExtensions(domain));
	const auto customCamera = std::find_if(descriptors.begin(), descriptors.end(),
	                                       [](const auto& item) { return item.id == "inspector:ursa.Camera"; });
	Expect(customCamera != descriptors.end() && !customCamera->canEditMultipleObjects &&
	           std::any_of(descriptors.begin(), descriptors.end(),
	                       [](const auto& item) { return item.id == "inspector:ursa.DirectionalLight"; }),
	       "Project Camera Inspector did not override the built-in or other built-ins were missing");
	Expect(!host.PrepareEditorExtensions(domain), "The same session registered editor callbacks twice");
	ComponentLoadContext loadContext(documents->Session());
	ManagedEditorContext context;
	context.document = documents->Current();
	context.documents = documents.get();
	context.workspace = workspace.get();
	context.registry = &registry;
	context.selection.object = owner;
	context.selection.roots = draft->GetScene().RootGameObjects();
	context.selection.scene = &draft->GetScene();
	context.selection.loadContext = &loadContext;
	const auto invoke = [&](const String& id) { return InvokeManagedEditorExtension(host, domain, id, context); };
	Done(invoke("menu:Tools/Test/Mark Dirty") ? Result<void>{} : Result<void>::Failure("Managed Dirty menu failed"));
	Expect(documents->Current()->Dirty(), "Managed menu did not mark the editing document Dirty");
	Expect(static_cast<bool>(invoke("menu:Tools/Test/Save")), "Managed Save menu failed");
	Expect(!documents->Current()->Dirty() &&
	           Take(workspace->Index().Filesystem().Exists(Path("prj:EditorFixture.ursascene"))),
	       "Managed Save did not persist the document through VFS");
	Expect(static_cast<bool>(invoke("menu:Tools/Test/Thread Guard")), "Managed background GUI guard failed");
	Expect(!invoke("menu:Tools/Test/Throw"), "Throwing managed menu was not isolated");
	Expect(static_cast<bool>(invoke("menu:Tools/Test/Mark Dirty")),
	       "A throwing menu disabled another editor extension");
	ImGui::CreateContext();
	ImGui::GetIO().DisplaySize = {800, 600};
	ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels = nullptr;
	int width = 0;
	int height = 0;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	const auto draw = [&](const String& id)
	{
		ImGui::NewFrame();
		ImGui::SetNextWindowSize({600, 450}, ImGuiCond_Always);
		ImGui::Begin("Managed editor fixture");
		auto result = invoke(id);
		ImGui::End();
		ImGui::Render();
		return result;
	};
	context.target = script;
	Expect(Take(draw("inspector:test.session-probe")), "Managed Inspector did not report its property Apply");
	auto payload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
	Expect(payload["Fields"]["value"] == 8 && documents->Current()->Dirty(),
	       "Managed Inspector did not use the unified serialized property transaction");
	Expect(payload["Fields"]["position"][0] == 9.0 && payload["Fields"]["modes"][1] == 1,
	       "Unity-style nested and array property edits did not persist");
	auto* pickerOwnerWindow = ImGui::FindWindowByName("Managed editor fixture");
	Expect(pickerOwnerWindow != nullptr, "Managed Inspector owner window disappeared before opening the picker");
	const String pickerScope = ":target:" + script->PersistentId().ToString() + ":object:" + owner->Id().ToString();
	ImGuiID pickerButtonId = ImHashStr((pickerScope + ":Object").c_str(), 0, pickerOwnerWindow->ID);
	pickerButtonId = ImHashStr("...", 0, pickerButtonId);
	ImGui::ActivateItemByID(pickerButtonId);
	const auto stalePropertyDraw = draw("inspector:test.session-probe");
	Expect(static_cast<bool>(stalePropertyDraw) && ImGui::GetCurrentContext()->OpenPopupStack.Size == 1,
	       "The field button did not open the host-owned object picker or the stale wrapper disabled its Inspector");
	auto* pickerWindow = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
	Expect(pickerWindow != nullptr, "Object picker popup did not create its ImGui window");
	Expect(static_cast<bool>(draw("inspector:test.session-probe")),
	       "Object picker remained open across its host callback");
	ImGui::ActivateItemByID(pickerWindow->GetID("Cancel"));
	Expect(static_cast<bool>(draw("inspector:test.session-probe")) &&
	           ImGui::GetCurrentContext()->OpenPopupStack.Size == 0,
	       "Cancel did not dismiss the object picker");
	ImGui::ActivateItemByID(pickerButtonId);
	Expect(static_cast<bool>(draw("inspector:test.session-probe")) &&
	           ImGui::GetCurrentContext()->OpenPopupStack.Size == 1,
	       "The object picker did not reopen for the reload-cancellation check");
	CancelReferencePickerSessions(ImGui::GetCurrentContext());
	Expect(ImGui::GetCurrentContext()->OpenPopupStack.Size == 0 &&
	           static_cast<bool>(draw("inspector:test.session-probe")) &&
	           ImGui::GetCurrentContext()->OpenPopupStack.Size == 0,
	       "Ending a script session did not cancel and retire its object picker before the next callback");
	const auto afterStaleProperty = nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["value"];
	const std::string stalePropertyDiagnostic =
	    "A stale managed property modified the editing document (value=" + afterStaleProperty.dump() + ")";
	Expect(afterStaleProperty == 8, stalePropertyDiagnostic.c_str());
	context.target = camera;
	Expect(Take(draw("inspector:ursa.Camera")) && camera->nearPlane == 0.25f,
	       "Managed Inspector could not edit an exposed native component");
	const auto otherOwner = Take(draft->GetScene().CreateGameObject("Other Camera Owner"));
	const auto otherCameraId = Take(draft->GetScene().AddComponent<CameraComponent>(otherOwner));
	auto* otherCamera = static_cast<CameraComponent*>(draft->GetScene().GetComponent(otherCameraId));
	context.componentTargets = {camera, otherCamera};
	context.componentPrimaryIndex = 0;
	context.supportsMultipleObjects = false;
	Expect(!draw("inspector:ursa.Camera"),
	       "An Inspector without multi-object support accepted a multi-target camera invocation");
	context.componentTargets.clear();
	context.componentPrimaryIndex = 0;
	Done(documents->SaveCurrent());
	auto reopenedScene = Take(
	    OpenSceneDocument(Path("prj:EditorFixture.ursascene"), ResourceKind::Scene, registry, documents->Session()));
	auto reopenedDraft = reopenedScene->Payload<SceneDraft>();
	auto reopenedOwner = Take(reopenedDraft->GetScene().GameObjectById(owner->Id()));
	auto* reopenedScript = reopenedOwner->FindComponent<ScriptBehaviourComponent>();
	Expect(reopenedScript && reopenedScript->PersistentId() == script->PersistentId() &&
	           nlohmann::json::parse(ToStdString(reopenedScript->SerializedPayload()))["Fields"]["value"] == 8 &&
	           nlohmann::json::parse(ToStdString(
	               reopenedScript->SerializedPayload()))["Fields"]["future"]["preserved"] == "unknown data" &&
	           reopenedOwner->FindComponent<CameraComponent>()->nearPlane == 0.25f,
	       "Editor Scene save/reopen lost typed changes, stable IDs or unknown fields");
	auto secondOwner = Take(draft->GetScene().CreateGameObject("Second Editor Probe"));
	const ComponentId secondScriptId = Take(draft->GetScene().AddComponent<ScriptBehaviourComponent>(
	    secondOwner, "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
	auto* secondScript = static_cast<ScriptBehaviourComponent*>(draft->GetScene().GetComponent(secondScriptId));
	secondScript->SetSerializedPayload(
	    R"({"FormatVersion":1,"ScriptType":"test.session-probe","Version":1,
            "Fields":{"value":11,"position":[4.0,5.0,6.0],"modes":[1,3],"future":{"preserved":"second target"}},
            "FieldTypes":{
                "value":{"Kind":"SignedInteger","ElementKind":"Unsupported","TypeName":"System.Int32"},
                "position":{"Kind":"Vector3","ElementKind":"Unsupported","TypeName":"Ursa.Vector3"},
                "modes":{"Kind":"List","ElementKind":"Enumeration","ElementTypeName":"Ursa.SessionFixtures.ProbeMode",
                    "EnumUnderlyingType":"System.Byte","EnumValues":["First","Second"],"EnumNumericValues":[1,3]}}})");
	context.target = secondScript;
	context.componentTargets = {script, secondScript};
	context.componentPrimaryIndex = 1;
	context.supportsMultipleObjects = true;
	Expect(Take(draw("inspector:test.session-probe")),
	       "Declared C# multi-object Inspector did not commit target-local and shared property edits");
	auto firstGroupPayload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
	auto secondGroupPayload = nlohmann::json::parse(ToStdString(secondScript->SerializedPayload()));
	Expect(firstGroupPayload["Fields"]["value"] == 17 && secondGroupPayload["Fields"]["value"] == 23 &&
	           firstGroupPayload["Fields"]["position"] == nlohmann::json({6.5, 2.0, 3.0}) &&
	           secondGroupPayload["Fields"]["position"] == nlohmann::json({6.5, 5.0, 6.0}),
	       "Managed target iteration did not preserve per-object values or the shared vector component patch");
	Expect(Take(draw("inspector:test.session-probe")),
	       "Declared C# multi-object Inspector did not apply its second property patch");
	Expect(nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["value"] == 99 &&
	           nlohmann::json::parse(ToStdString(secondScript->SerializedPayload()))["Fields"]["value"] == 99,
	       "Stale typed target snapshots overwrote a property already committed through SerializedObject");
	Done(documents->SaveCurrent());
	context.componentTargets.clear();
	context.componentPrimaryIndex = 0;
	context.supportsMultipleObjects = false;
	context.target = nullptr;
	Expect(static_cast<bool>(draw("window:Ursa.EditorFixtures.ProbeWindow")),
	       "Managed tool window could not query selection and draw controls");
	Expect(!draw("window:Ursa.EditorFixtures.ThrowWindow"), "Throwing tool window was not isolated");
	Expect(static_cast<bool>(draw("window:Ursa.EditorFixtures.ProbeWindow")),
	       "A throwing tool window disabled an unrelated window");
	const auto textureSource = Path("prj:TextureProbe.png");
	Done(workspace->Index().Filesystem().WriteAtomic(textureSource, {1, 2, 3}));
	Done(workspace->Database()->Refresh());
	auto importer = Take(AssetImporterInspector::Open(*workspace->Database(), textureSource));
	const auto initialMetadata = Take(workspace->Index().Filesystem().Read(Path("prj:TextureProbe.png.meta")));
	context.serializedTarget = importer->Properties();
	context.importerInspector = importer;
	context.assetSelection = importer->Reference();
	context.targetUsesDocument = false;
	UInt32 imports = 0;
	context.requestImport = [&](const VirtualPath& path)
	{
		Expect(path == textureSource, "Managed importer Apply queued a different source");
		++imports;
	};
	const bool importerDrawSucceeded = static_cast<bool>(draw("inspector:importer:ursa.Texture"));
	const bool importerDraftModified = importer->HasModified();
	const bool importerMetadataUnchanged =
	    Take(workspace->Index().Filesystem().Read(Path("prj:TextureProbe.png.meta"))) == initialMetadata;
	const bool importerDocumentClean = !documents->Current()->Dirty();
	const std::string importerDiagnostic =
	    "Managed importer pre-Apply state: draw=" + std::to_string(importerDrawSucceeded) +
	    ", draft=" + std::to_string(importerDraftModified) + ", metadata=" + std::to_string(importerMetadataUnchanged) +
	    ", document=" + std::to_string(importerDocumentClean);
	Expect(importerDrawSucceeded && importerDraftModified && importerMetadataUnchanged && importerDocumentClean,
	       importerDiagnostic.c_str());
	auto* importerWindow = ImGui::FindWindowByName("Managed editor fixture");
	ImGui::ActivateItemByID(ImHashStr("Apply", 0, importerWindow->ID));
	Expect(static_cast<bool>(draw("inspector:importer:ursa.Texture")) && !importer->HasModified() && imports == 1 &&
	           Take(workspace->Database()->Metadata(textureSource)).importSettings.at("maxDimension") == 8 &&
	           !documents->Current()->Dirty(),
	       "Managed AssetImporterEditor Apply did not save its typed draft and request exactly one import");
	{
		const auto savedMetadata = Take(workspace->Index().Filesystem().Read(Path("prj:TextureProbe.png.meta")));
		auto preview = std::make_unique<AssetPreviewExtension>(
		    "inspector:importer:ursa.Texture",
		    [&](UInt64 instance, ManagedPreviewPhase phase, const ViewportRect& rect,
		        ManagedEditorContext& previewContext)
		    {
			    return InvokeManagedEditorPreview(host, domain, "inspector:importer:ursa.Texture", instance, phase,
			                                      rect, previewContext);
		    });
		preview->Bind(context);
		auto query = Take(preview->Invoke(ManagedPreviewPhase::Query));
		Expect(query.hasPreview && query.constantRepaint && query.info.find("enables=1 disables=0") != String::npos,
		       "Preview existence, repaint request or OnEnable was lost");
		ImGui::NewFrame();
		ImGui::Begin("Managed Preview toolbar");
		const auto toolbar = preview->Invoke(ManagedPreviewPhase::Settings);
		ImGui::End();
		ImGui::Render();
		Expect(toolbar && toolbar.value.info.find("settings=1") != String::npos,
		       "Preview toolbar did not run in the common callback scope");
		ImGui::NewFrame();
		ImGui::Begin("Shared asset Inspector instance");
		context.importerControlsDrawn = false;
		const auto sharedInspector = preview->DrawInspector(context);
		ImGui::End();
		ImGui::Render();
		Expect(sharedInspector && context.importerControlsDrawn &&
		           Take(preview->Invoke(ManagedPreviewPhase::Query)).info.find("enables=1 disables=0 settings=1") !=
		               String::npos,
		       "Asset Inspector and Preview did not retain one shared instance");
		Take(preview->Invoke(ManagedPreviewPhase::Draw, {0, 0, 200, 100}));
		const auto image = preview->Image();
		Expect(image && image->GetWidth() == 2 && image->GetHeight() == 2 &&
		           image->GetColorSpace() == TextureColorSpace::Linear && image->GetPixels()[3] == 128 &&
		           image->GetPixels()[11] == 0 && !documents->Current()->Dirty() &&
		           Take(workspace->Index().Filesystem().Read(Path("prj:TextureProbe.png.meta"))) == savedMetadata,
		       "Managed Preview changed pixels, alpha, color space, settings or author Dirty");
		Take(preview->Invoke(ManagedPreviewPhase::Query));
		preview.reset();
		auto replacementPreview = std::make_unique<AssetPreviewExtension>(
		    "inspector:importer:ursa.Texture",
		    [&](UInt64 instance, ManagedPreviewPhase phase, const ViewportRect& rect,
		        ManagedEditorContext& previewContext)
		    {
			    return InvokeManagedEditorPreview(host, domain, "inspector:importer:ursa.Texture", instance, phase,
			                                      rect, previewContext);
		    });
		replacementPreview->Bind(context);
		query = Take(replacementPreview->Invoke(ManagedPreviewPhase::Query));
		Expect(query.info.find("enables=2 disables=1") != String::npos,
		       "Preview target replacement retained the old instance or omitted OnDisable");
	}
	context.serializedTarget.reset();
	context.importerInspector.reset();
	context.assetSelection.reset();
	context.requestImport = {};
	const auto assetSource = Path("prj:EditorSettings.ursaasset");
	Done(workspace->Index().Filesystem().WriteAtomic(assetSource, {}));
	Done(workspace->Database()->Refresh());
	const auto assetGuid = Take(workspace->Database()->Metadata(assetSource)).guid;
	ScriptTypeDescriptor assetType;
	assetType.sourceGuid = Take(ResourceId::Parse("11111111-1111-4111-8111-111111111111"));
	assetType.localFileId = 4;
	assetType.scriptType = "test.reload-dependency";
	assetType.kind = ScriptTypeKind::ScriptableObject;
	assetType.fields.push_back({"value", "SignedInteger", "Unsupported", "System.Int32", "7"});
	assetType.fields.push_back(
	    {"self", "ScriptableObjectReference", "Unsupported", "Ursa.SessionFixtures.ReloadDependency", "null"});
	Done(SaveScriptableAssetSource(workspace->Index().Filesystem(), assetSource,
	                               Take(CreateScriptableAssetWithDefaults(assetGuid, assetType))));
	auto assetInspector = Take(ScriptableAssetInspector::Open(*workspace, assetSource, assetType));
	context.serializedTarget = assetInspector->Properties();
	context.assetSelection = AssetReference{assetGuid, 4};
	context.targetIdentity = "asset:test.reload-dependency";
	context.saveTarget = [&] { return assetInspector->Save(); };
	Expect(
	    static_cast<bool>(draw("inspector:asset:test.reload-dependency")) &&
	        Take(LoadScriptableAssetSource(workspace->Index().Filesystem(), assetSource, assetGuid))
	                .Payload()["Fields"]["value"] == 9 &&
	        !documents->Current()->Dirty(),
	    "Typed ScriptableObject Target did not save through its source transaction or dirtied an unrelated document");
	Expect(static_cast<bool>(draw("inspector:asset:test.reload-dependency")) &&
	           Take(LoadScriptableAssetSource(workspace->Index().Filesystem(), assetSource, assetGuid))
	                   .Payload()["Fields"]["value"] == 11,
	       "Typed source Inspector did not refresh its fields or expire the previous callback target");
	const auto rendererDataPath = Path("prj:UniversalRendererData.ursaasset");
	Done(workspace->Index().Filesystem().WriteAtomic(rendererDataPath, {}));
	Done(workspace->Database()->Refresh());
	const auto rendererDataGuid = Take(workspace->Database()->Metadata(rendererDataPath)).guid;
	ScriptTypeDescriptor rendererDataType;
	rendererDataType.sourceGuid = Take(ResourceId::Parse("11111111-1111-4111-8111-111111111111"));
	rendererDataType.localFileId = 42;
	rendererDataType.scriptType = "ursa.rendering.universal-renderer-data";
	rendererDataType.version = 1;
	rendererDataType.kind = ScriptTypeKind::ScriptableObject;
	rendererDataType.fields.push_back(
	    {"cascadeSplits", "Array", "FloatingPoint", "System.Single[]", "[0.25,0.75]", "System.Single"});
	Done(SaveScriptableAssetSource(workspace->Index().Filesystem(), rendererDataPath,
	                               Take(CreateScriptableAssetWithDefaults(rendererDataGuid, rendererDataType))));
	auto rendererDataInspector = Take(ScriptableAssetInspector::Open(*workspace, rendererDataPath, rendererDataType));
	const auto drawRendererData = [&]
	{
		ImGui::NewFrame();
		ImGui::Begin("Renderer asset ordering");
		auto result = rendererDataInspector->Draw({}, {});
		ImGui::End();
		ImGui::Render();
		return result;
	};
	Take(drawRendererData());
	auto* rendererDataWindow = ImGui::FindWindowByName("Renderer asset ordering");
	Expect(rendererDataWindow != nullptr, "Renderer data Inspector window was not created");
	const String splitPath = "/Fields/cascadeSplits";
	const String secondSplitPath = splitPath + "/1";
	const ImGuiID splitFieldId = ImHashStr(splitPath.c_str(), 0, rendererDataWindow->ID);
	const ImGuiID secondSplitId = ImHashStr(secondSplitPath.c_str(), 0, splitFieldId);
	ImGui::ActivateItemByID(ImHashStr("^", 0, secondSplitId));
	Expect(Take(drawRendererData()) && rendererDataInspector->Dirty(),
	       "Renderer data Inspector did not apply an ordered property change or report Dirty");
	Done(rendererDataInspector->Save());
	const auto savedRendererData =
	    Take(LoadScriptableAssetSource(workspace->Index().Filesystem(), rendererDataPath, rendererDataGuid));
	Expect(savedRendererData.Payload()["Fields"]["cascadeSplits"] == nlohmann::json::array({0.75, 0.25}) &&
	           !rendererDataInspector->Dirty(),
	       "Renderer data Inspector did not save and reopen the reordered feature list");
	context.serializedTarget.reset();
	context.assetSelection.reset();
	context.targetIdentity.clear();
	context.saveTarget = {};
	context.selection = {};
	auto materialDraft = std::make_shared<MaterialDraft>(MaterialType::Unlit);
	auto materialShader = std::make_shared<Shader::ShaderAsset>();
	materialShader->id = ResourceId::Generate();
	Shader::PropertyDeclaration strength;
	strength.identifier = "_Strength";
	strength.type = Shader::PropertyType::Range;
	materialShader->properties.push_back(strength);
	Shader::PropertyDeclaration color;
	color.identifier = "_Color";
	color.type = Shader::PropertyType::Color;
	color.defaultVector = {1, 1, 1, 1};
	materialShader->properties.push_back(color);
	Done(materialDraft->Material().SetShaderAsset(materialShader));
	context.serializedTarget = materialDraft->Properties(documents->Session());
	context.targetMaterial = materialDraft->MaterialPtr();
	context.assetSelection = AssetReference{ResourceId::Generate(), 0};
	context.targetIdentity = "asset:ursa.Material";
	bool materialDirty = false;
	UInt32 materialSaves = 0;
	context.markTargetDirty = [&] { materialDirty = true; };
	context.saveTarget = [&]
	{
		++materialSaves;
		return Result<void>{};
	};
	Expect(Take(draw("inspector:asset:ursa.Material")) && materialDirty && materialSaves == 0 &&
	           Take(materialDraft->Material().PropertyValue("_Strength", *materialShader)).floatValue == 0.75f &&
	           Take(materialDraft->Material().PropertyValue("_Color", *materialShader)).vectorValue[0] == 0.2f &&
	           !documents->Current()->Dirty(),
	       "Typed Material Target did not commit its private candidate through the owning property transaction");
	context.targetMaterial = materialDraft->MaterialPtr();
	ImGui::ActivateItemByID(ImHashStr("Save material target", 0, importerWindow->ID));
	Expect(static_cast<bool>(draw("inspector:asset:ursa.Material")) && materialSaves == 1 &&
	           !documents->Current()->Dirty(),
	       "Typed Material save targeted the active Scene or lost its command");
	Done(materialDraft->Material().SetPropertyValue("_Strength", *materialShader, MaterialPropertyValue::Range(0.25f)));
	context.targetMaterial = materialDraft->MaterialPtr();
	context.serializedTarget = materialDraft->Properties(documents->Session(), false);
	context.canEditTarget = false;
	Expect(!draw("inspector:asset:ursa.Material") &&
	           Take(materialDraft->Material().PropertyValue("_Strength", *materialShader)).floatValue == 0.25f,
	       "Read-only typed Material Target wrote back its private candidate");
	context.canEditTarget = true;
	context.targetMaterial.reset();
	context.assetSelection.reset();
	context.serializedTarget.reset();
	context.targetIdentity.clear();
	context.markTargetDirty = {};
	context.saveTarget = {};
	context.targetUsesDocument = true;
	// Edit a detached Prefab graph through the same custom Inspector, in a fresh context.
	Done(host.CloseScriptSession(domain));
	const UInt64 replacement = Take(host.CreateScriptSession(true));
	Done(host.LoadScriptContent(replacement, Path("prj:Managed/Game.json")));
	Done(host.LoadScriptAssembly(Take(source.Read(Path("ursa:TestScripts/Ursa.EditorFixtures.dll")))));
	Done(host.ValidateScriptSession(replacement));
	auto registrations = Take(host.PrepareEditorExtensions(replacement));
	Expect(registrations.size() == descriptors.size(), "Reload duplicated extension registrations");
	auto prefabDocument = Take(
	    NewSceneDocument(Path("prj:EditorFixture.ursaprefab"), ResourceKind::Prefab, registry, documents->Session()));
	auto prefabDraft = prefabDocument->Payload<SceneDraft>();
	auto prefabRoot = prefabDraft->GetPrefabAsset()->Root();
	auto prefabScript = Take(prefabRoot->AddComponent<ScriptBehaviourComponent>(script->TypeName()));
	Take(prefabRoot->AddComponent<CameraComponent>());
	prefabScript->SetSerializedPayload(script->SerializedPayload());
	Done(documents->RequestOpen(std::move(prefabDocument)));
	context.document = documents->Current();
	context.target = prefabScript;
	context.selection.object = prefabRoot;
	context.selection.roots = {prefabRoot};
	context.selection.scene = nullptr;
	context.selection.loadContext = &loadContext;
	ImGui::NewFrame();
	ImGui::Begin("Detached Prefab fixture");
	auto prefabEdited = InvokeManagedEditorExtension(host, replacement, "inspector:test.session-probe", context);
	ImGui::End();
	ImGui::Render();
	const auto prefabValue = nlohmann::json::parse(ToStdString(prefabScript->SerializedPayload()))["Fields"]["value"];
	const std::string prefabDiagnostic =
	    "Reloaded Inspector detached Prefab state: success=" + std::to_string(static_cast<bool>(prefabEdited)) +
	    ", changed=" + std::to_string(prefabEdited ? prefabEdited.value : false) + ", value=" + prefabValue.dump();
	Expect(prefabEdited && prefabEdited.value && prefabValue == 100, prefabDiagnostic.c_str());
	Expect(documents->Current()->Dirty(), "Prefab property Apply did not mark the current Prefab document Dirty");
	context.target = nullptr;
	ImGui::NewFrame();
	ImGui::Begin("Typed Prefab fixture");
	const auto typedPrefab =
	    InvokeManagedEditorExtension(host, replacement, "inspector:scene:ursa.GameObject", context);
	ImGui::End();
	ImGui::Render();
	Expect(typedPrefab && Take(prefabRoot->Name()) == "Typed Prefab target" &&
	           Take(prefabRoot->GetLocalTransform()).position.x == 2 && documents->Current()->Dirty(),
	       "Typed independent Prefab target did not expose its graph or native components");
	Done(documents->SaveCurrent());
	auto reopenedPrefab = Take(
	    OpenSceneDocument(Path("prj:EditorFixture.ursaprefab"), ResourceKind::Prefab, registry, documents->Session()));
	auto reopenedPrefabRoot = reopenedPrefab->Payload<SceneDraft>()->GetPrefabAsset()->Root();
	auto* reopenedPrefabScript = reopenedPrefabRoot->FindComponent<ScriptBehaviourComponent>();
	Expect(!documents->Current()->Dirty() && reopenedPrefabRoot->Id() == prefabRoot->Id() && reopenedPrefabScript &&
	           reopenedPrefabScript->PersistentId() == prefabScript->PersistentId() &&
	           nlohmann::json::parse(ToStdString(reopenedPrefabScript->SerializedPayload()))["Fields"]["value"] ==
	               102 &&
	           nlohmann::json::parse(ToStdString(
	               reopenedPrefabScript->SerializedPayload()))["Fields"]["future"]["preserved"] == "unknown data",
	       "Editor Prefab save/reopen lost property changes, unknown fields or stable IDs");
	Done(host.CloseScriptSession(replacement));
	ImGui::DestroyContext();
	Expect(!invoke("menu:Tools/Test/Mark Dirty"), "Closed editing context retained a menu delegate");
	host.SetFilesystem(source);
}
} // namespace Ursa::Test
#endif
