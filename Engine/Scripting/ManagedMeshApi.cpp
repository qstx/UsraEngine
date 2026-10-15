#include <Scripting/ManagedSceneApi.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Mesh/Primitive.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Scene/SkinnedMeshRendererComponent.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/RenderResourceReferences.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Rendering/Shader/ComputeKernel.hpp>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Ursa::Scripting
{
namespace
{
using Json = nlohmann::json;

Json CameraFrameJson(const Rendering::CameraData& camera, UInt32 width, UInt32 height)
{
	const auto& settings = camera.settings;
	return {{"identity", ToStdString(camera.identity.ToString())},
	        {"width", width},
	        {"height", height},
	        {"settings",
	         {{"type", UInt32(settings.type)},
	          {"depth", settings.depth},
	          {"clearFlags", UInt32(settings.clearFlags)},
	          {"cullingMask", settings.cullingMask},
	          {"background", Json::array({settings.background.red, settings.background.green, settings.background.blue,
	                                      settings.background.alpha})},
	          {"viewport", Json::array({settings.viewport.x, settings.viewport.y, settings.viewport.width,
	                                    settings.viewport.height})},
	          {"targetWidth", width},
	          {"targetHeight", height}}}};
}

Json BytesJson(const Vector<UInt8>& bytes)
{
	Json result = Json::array();
	result.get_ref<Json::array_t&>().reserve(bytes.size());
	for (UInt8 value : bytes)
	{
		result.push_back(value);
	}
	return result;
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

Json HandleJson(ManagedHandle handle)
{
	if (!handle.sessionId)
	{
		return nullptr;
	}
	return {{"sessionId", handle.sessionId},
	        {"index", handle.index},
	        {"generation", handle.generation},
	        {"kind", UInt32(handle.kind)},
	        {"reserved", handle.reserved}};
}

UInt64 Unsigned(const Json& value);

UInt32 Index(const Json& value);

ManagedHandle ReadHandle(const Json& value)
{
	if (value.is_null())
	{
		return {};
	}
	const auto session = Unsigned(value.at("sessionId"));
	if (!session)
	{
		throw std::runtime_error("Null resource references must use an explicit null value");
	}
	return {session, Index(value.at("index")), Index(value.at("generation")),
	        ManagedHandleKind(Index(value.at("kind"))), Index(value.at("reserved"))};
}

Rendering::RenderContextToken ReadRenderToken(const Json& value)
{
	return {Unsigned(value.at("sessionId")), Unsigned(value.at("frameId")), Index(value.at("index")),
	        Index(value.at("generation"))};
}

Json RenderTokenJson(const Rendering::RenderContextToken& token)
{
	return {{"sessionId", token.sessionId},
	        {"frameId", token.frameId},
	        {"index", token.index},
	        {"generation", token.generation}};
}

bool SameRenderToken(const Rendering::RenderContextToken& left, const Rendering::RenderContextToken& right)
{
	return left.sessionId == right.sessionId && left.frameId == right.frameId && left.index == right.index &&
	       left.generation == right.generation;
}

UInt64 Unsigned(const Json& value)
{
	if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<Int64>() >= 0))
	{
		throw std::runtime_error("Resource count/index requires an unsigned integer");
	}
	return value.get<UInt64>();
}

UInt32 Index(const Json& value)
{
	const auto number = Unsigned(value);
	if (number > std::numeric_limits<UInt32>::max())
	{
		throw std::runtime_error("Resource index exceeds 32-bit capacity");
	}
	return UInt32(number);
}

Int32 BaseVertex(const Json& value)
{
	if (!value.is_number_integer())
	{
		throw std::runtime_error("Base vertex requires an integer");
	}
	const auto number = value.get<Int64>();
	if (number < std::numeric_limits<Int32>::min() || number > std::numeric_limits<Int32>::max())
	{
		throw std::runtime_error("Base vertex exceeds 32-bit capacity");
	}
	return Int32(number);
}

Float32 Number(const Json& value)
{
	const auto number = value.get<Float64>();
	if (!value.is_number() || !std::isfinite(number) || std::abs(number) > std::numeric_limits<Float32>::max())
	{
		throw std::runtime_error("Resource value must be a finite 32-bit number");
	}
	return Float32(number);
}

template <class T, class Read> Vector<T> Channel(const Json& value, Read read)
{
	if (!value.is_array() || value.size() > Mesh::maxDataBytes / sizeof(T))
	{
		throw std::runtime_error("Mesh channel exceeds capacity or has an invalid shape");
	}
	Vector<T> values;
	values.reserve(value.size());
	for (const auto& item : value)
	{
		values.push_back(read(item));
	}
	return values;
}

Vec3 ReadVector3(const Json& v)
{
	return {Number(v.at("x")), Number(v.at("y")), Number(v.at("z"))};
}

Vec2 ReadVector2(const Json& v)
{
	return {Number(v.at("x")), Number(v.at("y"))};
}

Vec4 ReadVector4(const Json& v)
{
	return {Number(v.at("x")), Number(v.at("y")), Number(v.at("z")), Number(v.at("w"))};
}

Color ReadColor(const Json& v)
{
	return {Number(v.at("r")), Number(v.at("g")), Number(v.at("b")), Number(v.at("a"))};
}

SubMesh ReadSubMesh(const Json& v)
{
	return {Index(v.at("firstIndex")), Index(v.at("indexCount")), BaseVertex(v.at("baseVertex"))};
}

template <class T, class Write> Json ArrayJson(const Vector<T>& values, Write write)
{
	Json result = Json::array();
	for (const auto& value : values)
	{
		result.push_back(write(value));
	}
	return result;
}

Json GeometryJson(const MeshData& data)
{
	return {
	    {"positions", ArrayJson(data.positions, [](Vec3 v) { return Json{{"x", v.x}, {"y", v.y}, {"z", v.z}}; })},
	    {"normals", ArrayJson(data.normals, [](Vec3 v) { return Json{{"x", v.x}, {"y", v.y}, {"z", v.z}}; })},
	    {"uv0", ArrayJson(data.uv0, [](Vec2 v) { return Json{{"x", v.x}, {"y", v.y}}; })},
	    {"colors", ArrayJson(data.colors, [](Color v)
	                         { return Json{{"r", v.red}, {"g", v.green}, {"b", v.blue}, {"a", v.alpha}}; })},
	    {"tangents",
	     ArrayJson(data.tangents, [](Vec4 v) { return Json{{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}}; })},
	    {"indices", ArrayJson(data.indices, [](UInt32 v) { return Json(v); })},
	    {"subMeshes", ArrayJson(data.subMeshes,
	                            [](SubMesh v)
	                            {
		                            return Json{{"firstIndex", v.firstIndex},
		                                        {"indexCount", v.indexCount},
		                                        {"baseVertex", v.baseVertex}};
	                            })},
	    {"bounds",
	     {{"empty", data.bounds.empty},
	      {"minimum", {{"x", data.bounds.minimum.x}, {"y", data.bounds.minimum.y}, {"z", data.bounds.minimum.z}}},
	      {"maximum", {{"x", data.bounds.maximum.x}, {"y", data.bounds.maximum.y}, {"z", data.bounds.maximum.z}}}}}};
}

MeshData ReadGeometry(const Json& value, const MeshData* previous = nullptr)
{
	MeshData data;
	if (previous)
	{
		data.skeleton = previous->skeleton;
		data.skeletonReference = previous->skeletonReference;
		data.skinInfluences = previous->skinInfluences;
	}
	data.positions = Channel<Vec3>(value.at("positions"), ReadVector3);
	data.normals = Channel<Vec3>(value.at("normals"), ReadVector3);
	data.uv0 = Channel<Vec2>(value.at("uv0"), ReadVector2);
	data.colors = Channel<Color>(value.at("colors"), ReadColor);
	data.tangents = Channel<Vec4>(value.at("tangents"), ReadVector4);
	data.indices = Channel<UInt32>(value.at("indices"), [](const Json& v) { return Index(v); });
	data.subMeshes = Channel<SubMesh>(value.at("subMeshes"), ReadSubMesh);
	if (value.contains("bounds"))
	{
		const auto& bounds = value["bounds"];
		data.bounds = {ReadVector3(bounds.at("minimum")), ReadVector3(bounds.at("maximum")),
		               bounds.at("empty").get<bool>()};
	}
	return data;
}
} // namespace

