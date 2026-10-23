#include <Rendering/Scene/RendererList.hpp>
#include <Rendering/Scene/DirectionalShadowCascades.hpp>
#include <cmath>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/SceneResource.hpp>
#include <Core/Memory/Memory.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace Ursa;
using namespace Ursa::Rendering;

namespace
{
void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

template <typename T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
}

MeshPtr Triangle(Float32 x = 0)
{
	MeshData data;
	data.positions = {{x - 0.5f, -0.5f, 0}, {x + 0.5f, -0.5f, 0}, {x, 0.5f, 0}};
	data.indices = {0, 1, 2};
	data.subMeshes = {{0, 3}};
	auto mesh = Take(Mesh::Create(std::move(data)));
	Done(mesh->RecalculateBounds());
	return mesh;
}

GameObject::Ptr Object(Scene& scene, MeshPtr mesh, Float32 x, Float32 z, UInt32 layer, AlphaMode alpha)
{
	auto object = Take(scene.CreateGameObject("Mesh"));
	LocalTransform transform;
	transform.position = {x, 0, z};
	transform.scale = {2, 1, 0.5f};
	Done(object->SetLocalTransform(transform));
	Done(object->SetLayer(layer));
	Done(Take(object->AddComponent<MeshFilterComponent>())->SetSharedMesh(std::move(mesh)));
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(material->SetAlphaMode(alpha));
	Done(Take(object->AddComponent<MeshRendererComponent>())->SetMaterial(material));
	return object;
}

void TestPersistentCamera()
{
	Scene scene;
	auto root = Take(scene.CreateGameObject("Camera"));
	Done(root->SetLayer(7));
	Expect(!root->SetLayer(32), "Invalid object layer accepted");
	auto camera = Take(root->AddComponent<CameraComponent>());
	camera->rendering.depth = -5;
	camera->rendering.cullingMask = 1u << 7;
	camera->rendering.clearFlags = CameraClearFlags::Depth;
	camera->rendering.viewport = {0.25f, 0.0f, 0.5f, 1.0f};
	camera->rendering.targetWidth = 800;
	camera->rendering.targetHeight = 600;
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	VirtualFileSystem files;
	auto resources = Take(ResourceSession::Open(files));
	ComponentLoadContext context(*resources);
	auto document = Take(EncodeSceneDocument("Camera.ursascene", ResourceId::Generate(), scene, registry));
	auto loaded = Take(DecodeSceneDocument("Camera.ursascene", document, registry, context));
	auto copy = loaded.scene->RootGameObjects()[0];
	Expect(Take(copy->Layer()) == 7 && copy->FindComponent<CameraComponent>()->rendering.cullingMask == (1u << 7) &&
	           copy->FindComponent<CameraComponent>()->rendering.targetWidth == 800,
	       "Scene lost rendering settings or layer");
	auto prefab = Take(CapturePrefabAsset(ResourceId::Generate(), scene, root, registry, context));
	Scene instances;
	context.ClearMappings();
	auto instance = Take(prefab.Instantiate(instances, registry, context));
	Expect(Take(instance->Layer()) == 7 && instance->FindComponent<CameraComponent>()->rendering.depth == -5,
	       "Prefab lost camera or layer settings");
	document["gameObjects"][0].erase("layer");
	document["gameObjects"][0]["components"][0]["data"].erase("rendering");
	context.ClearMappings();
	loaded = Take(DecodeSceneDocument("Old.ursascene", document, registry, context));
	copy = loaded.scene->RootGameObjects()[0];
	Expect(Take(copy->Layer()) == 0 && copy->FindComponent<CameraComponent>()->rendering.cullingMask == 0xffffffffu,
	       "Old scene did not get default rendering settings");
	camera->rendering.viewport.width = 2;
	ArchiveWriter writer;
	Expect(!camera->Serialize(writer), "Invalid camera viewport serialized");
}

void TestSnapshotAndLists()
{
	Scene scene;
	auto mesh = Triangle();
	auto near = Object(scene, mesh, 0, -3, 0, AlphaMode::Opaque);
	auto far = Object(scene, mesh, 0, -6, 1, AlphaMode::Blend);
	auto outside = Object(scene, mesh, 100, -4, 0, AlphaMode::Opaque);
	auto hidden = Object(scene, mesh, 0, -4, 0, AlphaMode::Opaque);
	Done(hidden->SetActive(false));
	auto firstCamera = Take(Take(scene.CreateGameObject("First"))->AddComponent<CameraComponent>());
	auto secondCamera = Take(Take(scene.CreateGameObject("Second"))->AddComponent<CameraComponent>());
	firstCamera->rendering.depth = 2;
	secondCamera->rendering.depth = -1;
	secondCamera->nearPlane = 10;
	secondCamera->orthographic = true;
	secondCamera->orthographicSize = 6;
	auto light = Take(scene.CreateGameObject("Light"));
	auto directional = Take(light->AddComponent<DirectionalLightComponent>());
	Done(directional->SetCastsShadows(false));
	Done(directional->SetShadowStrength(0.4f));
	Done(directional->SetShadowBias(0.02f));
	Done(directional->SetShadowNormalBias(0.2f));
	Done(directional->SetSoftShadows(true));
	RenderScene snapshot;
	Done(snapshot.Capture(scene, 1));
	Expect(snapshot.Objects().size() == 3 && snapshot.Cameras().size() == 2 &&
	           snapshot.Cameras()[0].identity == secondCamera->PersistentId() &&
	           snapshot.Objects()[0].worldBounds.minimum.x == -1 && snapshot.Lighting().directional.size() == 1 &&
	           snapshot.Objects()[0].meshSnapshot == snapshot.Objects()[1].meshSnapshot,
	       "Snapshot camera order, disabled object or scaled bounds incorrect");
	const auto& orthographicCamera = snapshot.Cameras()[0];
	const auto& perspectiveCamera = snapshot.Cameras()[1];
	const auto expectedOrthographic = Mat4::Orthographic(12, 1, 10, secondCamera->farPlane);
	const auto expectedPerspective =
	    Mat4::Perspective(firstCamera->verticalFovRadians, 1, firstCamera->nearPlane, firstCamera->farPlane);
	const auto matrixNear = [](const Mat4& actual, const Mat4& expected)
	{
		for (Size index = 0; index < actual.values.size(); ++index)
		{
			if (std::abs(actual.values[index] - expected.values[index]) > 1e-5f)
			{
				return false;
			}
		}
		return true;
	};
	Expect(orthographicCamera.orthographic && orthographicCamera.nearPlane == 10 &&
	           orthographicCamera.farPlane == secondCamera->farPlane &&
	           matrixNear(orthographicCamera.projection, expectedOrthographic) &&
	           matrixNear(orthographicCamera.viewProjection, expectedOrthographic) &&
	           matrixNear(orthographicCamera.viewProjection * orthographicCamera.inverseViewProjection,
	                      Mat4::Identity()) &&
	           !perspectiveCamera.orthographic && perspectiveCamera.nearPlane == firstCamera->nearPlane &&
	           perspectiveCamera.farPlane == firstCamera->farPlane &&
	           matrixNear(perspectiveCamera.projection, expectedPerspective) &&
	           matrixNear(perspectiveCamera.viewProjection, expectedPerspective) &&
	           matrixNear(perspectiveCamera.viewProjection * perspectiveCamera.inverseViewProjection, Mat4::Identity()),
	       "Camera snapshot lost its stable identity, projection kind or inverse/clip planes");
	const auto lightIdentity = directional->PersistentId();
	const auto& capturedLight = snapshot.Lighting().directional[0];
	Expect(capturedLight.identity == lightIdentity && !capturedLight.castsShadows &&
	           capturedLight.shadowStrength == 0.4f && capturedLight.shadowBias == 0.02f &&
	           capturedLight.shadowNormalBias == 0.2f && capturedLight.softShadows,
	       "Directional light snapshot omitted stable identity or shadow settings");
	Done(directional->SetCastsShadows(true));
	Done(directional->SetShadowStrength(0.8f));
	Expect(snapshot.Lighting().directional[0].identity == lightIdentity &&
	           !snapshot.Lighting().directional[0].castsShadows &&
	           snapshot.Lighting().directional[0].shadowStrength == 0.4f,
	       "Changing a light after capture mutated the immutable lighting snapshot");
	const auto pointNode = Take(scene.CreateGameObject("Visible range light"));
	const auto visiblePoint = Take(pointNode->AddComponent<PointLightComponent>());
	Done(visiblePoint->SetRange(1));
	LocalTransform pointTransform;
	pointTransform.position = {0, 0, -5};
	Done(pointNode->SetLocalTransform(pointTransform));
	const auto edgeNode = Take(scene.CreateGameObject("Range sphere outside frustum"));
	const auto edgePoint = Take(edgeNode->AddComponent<PointLightComponent>());
	Done(edgePoint->SetRange(1));
	pointTransform.position = {6.7f, 0, -10};
	Done(edgeNode->SetLocalTransform(pointTransform));
	const auto visiblePointIdentity = visiblePoint->PersistentId();
	Done(snapshot.Capture(scene, 1));
	CameraData sceneViewCamera;
	sceneViewCamera.identity = ResourceId::Generate();
	sceneViewCamera.settings.type = CameraType::SceneView;
	sceneViewCamera.settings.targetWidth = 640;
	sceneViewCamera.settings.targetHeight = 360;
	RenderScene sceneViewSnapshot;
	Done(sceneViewSnapshot.Capture(scene, 16.0f / 9.0f, CameraType::SceneView, sceneViewCamera));
	Expect(sceneViewSnapshot.Cameras().size() == 1 &&
	           sceneViewSnapshot.Cameras()[0].identity == sceneViewCamera.identity &&
	           sceneViewSnapshot.Cameras()[0].settings.type == CameraType::SceneView,
	       "SceneView capture did not isolate the editor camera from game cameras");
	RenderScene gameViewSnapshot;
	Done(gameViewSnapshot.Capture(scene, 16.0f / 9.0f, CameraType::Game));
	Expect(gameViewSnapshot.Cameras().size() == 2 &&
	           std::all_of(gameViewSnapshot.Cameras().begin(), gameViewSnapshot.Cameras().end(),
	                       [](const CameraData& camera) { return camera.settings.type == CameraType::Game; }),
	       "Game capture included a camera from another viewport type");
	auto material = near->FindComponent<MeshRendererComponent>()->SharedMaterials()[0];
	const auto capturedVersion = snapshot.Objects()[0].materialVersion;
	Done(material->SetBaseColor({0.5f, 0.0f, 0.0f, 1}));
	Expect(snapshot.Objects()[0].materialVersion == capturedVersion && material->Version() != capturedVersion,
	       "Material snapshot version changed after capture");
	RenderSceneContext context;
	auto token = Take(context.Begin(10, 1, snapshot));
	auto firstView = Take(context.Cull(snapshot.Cameras()[0]));
	auto secondView = Take(context.Cull(snapshot.Cameras()[1]));
	Expect(Take(context.VisibleObjects(firstView))->empty() && Take(context.VisibleObjects(secondView))->size() == 2 &&
	           context.Scene() == &snapshot,
	       "Multiple cameras did not share one unchanged snapshot");
	CameraData camera;
	camera.viewProjection = Mat4::Perspective(1.0f, 1, 0.1f, 100);
	auto culled = Take(context.Cull(camera));
	Expect(Take(context.VisibleObjects(culled))->size() == 2, "Frustum culling failed");
	const auto visibleLights = Take(context.VisibleLightsFor(culled));
	Expect(
	    visibleLights.directional.size() == 1 && visibleLights.directional[0].identity == lightIdentity &&
	        visibleLights.point.size() == 1 && visibleLights.point[0].identity == visiblePointIdentity,
	    "Cull did not retain stable visible light identities or used an AABB instead of the point-light range sphere");
	DrawingSettings drawing;
	drawing.sorting.cameraPosition = {};
	auto opaque = Take(context.CreateRendererList(culled, drawing, {0, 2500, 0xffffffffu}));
	Expect(Take(context.Entries(opaque))->size() == 1 &&
	           snapshot.Objects()[Take(context.Entries(opaque))->at(0).objectIndex].objectId == near->Id(),
	       "Opaque filtering failed");
	drawing.sorting.criteria = SortingCriteria::CommonTransparent;
	auto transparent = Take(context.CreateRendererList(culled, drawing, {2501, 5000, 0xffffffffu}));
	Expect(Take(context.Entries(transparent))->size() == 1 &&
	           snapshot.Objects()[Take(context.Entries(transparent))->at(0).objectIndex].objectId == far->Id(),
	       "Transparent filtering failed");
	camera.settings.cullingMask = 1u;
	auto masked = Take(context.Cull(camera));
	Expect(Take(context.VisibleObjects(masked))->size() == 1, "Camera layer mask failed");
	auto replacement = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	drawing.overrideMaterial = replacement;
	auto replaced = Take(context.CreateRendererList(culled, drawing, {}));
	Expect(Take(context.Entries(replaced))->at(0).material == replacement, "Replacement material ignored");
	auto wrong = culled;
	++wrong.context.sessionId;
	Expect(!context.VisibleObjects(wrong), "Cross-session result accepted");
	wrong = culled;
	++wrong.generation;
	Expect(!context.VisibleObjects(wrong), "Stale result generation accepted");
	bool rejectedThread = false;
	std::thread worker([&] { rejectedThread = !context.Validate(token); });
	worker.join();
	Expect(rejectedThread, "Render context accepted a worker thread");
	Done(context.End());
	Expect(!context.Entries(opaque) && !context.VisibleLightsFor(culled), "Closed frame result accepted");
	Take(context.Begin(10, 2, snapshot));
	Expect(!context.VisibleObjects(culled), "Previous frame result accepted");
	Done(context.End());
	// Warm each result slot once, then verify native container storage is retained across frames.
	auto frame = [&]
	{
		Done(snapshot.Capture(scene, 1));
		Take(context.Begin(10, 3, snapshot));
		auto visible = Take(context.Cull(camera));
		Take(context.CreateRendererList(visible, drawing, {}));
		Done(context.End());
	};
	frame();
	const auto before = Memory::Snapshot();
	for (int index = 0; index < 50; ++index)
	{
		frame();
	}
	const auto after = Memory::Snapshot();
	for (Size index = 0; index < before.categoryCount; ++index)
	{
		Expect(before.categories[index].allocationCount == after.categories[index].allocationCount,
		       "Stable culling/list frames reallocated container storage");
	}
	Take(context.Begin(10, 4, snapshot));
	Done(snapshot.Capture(scene, 1));
	Expect(!context.Validate(token), "Changed scene snapshot accepted");
	Expect(!context.End(), "Changed snapshot was not diagnosed during end");
	Take(context.Begin(10, 5, snapshot));
	Done(context.End());
	(void)outside;
}