Result<ManagedHandle> ManagedSceneApi::RegisterMesh(const MeshPtr& mesh)
{
	if (!mesh)
	{
		return Result<ManagedHandle>::Success({});
	}
	if (!mesh->IsValid())
	{
		return Result<ManagedHandle>::Failure("Mesh has been destroyed");
	}
	auto handle = handles->Register(mesh.get(), ManagedHandleKind::Mesh);
	if (handle)
	{
		ownedMeshes[handle.value.index] = mesh;
	}
	return handle;
}

Result<ManagedHandle> ManagedSceneApi::RegisterMaterial(const std::shared_ptr<MaterialInstance>& material)
{
	if (!material)
	{
		return Result<ManagedHandle>::Success({});
	}
	if (!material->IsValid())
	{
		return Result<ManagedHandle>::Failure("Material has been destroyed");
	}
	auto handle = handles->Register(material.get(), ManagedHandleKind::Material);
	if (handle)
	{
		ownedMaterials[handle.value.index] = material;
	}
	return handle;
}

Result<MeshPtr> ManagedSceneApi::ResolveMesh(const ManagedHandle& handle) const
{
	if (!handle.sessionId)
	{
		return Result<MeshPtr>::Success(nullptr);
	}
	auto valid = handles->Resolve(handle, ManagedHandleKind::Mesh);
	auto found = ownedMeshes.find(handle.index);
	return valid && found != ownedMeshes.end() && found->second->IsValid()
	           ? Result<MeshPtr>::Success(found->second)
	           : Result<MeshPtr>::Failure(valid ? "Mesh has been destroyed" : valid.error);
}

Result<std::shared_ptr<MaterialInstance>> ManagedSceneApi::ResolveMaterial(const ManagedHandle& handle) const
{
	if (!handle.sessionId)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Success(nullptr);
	}
	auto valid = handles->Resolve(handle, ManagedHandleKind::Material);
	auto found = ownedMaterials.find(handle.index);
	return valid && found != ownedMaterials.end() && found->second->IsValid()
	           ? Result<std::shared_ptr<MaterialInstance>>::Success(found->second)
	           : Result<std::shared_ptr<MaterialInstance>>::Failure(valid ? "Material has been destroyed"
	                                                                      : valid.error);
}

Result<std::shared_ptr<ManagedSceneApi::ManagedRenderTexture>>
ManagedSceneApi::ResolveRenderTexture(const ManagedHandle& handle) const
{
	auto valid = handles->Resolve(handle, ManagedHandleKind::RenderTexture);
	auto found = ownedRenderTextures.find(handle.index);
	return valid && found != ownedRenderTextures.end()
	           ? Result<std::shared_ptr<ManagedRenderTexture>>::Success(found->second)
	           : Result<std::shared_ptr<ManagedRenderTexture>>::Failure("RenderTexture handle is stale");
}

Result<std::shared_ptr<ManagedSceneApi::ManagedGraphicsBuffer>>
ManagedSceneApi::ResolveGraphicsBuffer(const ManagedHandle& handle) const
{
	auto valid = handles->Resolve(handle, ManagedHandleKind::GraphicsBuffer);
	auto found = ownedGraphicsBuffers.find(handle.index);
	return valid && found != ownedGraphicsBuffers.end()
	           ? Result<std::shared_ptr<ManagedGraphicsBuffer>>::Success(found->second)
	           : Result<std::shared_ptr<ManagedGraphicsBuffer>>::Failure("GraphicsBuffer handle is stale");
}