void TestSortingAndPasses()
{
	Scene scene;
	auto mesh = Triangle();
	auto near = Object(scene, mesh, 0, -3, 0, AlphaMode::Blend);
	auto far = Object(scene, mesh, 0, -9, 0, AlphaMode::Blend);
	auto middle = Object(scene, mesh, 0, -6, 0, AlphaMode::Blend);
	auto shader = std::make_shared<Shader::ShaderAsset>();
	shader->id = ResourceId::Generate();
	shader->name = "RendererListTestShader";
	shader->sourceHash = "renderer-list-shader-v1";
	Shader::ShaderSubShaderAsset subShader;
	subShader.tags = {{"RenderPipeline", "UrsaForward"}, {"Queue", "Transparent+10"}};
	Shader::ShaderPassAsset depth;
	depth.name = "Depth";
	depth.tags = {{"LightMode", "DepthOnly"}};
	Shader::ShaderPassAsset forward;
	forward.name = "Forward";
	forward.tags = {{"LightMode", "CustomForward"}};
	subShader.passes = {depth, forward};
	Shader::ShaderSubShaderAsset customSubShader;
	customSubShader.tags = {{"RenderPipeline", "UrsaCustom"}};
	Shader::ShaderPassAsset customPass;
	customPass.name = "CustomForward";
	customPass.tags = {{"LightMode", "CustomForward"}};
	customSubShader.passes = {customPass};
	shader->subShaders = {subShader, customSubShader};
	for (const auto& object : {near, far, middle})
	{
		Done(object->FindComponent<MeshRendererComponent>()->SharedMaterials()[0]->SetShaderAsset(shader));
	}
	RenderScene snapshot;
	Done(snapshot.Capture(scene, 1));
	RenderSceneContext context;
	Take(context.Begin(11, 1, snapshot));
	CameraData camera;
	camera.viewProjection = Mat4::Perspective(1.0f, 1, 0.1f, 100);
	auto results = Take(context.Cull(camera));
	DrawingSettings drawing;
	drawing.shaderTags = {{"CustomForward"}};
	drawing.sorting.criteria = SortingCriteria::CommonTransparent;
	drawing.activePipeline = "UrsaForward";
	auto list = Take(context.CreateRendererList(results, drawing, {}));
	auto entries = Take(context.Entries(list));
	Expect(entries->size() == 3 && entries->at(0).objectIndex == 1 && entries->at(1).objectIndex == 2 &&
	           entries->at(2).objectIndex == 0 && entries->at(0).subShaderIndex == 0 && entries->at(0).passIndex == 1 &&
	           entries->at(0).renderQueue == 3010,
	       "Transparent ordering or LightMode pass selection failed");
	auto repeated = Take(context.CreateRendererList(results, drawing, {}));
	auto repeatedEntries = Take(context.Entries(repeated));
	Expect(repeatedEntries->at(0).objectIndex == entries->at(0).objectIndex &&
	           repeatedEntries->at(2).objectIndex == entries->at(2).objectIndex,
	       "Identical renderer lists have a different deterministic order");
	drawing.shaderTags = {{"DepthOnly"}, {"CustomForward"}};
	list = Take(context.CreateRendererList(results, drawing, {}));
	Expect(Take(context.Entries(list))->at(0).passIndex == 0,
	       "ShaderTag request priority did not select the first available LightMode");
	drawing.activePipeline = "UrsaCustom";
	drawing.shaderTags = {{"CustomForward"}};
	list = Take(context.CreateRendererList(results, drawing, {}));
	Expect(Take(context.Entries(list))->size() == 3 && Take(context.Entries(list))->at(0).subShaderIndex == 1 &&
	           Take(context.Entries(list))->at(0).passIndex == 0,
	       "Active render pipeline did not select its compatible SubShader");
	drawing.activePipeline = "UrsaMissing";
	list = Take(context.CreateRendererList(results, drawing, {}));
	Expect(Take(context.Entries(list))->empty(), "Incompatible render pipeline SubShader was selected");
	drawing.activePipeline = "UrsaForward";
	drawing.shaderTags = {{"CustomForward"}};
	shader->sourceHash = "renderer-list-shader-v2";
	shader->subShaders[0].passes[0].tags = {{"LightMode", "CustomForward"}};
	shader->subShaders[0].passes[1].tags = {{"LightMode", "Other"}};
	list = Take(context.CreateRendererList(results, drawing, {}));
	Expect(Take(context.Entries(list))->at(0).passIndex == 0,
	       "Shader version change did not invalidate the LightMode selection cache");
	drawing.sorting.criteria = SortingCriteria::CommonOpaque;
	list = Take(context.CreateRendererList(results, drawing, {}));
	entries = Take(context.Entries(list));
	Expect(entries->at(0).objectIndex == 0 && entries->at(2).objectIndex == 1, "Opaque front-to-back order failed");
	drawing.shaderTags = {{"MissingPass"}};
	list = Take(context.CreateRendererList(results, drawing, {}));
	Expect(Take(context.Entries(list))->empty(), "Unmatched ShaderTag was drawn");
	drawing.shaderTags = {{"DepthNormals"}};
	auto missingNormals = context.CreateRendererList(results, drawing, {});
	Expect(!missingNormals && missingNormals.error.find("RendererListTestShader") != String::npos &&
	           missingNormals.error.find("DepthNormals") != String::npos,
	       "A camera normal prepass silently skipped a shader without its required pass or shader identity");
	drawing.shaderTags = {{"MissingPass"}};
	drawing.overrideMaterial = near->FindComponent<MeshRendererComponent>()->SharedMaterials()[0];
	drawing.overrideMaterialPass = 0;
	list = Take(context.CreateRendererList(results, drawing, {}));
	Expect(Take(context.Entries(list))->size() == 3 && Take(context.Entries(list))->at(0).passIndex == 0,
	       "Explicit replacement material pass failed");
	Done(context.End());
}

void TestSkinBounds()
{
	Scene scene;
	auto object = Take(scene.CreateGameObject("Skinned"));
	auto mesh = Triangle();
	auto data = Take(mesh->GetData());
	for (auto& position : data.positions)
	{
		position.x += 100;
		position.z = -5;
	}
	auto skeleton = std::make_shared<ModelSkeleton>();
	SkeletonJoint joint;
	joint.nodeLocalFileId = 1;
	joint.bindLocalTransform.position = {-100, 0, 0};
	joint.inverseBindMatrix = Mat4::Identity();
	skeleton->joints = {joint};
	skeleton->modelGuid = ResourceId::Generate();
	skeleton->localFileId = 1;
	data.skeleton = skeleton;
	data.skeletonReference = {skeleton->modelGuid, 1};
	data.skinInfluences.resize(data.positions.size());
	for (auto& influence : data.skinInfluences)
	{
		influence.weights[0] = 1;
	}
	Done(mesh->SetData(std::move(data)));
	Done(mesh->RecalculateBounds());
	auto renderer = Take(object->AddComponent<SkinnedMeshRendererComponent>());
	Done(renderer->SetSharedMesh(mesh));
	Done(renderer->SetMaterial(std::make_shared<MaterialInstance>(MaterialType::Unlit)));
	RenderScene snapshot;
	Done(snapshot.Capture(scene, 1));
	Expect(snapshot.Objects().size() == 1 && snapshot.Objects()[0].worldBounds.minimum.x == -0.5f &&
	           snapshot.Objects()[0].jointPalette.size() == 1,
	       "Skin bounds used the static mesh bounds");
	RenderSceneContext context;
	Take(context.Begin(12, 1, snapshot));
	CameraData camera;
	camera.viewProjection = Mat4::Perspective(1.0f, 1, 0.1f, 100);
	Expect(Take(context.VisibleObjects(Take(context.Cull(camera))))->size() == 1, "Visible deformed skin was culled");
	Done(context.End());
	// Move the joint while preserving the mesh's original bind-pose bounds.
	skeleton->joints[0].bindLocalTransform.position.x = -90;
	Done(snapshot.Capture(scene, 1));
	Take(context.Begin(12, 2, snapshot));
	Expect(Take(context.VisibleObjects(Take(context.Cull(camera))))->empty(),
	       "Moved skin remained in its stale bounds");
	Done(context.End());
}