Int32 ManagedSceneApi::ResourceRequest(void* context, const UInt8* input, UInt64 length, UInt8* output, UInt64 capacity,
                                       UInt64* required)
{
	auto* api = FromContext(context);
	if (!api || !required || (!input && length) || (!output && capacity) || length > maxMeshResourceBytes)
	{
		return 1;
	}
	if (input)
	{
		try
		{
			auto request = Json::parse(input, input + length);
			api->resourceResponse = ToUrsaString(Json{{"value", api->DispatchResourceRequest(request)}}.dump());
		}
		catch (const std::exception& error)
		{
			api->resourceResponse = ToUrsaString(Json{{"error", error.what()}}.dump());
		}
	}
	// A sizing retry retrieves this exact result; it never repeats a mutation.
	*required = api->resourceResponse.size();
	if (capacity < *required)
	{
		return 4;
	}
	if (*required)
	{
		std::memcpy(output, api->resourceResponse.data(), Size(*required));
	}
	api->resourceResponse.clear();
	return 0;
}

nlohmann::json ManagedSceneApi::DispatchResourceRequest(const Json& request)
{
	const auto operation = request.at("operation").get<std::string>();
	if ((operation.rfind("render", 0) == 0 || operation.rfind("graphicsBuffer.", 0) == 0 ||
	     operation.rfind("computeShader.", 0) == 0) &&
	    ownerThread != std::this_thread::get_id())
	{
		throw std::runtime_error("Rendering resource requests require the owner thread");
	}
	const auto handle = ReadHandle(request.value("handle", Json(nullptr)));
	if (operation == "render.error")
	{
		return ToStdString(lastRenderError);
	}
	if (operation == "computeShader.findKernel" || operation == "computeShader.threadGroup")
	{
		if (!resources || Unsigned(request.at("localFileId")) != 0)
		{
			throw std::runtime_error(
			    "ComputeShader resolution requires a ResourceSession and a root Shader asset reference");
		}
		const auto guid = Take(ResourceId::Parse(ToUrsaString(request.at("guid").get<std::string>())));
		const auto shader = Take(resources->LoadShader(guid));
		if (operation == "computeShader.findKernel")
		{
			return Take(Rendering::FindComputeKernel(*shader, ToUrsaString(request.at("name").get<std::string>())));
		}
		const auto* kernel = Take(Rendering::ComputeKernelAt(*shader, Index(request.at("kernel"))));
		return Json::array({kernel->threadGroupSize[0], kernel->threadGroupSize[1], kernel->threadGroupSize[2]});
	}
	if (operation == "render.context.begin")
	{
		if (std::this_thread::get_id() != ownerThread || activeRenderToken)
		{
			throw std::runtime_error("Render context begin requires the owner thread and a closed frame");
		}
		const UInt64 frameId = Unsigned(request.at("frameId"));
		Float32 aspect = request.at("aspect").get<Float32>();
		if (frameId == 0 || !std::isfinite(aspect) || aspect <= 0)
		{
			throw std::runtime_error("Render frame ID or camera aspect is invalid");
		}
		if (renderExecutor)
		{
			Done(renderExecutor->Begin(renderOutput, renderOutputFinalState, renderWindow));
			const auto target = renderExecutor->CameraTarget();
			if (target)
			{
				const auto texture = target->color ? target->color : target->depth;
				aspect = Float32(texture->desc.width) / Float32(texture->desc.height);
			}
		}
		Rendering::RenderScene::BuiltinShaderResolver resolveBuiltin;
		if (renderExecutor)
		{
			resolveBuiltin = [this](ResourceId id) { return renderExecutor->ResolveBuiltinShader(id); };
		}
		auto captured = renderScene.Capture(renderSceneOverride ? *renderSceneOverride : scene, aspect,
		                                    renderCameraFilter, renderCameraOverride, resolveBuiltin);
		if (!captured)
		{
			if (renderExecutor && renderExecutor->IsActive())
			{
				renderExecutor->Abort();
			}
			throw std::runtime_error(ToStdString(captured.error));
		}
		auto begun = renderSceneContext.Begin(renderSessionId, frameId, renderScene);
		if (!begun)
		{
			if (renderExecutor && renderExecutor->IsActive())
			{
				renderExecutor->Abort();
			}
			throw std::runtime_error(ToStdString(begun.error));
		}
		activeRenderToken = begun.value;
		activeRenderCamera.clear();
		return {{"context", RenderTokenJson(begun.value)}};
	}
	if (operation == "render.context.end" || operation == "render.context.submit" ||
	    operation == "render.context.abort")
	{
		if (!activeRenderToken || !SameRenderToken(ReadRenderToken(request.at("context")), *activeRenderToken))
		{
			throw std::runtime_error("Render context token is stale or belongs to another session");
		}
		if (operation == "render.context.submit")
		{
			if (renderExecutor)
			{
				Done(renderExecutor->Submit());
			}
			return nullptr;
		}
		String executionError;
		if (renderExecutor)
		{
			auto execution = operation == "render.context.abort" ? renderExecutor->Abort() : renderExecutor->End();
			if (!execution)
			{
				executionError = execution.error;
				if (renderExecutor->IsActive())
				{
					auto aborted = renderExecutor->Abort();
					if (!aborted)
					{
						executionError += "; cleanup: " + aborted.error;
					}
				}
			}
		}
		auto ended = renderSceneContext.End();
		if (!ended)
		{
			throw std::runtime_error(ToStdString(ended.error));
		}
		activeRenderToken.reset();
		activeRenderCamera.clear();
		renderBatches.clear();
		renderBatchResources.clear();
		if (!executionError.empty())
		{
			throw std::runtime_error(ToStdString(executionError));
		}
		return nullptr;
	}
	if (operation == "render.cameras")
	{
		Done(renderSceneContext.Validate(ReadRenderToken(request.at("context"))));
		UInt32 width = 0, height = 0;
		if (renderExecutor && renderExecutor->CameraTarget())
		{
			const auto output = renderExecutor->CameraTarget();
			const auto texture = output->color ? output->color : output->depth;
			width = texture->desc.width;
			height = texture->desc.height;
		}
		Json cameras = Json::array();
		for (const auto& camera : renderScene.Cameras())
		{
			cameras.push_back(CameraFrameJson(camera, width, height));
		}
		return cameras;
	}
	if (operation == "render.camera.setup")
	{
		const auto token = ReadRenderToken(request.at("context"));
		if (!activeRenderToken || !SameRenderToken(token, *activeRenderToken))
		{
			throw std::runtime_error("Camera setup uses a stale render context token");
		}
		auto cameraId = ResourceId::Parse(ToUrsaString(request.at("cameraPersistentId").get<std::string>()));
		if (!cameraId)
		{
			throw std::runtime_error(ToStdString(cameraId.error));
		}
		const auto camera =
		    std::find_if(renderScene.Cameras().begin(), renderScene.Cameras().end(),
		                 [&](const Rendering::CameraData& candidate) { return candidate.identity == cameraId.value; });
		if (camera == renderScene.Cameras().end())
		{
			throw std::runtime_error("Camera does not belong to the captured RenderScene");
		}
		if (renderExecutor)
		{
			Done(renderExecutor->ResetCamera());
		}
		activeRenderCamera = cameraId.value.ToString();
		UInt32 width = camera->settings.targetWidth;
		UInt32 height = camera->settings.targetHeight;
		if (renderExecutor)
		{
			const auto output = renderExecutor->CameraTarget();
			if (output)
			{
				const auto texture = output->color ? output->color : output->depth;
				width = texture->desc.width;
				height = texture->desc.height;
			}
		}
		const auto& settings = camera->settings;
		return {{"width", width},
		        {"height", height},
		        {"settings",
		         {{"type", UInt32(settings.type)},
		          {"depth", settings.depth},
		          {"clearFlags", UInt32(settings.clearFlags)},
		          {"cullingMask", settings.cullingMask},
		          {"background", Json::array({settings.background.red, settings.background.green,
		                                      settings.background.blue, settings.background.alpha})},
		          {"viewport", Json::array({settings.viewport.x, settings.viewport.y, settings.viewport.width,
		                                    settings.viewport.height})},
		          {"targetWidth", width},
		          {"targetHeight", height}}}};
	}
	if (operation == "render.cull")
	{
		const auto token = ReadRenderToken(request.at("context"));
		if (!activeRenderToken || !SameRenderToken(token, *activeRenderToken))
		{
			throw std::runtime_error("Cull request uses a stale render context token");
		}
		Done(renderSceneContext.Validate(token));
		auto cameraId = ResourceId::Parse(ToUrsaString(request.at("cameraPersistentId").get<std::string>()));
		if (!cameraId)
		{
			throw std::runtime_error(ToStdString(cameraId.error));
		}
		const auto camera =
		    std::find_if(renderScene.Cameras().begin(), renderScene.Cameras().end(),
		                 [&](const Rendering::CameraData& candidate) { return candidate.identity == cameraId.value; });
		if (camera == renderScene.Cameras().end())
		{
			throw std::runtime_error("Camera does not belong to the captured RenderScene");
		}
		if (!activeRenderCamera.empty() && activeRenderCamera != cameraId.value.ToString())
		{
			throw std::runtime_error("Cull camera differs from the active camera properties");
		}
		auto results = renderSceneContext.Cull(*camera);
		if (!results)
		{
			throw std::runtime_error(ToStdString(results.error));
		}
		return {{"context", RenderTokenJson(results.value.context)},
		        {"index", results.value.index},
		        {"generation", results.value.generation}};
	}
	if (operation == "render.rendererList.create")
	{
		const auto token = ReadRenderToken(request.at("context"));
		if (!activeRenderToken || !SameRenderToken(token, *activeRenderToken))
		{
			throw std::runtime_error("RendererList request uses a stale render context token");
		}
		Rendering::CullingResults culling;
		culling.context = ReadRenderToken(request.at("cullingContext"));
		culling.index = Index(request.at("cullingIndex"));
		culling.generation = Index(request.at("cullingGeneration"));
		Rendering::DrawingSettings drawing;
		if (request.contains("shaderTags"))
		{
			if (!request["shaderTags"].is_array())
			{
				throw std::runtime_error("RendererList ShaderTag list must be an array");
			}
			for (const Json& tag : request["shaderTags"])
			{
				if (!tag.is_string())
				{
					throw std::runtime_error("RendererList ShaderTag must be a string");
				}
				drawing.shaderTags.push_back({ToUrsaString(tag.get<std::string>())});
			}
		}
		const UInt32 criteria = request.value("sortingCriteria", UInt32{1});
		if (criteria > static_cast<UInt32>(Rendering::SortingCriteria::CommonTransparent))
		{
			throw std::runtime_error("RendererList sorting criteria is invalid");
		}
		drawing.sorting.criteria = static_cast<Rendering::SortingCriteria>(criteria);
		if (request.contains("cameraPosition") && !request["cameraPosition"].is_null())
		{
			const auto& position = request.at("cameraPosition");
			if (!position.is_array() || position.size() != 3 || !position[0].is_number() || !position[1].is_number() ||
			    !position[2].is_number())
			{
				throw std::runtime_error("RendererList camera position must be a three-element numeric array");
			}
			drawing.sorting.cameraPosition = {position[0].get<Float32>(), position[1].get<Float32>(),
			                                  position[2].get<Float32>()};
		}
		else if (!activeRenderCamera.empty())
		{
			const auto camera =
			    std::find_if(renderScene.Cameras().begin(), renderScene.Cameras().end(),
			                 [&](const auto& value) { return value.identity.ToString() == activeRenderCamera; });
			if (camera != renderScene.Cameras().end())
			{
				drawing.sorting.cameraPosition = camera->position;
			}
		}
		drawing.activePipeline = ToUrsaString(request.value("activePipeline", std::string("UrsaForward")));
		drawing.overrideMaterialPass = request.value("overrideMaterialPass", Int32{-1});
		if (request.contains("overrideMaterial") && !request["overrideMaterial"].is_null())
		{
			drawing.overrideMaterial = Take(ResolveMaterial(ReadHandle(request["overrideMaterial"])));
		}
		const Int32 minimumQueue = request.value("minimumQueue", Int32{0});
		const Int32 maximumQueue = request.value("maximumQueue", Int32{5000});
		const UInt32 layerMask = request.value("layerMask", UInt32{0xffffffffu});
		auto list = renderSceneContext.CreateRendererList(culling, drawing, {minimumQueue, maximumQueue, layerMask});
		if (!list)
		{
			throw std::runtime_error(ToStdString(list.error));
		}
		return {{"context", RenderTokenJson(list.value.context)},
		        {"index", list.value.index},
		        {"generation", list.value.generation}};
	}
	if (operation == "scriptableasset.graph.load")
	{
		if (!resources)
		{
			throw std::runtime_error("ScriptableAsset loading requires a ResourceSession");
		}
		auto guid = ResourceId::Parse(ToUrsaString(request.at("guid").get<std::string>()));
		if (!guid)
		{
			throw std::runtime_error(ToStdString(guid.error));
		}
		const AssetReference root{guid.value, Unsigned(request.at("localFileId"))};
		auto graph = Take(LoadScriptableAssetGraph(*resources, root));
		Json result = Json::array();
		for (const auto& asset : graph.assets)
		{
			result.push_back({{"guid", ToStdString(asset.SourceGuid().ToString())},
			                  {"localFileId", asset.LocalFileId()},
			                  {"typeId", ToStdString(asset.TypeId())},
			                  {"typeVersion", asset.TypeVersion()},
			                  {"payload", asset.Payload()}});
		}
		return result;
	}
	if (operation == "gameObject.layer.get")
	{
		return Take(Take(Resolve(handle))->Layer());
	}
	if (operation == "gameObject.layer.set")
	{
		Done(Take(Resolve(handle))->SetLayer(request.at("value").get<UInt32>()));
		return nullptr;
	}
	if (operation == "camera.settings.get" || operation == "camera.settings.set")
	{
		auto* camera = dynamic_cast<CameraComponent*>(Take(ResolveComponent(handle)));
		if (!camera)
		{
			throw std::runtime_error("Camera rendering settings require a CameraComponent");
		}
		ArchiveWriter writer;
		Done(camera->Serialize(writer));
		auto document = Take(ArchiveToJson(writer));
		if (operation == "camera.settings.get")
		{
			return document.at("rendering");
		}
		document["rendering"] = request.at("data");
		auto reader = Take(ArchiveFromJson(document));
		SerializationContext context;
		Done(camera->Deserialize(reader, context));
		return nullptr;
	}
	if (operation == "gameObject.primitive")
	{
		if (!resources)
		{
			throw std::runtime_error("Primitive creation requires a ResourceSession");
		}
		if (request.value("type", std::string{}) != "Cube")
		{
			throw std::runtime_error("Unsupported primitive type");
		}
		auto object = Take(CreatePrimitive(scene, *resources, PrimitiveType::Cube));
		auto registered = Register(object);
		if (!registered)
		{
			scene.DestroyGameObject(object);
			throw std::runtime_error(ToStdString(registered.error));
		}
		return HandleJson(registered.value);
	}
	if (operation == "mesh.create")
	{
		return HandleJson(Take(RegisterMesh(Take(Mesh::Create()))));
	}
	if (operation == "mesh.load")
	{
		if (!resources)
		{
			throw std::runtime_error("Mesh loading requires a ResourceSession");
		}
		auto guid = Take(ResourceId::Parse(ToUrsaString(request.at("guid").get<std::string>())));
		return HandleJson(Take(RegisterMesh(Take(resources->LoadMesh(guid, request.at("localFileId").get<UInt64>())))));
	}
	if (operation == "material.create")
	{
		const auto type = request.value("type", std::string("Unlit"));
		if (type != "Unlit" && type != "PbrMetallicRoughness")
		{
			throw std::runtime_error("Unknown Material type");
		}
		auto material = MakeShared<MaterialInstance>(
		    Memory::MemoryTag::Assets, type == "Unlit" ? MaterialType::Unlit : MaterialType::PbrMetallicRoughness);
		const auto shaderId = type == "Unlit" ? Shader::BuiltinUnlitShaderId() : Shader::BuiltinPbrShaderId();
		Done(material->MigrateLegacyToShader(shaderId));
		auto shader =
		    resources ? Take(resources->LoadShader(shaderId))
		              : MakeShared<const Shader::ShaderAsset>(Memory::MemoryTag::Assets,
		                                                      type == "Unlit" ? Shader::CreateBuiltinUnlitShaderAsset()
		                                                                      : Shader::CreateBuiltinPbrShaderAsset());
		Done(material->SetShaderAsset(std::move(shader)));
		return HandleJson(Take(RegisterMaterial(material)));
	}
	if (operation == "material.load")
	{
		if (!resources)
		{
			throw std::runtime_error("Material loading requires a ResourceSession");
		}
		auto guid = Take(ResourceId::Parse(ToUrsaString(request.at("guid").get<std::string>())));
		return HandleJson(
		    Take(RegisterMaterial(Take(resources->LoadMaterial(guid, request.at("localFileId").get<UInt64>())))));
	}
	if (operation == "resource.valid")
	{
		if (!handle.sessionId)
		{
			return false;
		}
		if (handle.kind == ManagedHandleKind::Asset)
		{
			return bool(handles->Resolve(handle, ManagedHandleKind::Asset)) &&
			       ownedTextures.find(handle.index) != ownedTextures.end();
		}
		if (handle.kind == ManagedHandleKind::Mesh)
		{
			return bool(ResolveMesh(handle));
		}
		if (handle.kind == ManagedHandleKind::Material)
		{
			return bool(ResolveMaterial(handle));
		}
		if (handle.kind == ManagedHandleKind::RenderTexture)
		{
			return bool(ResolveRenderTexture(handle));
		}
		return handle.kind == ManagedHandleKind::GraphicsBuffer && bool(ResolveGraphicsBuffer(handle));
	}
	if (operation == "renderTexture.create")
	{
		if (!device)
		{
			throw std::runtime_error("RenderTexture creation requires an active RHI device");
		}
		auto texture = Take(Rendering::CreateRenderTexture(
		    *device, {Index(request.at("width")), Index(request.at("height")), Index(request.at("format")),
		              Index(request.at("depthBufferBits")), request.at("enableRandomWrite").get<bool>(),
		              Index(request.at("msaaSamples"))}));
		auto registered = Take(handles->Register(texture.get(), ManagedHandleKind::RenderTexture));
		ownedRenderTextures[registered.index] = std::move(texture);
		return HandleJson(registered);
	}
	if (operation == "renderTexture.release")
	{
		Take(handles->Resolve(handle, ManagedHandleKind::RenderTexture));
		Done(handles->Invalidate(handle));
		ownedRenderTextures.erase(handle.index);
		return nullptr;
	}
	if (operation == "renderTexture.readback")
	{
		if (activeRenderToken)
		{
			throw std::runtime_error("RenderTexture readback requires a finished render context");
		}
		if (!device)
		{
			throw std::runtime_error("RenderTexture readback requires a device");
		}
		return BytesJson(Take(Rendering::ReadRenderTexture(*device, *Take(ResolveRenderTexture(handle)))));
	}
	if (operation == "graphicsBuffer.create")
	{
		if (!device)
		{
			throw std::runtime_error("GraphicsBuffer creation requires an active RHI device");
		}
		auto resource = Take(Rendering::CreateGraphicsBuffer(*device, Index(request.at("target")),
		                                                     Index(request.at("count")), Index(request.at("stride"))));
		auto registered = Take(handles->Register(resource.get(), ManagedHandleKind::GraphicsBuffer));
		ownedGraphicsBuffers[registered.index] = std::move(resource);
		return HandleJson(registered);
	}
	if (operation == "graphicsBuffer.release")
	{
		Take(handles->Resolve(handle, ManagedHandleKind::GraphicsBuffer));
		Done(handles->Invalidate(handle));
		ownedGraphicsBuffers.erase(handle.index);
		return nullptr;
	}
	if (operation == "graphicsBuffer.readback")
	{
		if (activeRenderToken)
		{
			throw std::runtime_error("GraphicsBuffer readback requires a finished render context");
		}
		if (!device)
		{
			throw std::runtime_error("GraphicsBuffer readback requires a device");
		}
		return BytesJson(Take(Rendering::ReadGraphicsBuffer(*device, *Take(ResolveGraphicsBuffer(handle)),
		                                                    Size(Unsigned(request.at("offset"))),
		                                                    Size(Unsigned(request.at("size"))))));
	}
	if (operation == "resource.reference")
	{
		ArchiveWriter writer;
		writer.SetContext(SerializationContext::Active());
		if (handle.kind == ManagedHandleKind::Mesh)
		{
			auto mesh = Take(ResolveMesh(handle));
			if (mesh && !mesh->AssetIdentity().guid && !writer.Context())
			{
				return {{"kind", "local"}, {"id", ToStdString(mesh->LocalIdentity().ToString())}};
			}
			Done(WriteMeshBinding(writer, "value", mesh));
		}
		else if (handle.kind == ManagedHandleKind::Material)
		{
			auto material = Take(ResolveMaterial(handle));
			if (material && !material->AssetIdentity().guid && !writer.Context())
			{
				return {{"kind", "local"}, {"id", ToStdString(material->LocalIdentity().ToString())}};
			}
			Done(WriteMaterialBinding(writer, "value", material));
		}
		else
		{
			throw std::runtime_error("Invalid render resource reference kind");
		}
		return Take(ArchiveToJson(writer))["value"];
	}
	if (operation == "resource.restore")
	{
		auto reader = Take(ArchiveFromJson(Json{{"value", request.at("data")}}));
		SerializationContext fallback(resources);
		auto* active = SerializationContext::Active();
		if (active && !active->ResourceLoader())
		{
			auto service = active->Service<RenderResourceReferences>();
			if (service)
			{
				fallback.Provide<RenderResourceReferences>(*service.value);
			}
		}
		auto& context = active && active->ResourceLoader() ? *active : fallback;
		const auto type = request.at("type").get<std::string>();
		if (type == "Mesh")
		{
			return HandleJson(Take(RegisterMesh(Take(ReadMeshBinding(reader, "value", context)))));
		}
		if (type == "Material")
		{
			return HandleJson(Take(RegisterMaterial(Take(ReadMaterialBinding(reader, "value", context)))));
		}
		throw std::runtime_error("Unsupported render resource restore kind");
	}
	if (operation.rfind("mesh.", 0) == 0)
	{
		auto mesh = Take(ResolveMesh(handle));
		if (!mesh)
		{
			throw std::runtime_error("Mesh handle cannot be null");
		}
		if (operation == "mesh.clone")
		{
			return HandleJson(Take(RegisterMesh(Take(mesh->Clone()))));
		}
		if (operation == "mesh.destroy")
		{
			Done(mesh->Destroy());
			Done(handles->Invalidate(handle));
			ownedMeshes.erase(handle.index);
			return nullptr;
		}
		if (operation == "mesh.get")
		{
			return GeometryJson(Take(mesh->GetData()));
		}
		if (operation == "mesh.version")
		{
			return mesh->Version();
		}
		if (operation == "mesh.set")
		{
			auto previous = Take(mesh->GetData());
			Done(mesh->SetData(ReadGeometry(request.at("data"), &previous)));
		}
		else if (operation == "mesh.vertices")
		{
			Done(mesh->SetVertices(Channel<Vec3>(request.at("data"), ReadVector3)));
		}
		else if (operation == "mesh.normals")
		{
			Done(mesh->SetNormals(Channel<Vec3>(request.at("data"), ReadVector3)));
		}
		else if (operation == "mesh.uv0")
		{
			Done(mesh->SetUv0(Channel<Vec2>(request.at("data"), ReadVector2)));
		}
		else if (operation == "mesh.colors")
		{
			Done(mesh->SetColors(Channel<Color>(request.at("data"), ReadColor)));
		}
		else if (operation == "mesh.tangents")
		{
			Done(mesh->SetTangents(Channel<Vec4>(request.at("data"), ReadVector4)));
		}
		else if (operation == "mesh.indices")
		{
			Done(mesh->SetIndices(Channel<UInt32>(request.at("data"), [](const Json& v) { return Index(v); })));
		}
		else if (operation == "mesh.subMeshCount")
		{
			Done(mesh->SetSubMeshCount(Size(Unsigned(request.at("data")))));
		}
		else if (operation == "mesh.subMesh")
		{
			Done(mesh->SetSubMesh(Size(Unsigned(request.at("index"))), ReadSubMesh(request.at("data"))));
		}
		else if (operation == "mesh.triangles.get")
		{
			return ArrayJson(Take(mesh->GetTriangles(Size(Unsigned(request.at("index"))))),
			                 [](UInt32 v) { return Json(v); });
		}
		else if (operation == "mesh.triangles.set")
		{
			Done(mesh->SetTriangles(Size(Unsigned(request.at("index"))),
			                        Channel<UInt32>(request.at("data"), [](const Json& v) { return Index(v); })));
		}
		else if (operation == "mesh.clear")
		{
			Done(mesh->Clear());
		}
		else if (operation == "mesh.recalculateNormals")
		{
			Done(mesh->RecalculateNormals());
		}
		else if (operation == "mesh.recalculateBounds")
		{
			Done(mesh->RecalculateBounds());
		}
		else
		{
			throw std::runtime_error("Unsupported Mesh operation");
		}
		return nullptr;
	}
	if (operation.rfind("binding.", 0) == 0)
	{
		auto* component = Take(ResolveComponent(handle));
		if (operation == "binding.mesh.get")
		{
			MeshPtr mesh;
			if (auto* filter = dynamic_cast<MeshFilterComponent*>(component))
			{
				mesh = request.value("instance", false) ? Take(filter->Mesh()) : filter->SharedMesh();
			}
			else if (auto* skin = dynamic_cast<SkinnedMeshRendererComponent*>(component))
			{
				mesh = skin->SharedMesh();
			}
			else
			{
				throw std::runtime_error("Component has no Mesh binding");
			}
			return HandleJson(Take(RegisterMesh(mesh)));
		}
		if (operation == "binding.mesh.set")
		{
			auto mesh = Take(ResolveMesh(ReadHandle(request.at("data"))));
			if (auto* filter = dynamic_cast<MeshFilterComponent*>(component))
			{
				Done(request.value("instance", false) ? filter->SetMesh(std::move(mesh))
				                                      : filter->SetSharedMesh(std::move(mesh)));
			}
			else if (auto* skin = dynamic_cast<SkinnedMeshRendererComponent*>(component))
			{
				Done(skin->SetSharedMesh(std::move(mesh)));
			}
			else
			{
				throw std::runtime_error("Component has no Mesh binding");
			}
			return nullptr;
		}
		auto* renderer = dynamic_cast<RendererComponent*>(component);
		if (!renderer)
		{
			throw std::runtime_error("Component is not a Renderer");
		}
		if (operation == "binding.materials.get")
		{
			const bool instance = request.value("instance", false);
			const bool first = request.value("first", false);
			Vector<std::shared_ptr<MaterialInstance>> values;
			if (first)
			{
				values = {instance ? Take(renderer->Material()) : renderer->SharedMaterial()};
			}
			else
			{
				values = instance ? Take(renderer->Materials()) : renderer->SharedMaterials();
			}
			Json result = Json::array();
			for (const auto& value : values)
			{
				result.push_back(HandleJson(Take(RegisterMaterial(value))));
			}
			return result;
		}
		if (operation == "binding.materials.set")
		{
			const auto& data = request.at("data");
			if (!data.is_array() || data.size() > Mesh::maxSubMeshes)
			{
				throw std::runtime_error("Renderer slot capacity exceeded");
			}
			Vector<std::shared_ptr<MaterialInstance>> values;
			for (const auto& item : data)
			{
				values.push_back(Take(ResolveMaterial(ReadHandle(item))));
			}
			const bool shared = request.value("shared", true);
			if (request.value("first", false))
			{
				if (values.size() != 1)
				{
					throw std::runtime_error("Single material setter needs one value");
				}
				Done(shared ? renderer->SetSharedMaterial(values[0]) : renderer->SetMaterial(values[0]));
			}
			else
			{
				Done(shared ? renderer->SetSharedMaterials(std::move(values))
				            : renderer->SetMaterials(std::move(values)));
			}
			return nullptr;
		}
	}
	if (operation.rfind("material.", 0) == 0)
	{
		auto material = Take(ResolveMaterial(handle));
		if (!material)
		{
			throw std::runtime_error("Material handle cannot be null");
		}
		if (operation == "material.clone")
		{
			return HandleJson(Take(RegisterMaterial(material->Clone())));
		}
		if (operation == "material.destroy")
		{
			Done(material->Destroy());
			Done(handles->Invalidate(handle));
			ownedMaterials.erase(handle.index);
			return nullptr;
		}
		if (operation == "material.get")
		{
			const auto& p = material->Parameters();
			return {
			    {"baseColor",
			     {{"r", p.baseColor.red}, {"g", p.baseColor.green}, {"b", p.baseColor.blue}, {"a", p.baseColor.alpha}}},
			    {"metallic", p.metallic},
			    {"roughness", p.roughness},
			    {"normalScale", p.normalScale},
			    {"occlusionStrength", p.occlusionStrength},
			    {"alphaCutoff", p.alphaCutoff},
			    {"alphaMode", UInt32(p.alphaMode)},
			    {"emissive",
			     {{"r", p.emissive.red}, {"g", p.emissive.green}, {"b", p.emissive.blue}, {"a", p.emissive.alpha}}}};
		}
		if (operation == "material.texture.get")
		{
			const auto reference = material->TextureReference(MaterialTextureSlot(Index(request.at("slot"))));
			return reference.guid ? AssetReferenceToJson(reference) : Json(nullptr);
		}
		if (operation == "material.shader.get")
		{
			return material->ShaderId() ? AssetReferenceToJson({material->ShaderId(), 0}) : Json(nullptr);
		}
		if (operation == "material.keywords")
		{
			return ArrayJson(material->Keywords(), [](const String& v) { return Json(ToStdString(v)); });
		}
		if (operation == "material.sampler.get")
		{
			const auto sampler = material->Parameters().sampler;
			return {{"linear", sampler.linear}, {"repeat", sampler.repeat}};
		}
		if (operation == "material.property.get" || operation == "material.property.set" ||
		    operation == "material.property.remove")
		{
			const String name = ToUrsaString(request.at("name").get<std::string>());
			auto shader = material->Shader();
			if (!shader && resources)
			{
				shader = Take(resources->LoadShader(material->ShaderId()));
			}
			if (!shader)
			{
				throw std::runtime_error("Material property access requires its Shader metadata");
			}
			auto current = Take(material->PropertyValue(name, *shader));
			if (operation == "material.property.remove")
			{
				Done(material->RemovePropertyOverride(name));
				return nullptr;
			}
			if (operation == "material.property.get")
			{
				if (current.type == Shader::PropertyType::Float || current.type == Shader::PropertyType::Range)
				{
					return current.floatValue;
				}
				if (current.type == Shader::PropertyType::Int)
				{
					return current.intValue;
				}
				if (current.type == Shader::PropertyType::Texture2D)
				{
					return current.textureId ? AssetReferenceToJson({current.textureId, current.textureLocalFileId})
					                         : Json(nullptr);
				}
				if (current.type == Shader::PropertyType::Color)
				{
					return {{"r", current.vectorValue[0]},
					        {"g", current.vectorValue[1]},
					        {"b", current.vectorValue[2]},
					        {"a", current.vectorValue[3]}};
				}
				return {{"x", current.vectorValue[0]},
				        {"y", current.vectorValue[1]},
				        {"z", current.vectorValue[2]},
				        {"w", current.vectorValue[3]}};
			}
			const auto type = request.at("type").get<std::string>();
			const auto& data = request.at("data");
			MaterialPropertyValue value;
			value.type = current.type;
			if (type == "Float" &&
			    (current.type == Shader::PropertyType::Float || current.type == Shader::PropertyType::Range))
			{
				value.floatValue = Number(data);
			}
			else if (type == "Int" && current.type == Shader::PropertyType::Int)
			{
				value.intValue = BaseVertex(data);
			}
			else if (type == "Color" && current.type == Shader::PropertyType::Color)
			{
				const auto c = ReadColor(data);
				value.vectorValue = {c.red, c.green, c.blue, c.alpha};
			}
			else if (type == "Vector" && current.type == Shader::PropertyType::Vector)
			{
				const auto v = ReadVector4(data);
				value.vectorValue = {v.x, v.y, v.z, v.w};
			}
			else if (type == "Texture2D" && current.type == Shader::PropertyType::Texture2D)
			{
				if (!data.is_null())
				{
					if (!resources)
					{
						throw std::runtime_error("Texture property binding requires a ResourceSession");
					}
					const auto reference = Take(AssetReferenceFromJson(data, "Texture property"));
					value.textureId = reference.guid;
					value.textureLocalFileId = reference.localFileId;
					value.texture = Take(resources->LoadTexture(reference.guid, reference.localFileId));
				}
				else
				{
					value.textureDefault = "white";
				}
			}
			else
			{
				throw std::runtime_error("Shader property type does not match the requested setter");
			}
			Done(material->SetPropertyValue(name, *shader, std::move(value)));
			return nullptr;
		}
		const auto& value = request.at("data");
		if (operation == "material.shader.set")
		{
			if (!resources)
			{
				throw std::runtime_error("Shader assignment requires a ResourceSession");
			}
			const auto reference = Take(AssetReferenceFromJson(value, "Material Shader"));
			if (reference.localFileId)
			{
				throw std::runtime_error("Shader sub-assets are not supported");
			}
			Done(material->SetShaderAsset(Take(resources->LoadShader(reference.guid))));
		}
		else if (operation == "material.texture.set")
		{
			AssetReference reference;
			MaterialTexturePtr texture;
			if (!value.is_null())
			{
				if (!resources)
				{
					throw std::runtime_error("Texture assignment requires a ResourceSession");
				}
				reference = Take(AssetReferenceFromJson(value, "Material Texture"));
				texture = Take(resources->LoadTexture(reference.guid, reference.localFileId));
			}
			Done(material->SetTextureReference(MaterialTextureSlot(Index(request.at("slot"))), reference,
			                                   std::move(texture)));
		}
		else if (operation == "material.sampler.set")
		{
			Done(material->SetSampler({value.at("linear").get<bool>(), value.at("repeat").get<bool>()}));
		}
		else if (operation == "material.color")
		{
			Done(material->SetBaseColor(ReadColor(value)));
		}
		else if (operation == "material.metallic")
		{
			Done(material->SetMetallic(Number(value)));
		}
		else if (operation == "material.roughness")
		{
			Done(material->SetRoughness(Number(value)));
		}
		else if (operation == "material.normalScale")
		{
			Done(material->SetNormalScale(Number(value)));
		}
		else if (operation == "material.occlusionStrength")
		{
			Done(material->SetOcclusionStrength(Number(value)));
		}
		else if (operation == "material.emissive")
		{
			Done(material->SetEmissive(ReadColor(value)));
		}
		else if (operation == "material.alphaMode")
		{
			Done(material->SetAlphaMode(AlphaMode(value.get<UInt32>()), material->Parameters().alphaCutoff));
		}
		else if (operation == "material.alphaCutoff")
		{
			Done(material->SetAlphaMode(material->Parameters().alphaMode, Number(value)));
		}
		else if (operation == "material.keyword")
		{
			Done(material->SetKeywordEnabled(ToUrsaString(request.at("name").get<std::string>()), value.get<bool>()));
		}
		else
		{
			throw std::runtime_error("Unsupported Material operation");
		}
		return nullptr;
	}
	throw std::runtime_error("Unsupported resource request " + operation);
}
} // namespace Ursa::Scripting