void ExpectNear(Float32 actual, Float32 expected, Float32 tolerance, const char* message)
{
	if (std::abs(actual - expected) > tolerance)
	{
		throw std::runtime_error(message);
	}
}

void TestDirectionalShadowCascades()
{
	CameraData camera;
	camera.view = Mat4::LookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
	camera.projection = Mat4::Perspective(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
	camera.nearPlane = 0.1f;
	camera.farPlane = 100.0f;
	camera.orthographic = false;

	DirectionalShadowCascadeSettings settings;
	settings.cascadeCount = 4;
	settings.shadowDistance = 40.0f;
	settings.splitFractions = {0.1f, 0.3f, 0.6f};
	settings.atlasWidth = 2048;
	settings.atlasHeight = 2048;
	auto perspective = Take(ComputeDirectionalShadowCascades(camera, {0, -1, 0}, settings));
	Expect(perspective.size() == 4, "Four-cascade perspective camera returned the wrong cascade count");
	Expect(perspective[0].atlasX == 0 && perspective[0].atlasY == 0 && perspective[1].atlasX == 1024 &&
	           perspective[1].atlasY == 0 && perspective[2].atlasX == 0 && perspective[2].atlasY == 1024 &&
	           perspective[3].atlasX == 1024 && perspective[3].atlasY == 1024,
	       "Four-cascade atlas tiles were not laid out in stable row-major order");
	for (UInt32 index = 0; index < perspective.size(); ++index)
	{
		Expect(perspective[index].index == index && perspective[index].resolution == 1024,
		       "Cascade identity or tile resolution is incorrect");
		Expect(perspective[index].worldUnitsPerTexel > 0 && perspective[index].farDepth > perspective[index].nearDepth,
		       "Cascade contains invalid depth or texel coverage");
	}
	ExpectNear(perspective.front().nearDepth, 0.1f, 1e-5f, "Perspective cascade did not start at camera near plane");
	ExpectNear(perspective[0].farDepth, 4.09f, 1e-4f, "Perspective split did not use shadow-distance proportions");
	ExpectNear(perspective.back().farDepth, 40.0f, 1e-5f, "Perspective cascades did not stop at shadow distance");
	CameraData shortCamera = camera;
	shortCamera.farPlane = 20.0f;
	DirectionalShadowCascadeSettings shortRangeSettings = settings;
	shortRangeSettings.shadowDistance = 60.0f;
	const auto shortRange = Take(ComputeDirectionalShadowCascades(shortCamera, {0, -1, 0}, shortRangeSettings));
	ExpectNear(shortRange.back().farDepth, 20.0f, 1e-5f,
	           "Cascade coverage extended beyond the receiver camera far plane");

	CameraData slightlyMoved = camera;
	const Float32 subTexel = perspective[0].worldUnitsPerTexel * 0.1f;
	slightlyMoved.view = Mat4::LookAt({subTexel, 0, 0}, {subTexel, 0, -1}, {0, 1, 0});
	auto stable = Take(ComputeDirectionalShadowCascades(slightlyMoved, {0, -1, 0}, settings));
	for (UInt32 index = 0; index < perspective.size(); ++index)
	{
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				ExpectNear(stable[index].worldToShadow.At(row, column),
				           perspective[index].worldToShadow.At(row, column), 1e-5f,
				           "Sub-texel camera motion caused cascade projection shimmer");
			}
		}
	}

	camera.orthographic = true;
	camera.view = Mat4::LookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
	camera.projection = Mat4::Orthographic(20.0f, 16.0f / 9.0f, 0.1f, 100.0f);
	for (UInt32 cascadeCount : {1u, 2u, 4u})
	{
		settings.cascadeCount = cascadeCount;
		settings.atlasWidth = 2048;
		settings.atlasHeight = cascadeCount == 2 ? 1024 : 2048;
		settings.splitFractions = cascadeCount == 1   ? Vector<Float32>{}
		                          : cascadeCount == 2 ? Vector<Float32>{0.4f}
		                                              : Vector<Float32>{0.1f, 0.3f, 0.6f};
		const auto cascades = Take(ComputeDirectionalShadowCascades(camera, {0, -1, 0}, settings));
		Expect(cascades.size() == cascadeCount, "Orthographic camera failed a supported cascade count");
		ExpectNear(cascades.back().farDepth, 40.0f, 1e-5f,
		           "Orthographic cascades did not stop at the configured shadow distance");
	}

	settings.cascadeCount = 2;
	settings.splitFractions = {0.4f};
	settings.atlasWidth = 2048;
	settings.atlasHeight = 2048;
	Expect(!ComputeDirectionalShadowCascades(camera, {0, -1, 0}, settings),
	       "Cascade atlas with non-square tiles was accepted");
	settings.atlasHeight = 1024;
	settings.splitFractions = {std::numeric_limits<Float32>::quiet_NaN()};
	Expect(!ComputeDirectionalShadowCascades(camera, {0, -1, 0}, settings), "Non-finite cascade split was accepted");
	settings.cascadeCount = 4;
	settings.splitFractions = {0.4f, 0.4f, 0.8f};
	settings.atlasHeight = 2048;
	Expect(!ComputeDirectionalShadowCascades(camera, {0, -1, 0}, settings),
	       "Non-increasing cascade split ratios were accepted");
	settings.cascadeCount = 2;
	settings.atlasHeight = 1024;
	settings.splitFractions = {0.4f};
	settings.shadowDistance = camera.nearPlane;
	Expect(!ComputeDirectionalShadowCascades(camera, {0, -1, 0}, settings),
	       "Shadow distance at or before the near plane was accepted");
	settings.shadowDistance = 40;
	Expect(!ComputeDirectionalShadowCascades(camera, {0, 0, 0}, settings),
	       "Zero-length directional light was accepted");
	settings.cascadeCount = 3;
	settings.splitFractions = {0.25f, 0.5f};
	Expect(!ComputeDirectionalShadowCascades(camera, {0, -1, 0}, settings), "Unsupported cascade count was accepted");
	settings.cascadeCount = 2;
	settings.splitFractions = {0.4f};
	settings.atlasWidth = 0;
	Expect(!ComputeDirectionalShadowCascades(camera, {0, -1, 0}, settings), "Zero-width cascade atlas was accepted");
	settings.cascadeCount = 4;
	settings.splitFractions = {0.1f, 0.3f, 0.6f};
	settings.atlasWidth = 16384;
	settings.atlasHeight = 16384;
	Expect(!ComputeDirectionalShadowCascades(camera, {0, -1, 0}, settings),
	       "Cascade atlas exceeding the render-target budget was accepted");
}

std::shared_ptr<Shader::ShaderAsset> ShadowCasterShader(bool transparent)
{
	auto shader = std::make_shared<Shader::ShaderAsset>();
	shader->id = ResourceId::Generate();
	shader->sourceHash = transparent ? "shadow-caster-transparent" : "shadow-caster-opaque";
	Shader::ShaderSubShaderAsset subShader;
	subShader.tags = {{"RenderPipeline", "UrsaUniversal"}, {"Queue", transparent ? "Transparent" : "Geometry"}};
	Shader::ShaderPassAsset pass;
	pass.name = "ShadowCaster";
	pass.tags = {{"LightMode", "ShadowCaster"}};
	subShader.passes = {pass};
	shader->subShaders = {subShader};
	return shader;
}

void TestIndependentShadowCasterCulling()
{
	Scene scene;
	auto mesh = Triangle();
	auto caster = Object(scene, mesh, 100, 0, 0, AlphaMode::Opaque);
	auto nonCaster = Object(scene, mesh, 101, 0, 0, AlphaMode::Opaque);
	auto wrongLayer = Object(scene, mesh, 102, 0, 1, AlphaMode::Opaque);
	auto outsideLight = Object(scene, mesh, 150, 0, 0, AlphaMode::Opaque);
	auto receiverOnly = Object(scene, mesh, 0, -5, 0, AlphaMode::Opaque);
	auto transparent = Object(scene, mesh, 99, 0, 0, AlphaMode::Blend);
	auto inactive = Object(scene, mesh, 98, 0, 0, AlphaMode::Opaque);
	Done(inactive->SetActive(false));
	auto skinnedRoot = Take(scene.CreateGameObject("SkinnedCaster"));
	LocalTransform skinnedTransform;
	skinnedTransform.position = {100, 0, 0};
	Done(skinnedRoot->SetLocalTransform(skinnedTransform));
	MeshPtr skinnedMesh = Triangle(200);
	MeshData skinnedData = Take(skinnedMesh->GetData());
	auto skeleton = std::make_shared<ModelSkeleton>();
	SkeletonJoint joint;
	joint.nodeLocalFileId = 1;
	joint.bindLocalTransform.position = {-100, 0, 0};
	joint.inverseBindMatrix = Mat4::Identity();
	skeleton->joints = {joint};
	skeleton->modelGuid = ResourceId::Generate();
	skeleton->localFileId = 1;
	skinnedData.skeleton = skeleton;
	skinnedData.skeletonReference = {skeleton->modelGuid, 1};
	skinnedData.skinInfluences.resize(skinnedData.positions.size());
	for (auto& influence : skinnedData.skinInfluences)
	{
		influence.weights[0] = 1;
	}
	Done(skinnedMesh->SetData(std::move(skinnedData)));
	Done(skinnedMesh->RecalculateBounds());
	auto skinnedRenderer = Take(skinnedRoot->AddComponent<SkinnedMeshRendererComponent>());
	Done(skinnedRenderer->SetSharedMesh(skinnedMesh));
	Done(skinnedRenderer->SetMaterial(std::make_shared<MaterialInstance>(MaterialType::Unlit)));
	auto opaqueShader = ShadowCasterShader(false);
	auto transparentShader = ShadowCasterShader(true);
	for (const auto& object : {caster, nonCaster, wrongLayer, outsideLight, receiverOnly, inactive})
	{
		Done(object->FindComponent<MeshRendererComponent>()->SharedMaterials()[0]->SetShaderAsset(opaqueShader));
	}
	Done(skinnedRenderer->SharedMaterials()[0]->SetShaderAsset(opaqueShader));
	Done(transparent->FindComponent<MeshRendererComponent>()->SharedMaterials()[0]->SetShaderAsset(transparentShader));
	nonCaster->FindComponent<MeshRendererComponent>()->SetCastShadows(false);

	RenderScene snapshot;
	Done(snapshot.Capture(scene, 1));
	RenderSceneContext context;
	Take(context.Begin(19, 1, snapshot));
	CameraData receiverCamera;
	receiverCamera.settings.cullingMask = 1u;
	receiverCamera.viewProjection = Mat4::Perspective(1.0f, 1.0f, 0.1f, 100.0f);
	const auto cameraVisible = Take(context.VisibleObjects(Take(context.Cull(receiverCamera))));
	Expect(cameraVisible->size() == 1 && snapshot.Objects()[cameraVisible->front()].world.At(0, 3) == 0,
	       "Receiver camera unexpectedly saw a light-space caster outside its frustum");

	const Mat4 lightViewProjection =
	    Mat4::Orthographic(20.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt({100, 0, 10}, {100, 0, 0}, {0, 1, 0});
	const auto shadowCulling = Take(context.CullShadowCasters(receiverCamera, lightViewProjection));
	const auto candidates = Take(context.VisibleObjects(shadowCulling));
	Expect(candidates->size() == 3, ("Shadow culling returned " + std::to_string(candidates->size()) +
	                                 " candidates instead of the expected static, skinned and transparent casters")
	                                    .c_str());
	for (UInt32 index : *candidates)
	{
		Expect(snapshot.Objects()[index].world.At(0, 3) == 100 || snapshot.Objects()[index].world.At(0, 3) == 99,
		       "Shadow culling included a disabled, non-casting, wrong-layer or out-of-light-frustum object");
	}
	DrawingSettings drawing;
	drawing.shaderTags = {{"ShadowCaster"}};
	drawing.activePipeline = "UrsaUniversal";
	FilteringSettings filtering;
	filtering.maximumQueue = 2500;
	filtering.layerMask = receiverCamera.settings.cullingMask;
	const auto list = Take(context.CreateRendererList(shadowCulling, drawing, filtering));
	const auto entries = Take(context.Entries(list));
	Expect(entries->size() == 2 && snapshot.Objects()[entries->front().objectIndex].world.At(0, 3) == 100 &&
	           snapshot.Objects()[entries->back().objectIndex].meshSnapshot->data.skeleton != nullptr,
	       "Shadow renderer list did not exclude transparent Blend casters");
	Done(context.End());
	Expect(!context.VisibleObjects(shadowCulling), "Shadow culling results remained valid after their frame ended");
}
} // namespace

int main()
{
	try
	{
		TestPersistentCamera();
		TestSnapshotAndLists();
		TestSortingAndPasses();
		TestSkinBounds();
		TestDirectionalShadowCascades();
		TestIndependentShadowCasterCulling();
		std::cout << "Render scene, camera and renderer list tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
