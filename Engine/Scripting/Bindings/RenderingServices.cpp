#include <Scripting/Bindings/RenderingServices.hpp>
#include <Scripting/Bindings/RuntimeServices.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/Bindings/BindingMath.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Rendering/Scene/DirectionalShadowCascades.hpp>
#include <cmath>

namespace Ursa::Scripting::Bindings
{
namespace
{
ManagedSceneApi& RenderApi(BindingCall& call)
{
	auto& api = RuntimeServices::SceneApi(call);
	api.RequireRendering();
	return api;
}

Ursa::Rendering::RenderContextToken ReadToken(const RenderingServices::Token& token)
{
	return {token.F_sessionId, token.F_frameId, token.F_index, token.F_generation};
}

RenderingServices::Token WriteToken(const Ursa::Rendering::RenderContextToken& token)
{
	return {token.sessionId, token.frameId, token.index, token.generation};
}
} // namespace

void RenderingServices::ValidateActive(ManagedSceneApi& api, const Token& token)
{
	if (!api.activeRenderToken || token.F_sessionId != api.activeRenderToken->sessionId ||
	    token.F_frameId != api.activeRenderToken->frameId || token.F_index != api.activeRenderToken->index ||
	    token.F_generation != api.activeRenderToken->generation)
	{
		throw std::runtime_error("Render context token is stale or belongs to another session");
	}
	CompleteBindingResult(api.renderSceneContext.Validate(ReadToken(token)));
}

RenderingServices::CameraFrame RenderingServices::WriteCamera(BindingCall& call, ManagedSceneApi& api,
                                                              const Ursa::Rendering::CameraData& camera)
{
	CameraFrame result;
	result.present = 1;
	result.F_Identity = call.arena.StringValue(camera.identity.ToString());
	result.F_OutputIdentity = 0;
	result.F_OutputGeneration = 0;
	if (api.renderOutput)
	{
		result.F_OutputIdentity = api.renderOutput->identity;
		result.F_OutputGeneration = api.renderOutput->generation ^ (UInt64(api.renderOutputFinalState) << 48);
	}
	else if (api.renderWindow)
	{
		result.F_OutputIdentity = UInt64(reinterpret_cast<UIntPtr>(api.renderWindow.get()));
		result.F_OutputGeneration = api.renderWindow->Statistics().rebuilds + 1;
	}
	const auto& settings = camera.settings;
	result.F_Width = settings.targetWidth;
	result.F_Height = settings.targetHeight;
	if (api.renderExecutor && api.renderExecutor->CameraTarget())
	{
		const auto output = api.renderExecutor->CameraTarget();
		const auto texture = output->color ? output->color : output->depth;
		result.F_Width = texture->desc.width;
		result.F_Height = texture->desc.height;
		result.F_OutputIsSrgb = texture->desc.format == Ursa::Rhi::Format::RgbA8Srgb;
	}
	result.F_View = WriteMatrix<Wire::Value_Ursa_Matrix4x4>(camera.view);
	result.F_Projection = WriteMatrix<Wire::Value_Ursa_Matrix4x4>(camera.projection);
	result.F_InverseViewProjection = WriteMatrix<Wire::Value_Ursa_Matrix4x4>(camera.inverseViewProjection);
	result.F_Position = WriteVector3<Wire::Value_Ursa_Vector3>(camera.position);
	result.F_NearPlane = camera.nearPlane;
	result.F_FarPlane = camera.farPlane;
	result.F_Orthographic = camera.orthographic;
	result.F_Settings.present = 1;
	result.F_Settings.F_Type = UInt32(settings.type);
	result.F_Settings.F_Depth = settings.depth;
	result.F_Settings.F_ClearFlags = UInt32(settings.clearFlags);
	result.F_Settings.F_Background = WriteColor<Wire::Value_Ursa_Color>(settings.background);
	result.F_Settings.F_CullingMask = settings.cullingMask;
	result.F_Settings.F_Viewport = {settings.viewport.x, settings.viewport.y, settings.viewport.width,
	                                settings.viewport.height};
	result.F_Settings.F_TargetWidth = result.F_Width;
	result.F_Settings.F_TargetHeight = result.F_Height;
	return result;
}

const Ursa::Rendering::CameraData& RenderingServices::FindCamera(ManagedSceneApi& api, const Utf8View& identity)
{
	const auto id = TakeBindingResult(ResourceId::Parse(ReadString(identity)));
	const auto& cameras = api.renderScene.Cameras();
	const auto camera =
	    std::find_if(cameras.begin(), cameras.end(), [&](const auto& value) { return value.identity == id; });
	if (camera == cameras.end())
	{
		throw std::runtime_error("Camera does not belong to the captured RenderScene");
	}
	return *camera;
}

Result<RenderingServices::Token> RenderingServices::Begin(BindingCall& call, UInt64 frameId, Float32 aspect)
{
	auto& api = RenderApi(call);
	if (api.activeRenderToken || !frameId || !std::isfinite(aspect) || aspect <= 0)
	{
		return Result<Token>::Failure("Render begin requires a closed frame, a frame ID and a positive finite aspect");
	}
	if (api.renderExecutor)
	{
		CompleteBindingResult(
		    api.renderExecutor->Begin(api.renderOutput, api.renderOutputFinalState, api.renderWindow));
		const auto target = api.renderExecutor->CameraTarget();
		if (target)
		{
			const auto texture = target->color ? target->color : target->depth;
			aspect = Float32(texture->desc.width) / Float32(texture->desc.height);
		}
	}
	try
	{
		Ursa::Rendering::RenderScene::BuiltinShaderResolver resolveBuiltin;
		if (api.renderExecutor)
		{
			resolveBuiltin = [&](ResourceId id) { return api.renderExecutor->ResolveBuiltinShader(id); };
		}
		CompleteBindingResult(api.renderScene.Capture(api.renderSceneOverride ? *api.renderSceneOverride : api.scene,
		                                              aspect, api.renderCameraFilter, api.renderCameraOverride,
		                                              resolveBuiltin));
		api.renderScene.ExcludeObjects(api.renderObjectExclusions);
		const auto begun =
		    TakeBindingResult(api.renderSceneContext.Begin(api.renderSessionId, frameId, api.renderScene));
		api.activeRenderToken = begun;
		api.activeRenderCamera.clear();
		return Result<Token>::Success(WriteToken(begun));
	}
	catch (...)
	{
		if (api.renderExecutor && api.renderExecutor->IsActive())
		{
			api.renderExecutor->Abort();
		}
		throw;
	}
}

Result<void> RenderingServices::End(BindingCall& call, const Token& token)
{
	return Finish(call, token, false);
}

Result<void> RenderingServices::Abort(BindingCall& call, const Token& token)
{
	return Finish(call, token, true);
}

Result<void> RenderingServices::Finish(BindingCall& call, const Token& token, bool abort)
{
	auto& api = RenderApi(call);
	ValidateActive(api, token);
	String error;
	if (api.renderExecutor)
	{
		const auto execution = abort ? api.renderExecutor->Abort() : api.renderExecutor->End();
		if (!execution)
		{
			error = execution.error;
			if (api.renderExecutor->IsActive())
			{
				const auto cleanup = api.renderExecutor->Abort();
				if (!cleanup)
				{
					error += "; cleanup: " + cleanup.error;
				}
			}
		}
	}
	const auto ended = api.renderSceneContext.End();
	api.activeRenderToken.reset();
	api.activeRenderCamera.clear();
	api.renderBatches.clear();
	api.renderBatchResources.clear();
	if (!error.empty())
	{
		return Result<void>::Failure(error);
	}
	return ended;
}

Result<void> RenderingServices::Submit(BindingCall& call, const Token& token)
{
	auto& api = RenderApi(call);
	ValidateActive(api, token);
	return api.renderExecutor ? api.renderExecutor->Submit() : Result<void>{};
}

Result<DataView> RenderingServices::Cameras(BindingCall& call, const Token& token)
{
	auto& api = RenderApi(call);
	ValidateActive(api, token);
	return Result<DataView>::Success(WriteArray<CameraFrame>(
	    call.arena, api.renderScene.Cameras(), [&](const auto& value) { return WriteCamera(call, api, value); }));
}

Result<RenderingServices::CameraFrame> RenderingServices::SetupCamera(BindingCall& call, const Token& token,
                                                                      const Utf8View& identity)
{
	auto& api = RenderApi(call);
	ValidateActive(api, token);
	const auto& camera = FindCamera(api, identity);
	if (api.renderExecutor)
	{
		CompleteBindingResult(api.renderExecutor->ResetCamera(camera));
	}
	api.activeRenderCamera = camera.identity.ToString();
	return Result<CameraFrame>::Success(WriteCamera(call, api, camera));
}

Result<RenderingServices::RenderResult> RenderingServices::Cull(BindingCall& call, const Token& token,
                                                                const Utf8View& identity)
{
	auto& api = RenderApi(call);
	ValidateActive(api, token);
	const auto& camera = FindCamera(api, identity);
	if (!api.activeRenderCamera.empty() && api.activeRenderCamera != camera.identity.ToString())
	{
		return Result<RenderResult>::Failure("Cull camera differs from the active camera properties");
	}
	const auto value = TakeBindingResult(api.renderSceneContext.Cull(camera));
	return Result<RenderResult>::Success({WriteToken(value.context), value.index, value.generation});
}

Result<RenderingServices::RenderResult> RenderingServices::CullShadowCasters(BindingCall& call, const Token& token,
                                                                             const Utf8View& identity,
                                                                             const Matrix& lightViewProjection)
{
	auto& api = RenderApi(call);
	ValidateActive(api, token);
	const auto& receiverCamera = FindCamera(api, identity);
	if (!api.activeRenderCamera.empty() && api.activeRenderCamera != receiverCamera.identity.ToString())
	{
		return Result<RenderResult>::Failure("Shadow caster culling camera differs from active camera properties");
	}
	const auto culled =
	    TakeBindingResult(api.renderSceneContext.CullShadowCasters(receiverCamera, ReadMatrix(lightViewProjection)));
	return Result<RenderResult>::Success({WriteToken(culled.context), culled.index, culled.generation});
}

Result<RenderingServices::VisibleLightsValue> RenderingServices::VisibleLights(BindingCall& call, const Token& token,
                                                                               const RenderResult& culling)
{
	auto& api = RenderApi(call);
	ValidateActive(api, token);
	Ursa::Rendering::CullingResults nativeCulling{ReadToken(culling.F_Context), culling.F_Index, culling.F_Generation};
	const auto lights = TakeBindingResult(api.renderSceneContext.VisibleLightsFor(nativeCulling));
	VisibleLightsValue result;
	result.F_Directional = WriteArray<Wire::Value_Ursa_Rendering_DirectionalLightValue>(
	    call.arena, lights.directional,
	    [&](const auto& light)
	    {
		    Wire::Value_Ursa_Rendering_DirectionalLightValue value;
		    value.F_Identity = call.arena.StringValue(light.identity.ToString());
		    value.F_Layer = light.layer;
		    value.F_Direction = WriteVector3<Wire::Value_Ursa_Vector3>(light.direction);
		    value.F_Color = WriteColor<Wire::Value_Ursa_Color>(light.color);
		    value.F_Intensity = light.intensity;
		    value.F_CastsShadows = light.castsShadows;
		    value.F_ShadowStrength = light.shadowStrength;
		    value.F_ShadowBias = light.shadowBias;
		    value.F_ShadowNormalBias = light.shadowNormalBias;
		    value.F_SoftShadows = light.softShadows;
		    return value;
	    });
	result.F_Point = WriteArray<Wire::Value_Ursa_Rendering_PointLightValue>(
	    call.arena, lights.point,
	    [&](const auto& light)
	    {
		    Wire::Value_Ursa_Rendering_PointLightValue value;
		    value.F_Identity = call.arena.StringValue(light.identity.ToString());
		    value.F_Layer = light.layer;
		    value.F_Position = WriteVector3<Wire::Value_Ursa_Vector3>(light.position);
		    value.F_Color = WriteColor<Wire::Value_Ursa_Color>(light.color);
		    value.F_Intensity = light.intensity;
		    value.F_Range = light.range;
		    return value;
	    });
	return Result<VisibleLightsValue>::Success(std::move(result));
}

Result<DataView> RenderingServices::DirectionalShadowCascades(
    BindingCall& call, const Token& token, const Utf8View& identity, const Wire::Value_Ursa_Vector3& lightDirection,
    UInt32 cascadeCount, Float32 shadowDistance, const DataView& splitFractions, UInt32 atlasWidth, UInt32 atlasHeight)
{
	auto& api = RenderApi(call);
	ValidateActive(api, token);
	Ursa::Rendering::DirectionalShadowCascadeSettings settings;
	settings.cascadeCount = cascadeCount;
	settings.shadowDistance = shadowDistance;
	settings.splitFractions = ReadArray<Float32, Float32>(splitFractions, [](Float32 value) { return value; });
	settings.atlasWidth = atlasWidth;
	settings.atlasHeight = atlasHeight;
	const auto cascades = TakeBindingResult(Ursa::Rendering::ComputeDirectionalShadowCascades(
	    FindCamera(api, identity), ReadVector3(lightDirection), settings));
	return Result<DataView>::Success(WriteArray<ShadowCascade>(
	    call.arena, cascades,
	    [&](const auto& cascade)
	    {
		    ShadowCascade value;
		    value.F_Index = cascade.index;
		    value.F_NearDepth = cascade.nearDepth;
		    value.F_FarDepth = cascade.farDepth;
		    value.F_WorldUnitsPerTexel = cascade.worldUnitsPerTexel;
		    value.F_AtlasX = cascade.atlasX;
		    value.F_AtlasY = cascade.atlasY;
		    value.F_Resolution = cascade.resolution;
		    value.F_LightView = WriteMatrix<Wire::Value_Ursa_Matrix4x4>(cascade.lightView);
		    value.F_LightProjection = WriteMatrix<Wire::Value_Ursa_Matrix4x4>(cascade.lightProjection);
		    value.F_WorldToShadow = WriteMatrix<Wire::Value_Ursa_Matrix4x4>(cascade.worldToShadow);
		    return value;
	    }));
}

Result<RenderingServices::RenderResult> RenderingServices::CreateRendererList(BindingCall& call, const Token& token,
                                                                              const RenderResult& culling,
                                                                              const Drawing& drawing,
                                                                              const Filtering& filtering)
{
	auto& api = RenderApi(call);
	ValidateActive(api, token);
	if (drawing.F_Sorting > UInt32(Ursa::Rendering::SortingCriteria::CommonTransparent))
	{
		return Result<RenderResult>::Failure("RendererList sorting criteria is invalid");
	}
	Ursa::Rendering::DrawingSettings nativeDrawing;
	nativeDrawing.shaderTags = ReadArray<Utf8View, Ursa::Rendering::ShaderTagId>(
	    drawing.F_ShaderTags, [](const auto& tag) { return Ursa::Rendering::ShaderTagId{ReadString(tag)}; });
	nativeDrawing.sorting.criteria = Ursa::Rendering::SortingCriteria(drawing.F_Sorting);
	if (drawing.F_CameraPosition.present)
	{
		nativeDrawing.sorting.cameraPosition = ReadVector3(drawing.F_CameraPosition.F_value);
	}
	else if (!api.activeRenderCamera.empty())
	{
		const auto& cameras = api.renderScene.Cameras();
		const auto camera = std::find_if(cameras.begin(), cameras.end(), [&](const auto& value)
		                                 { return value.identity.ToString() == api.activeRenderCamera; });
		if (camera != cameras.end())
		{
			nativeDrawing.sorting.cameraPosition = camera->position;
		}
	}
	nativeDrawing.activePipeline = ReadString(drawing.F_ActivePipeline);
	nativeDrawing.overrideMaterialPass = drawing.F_OverrideMaterialPass;
	if (drawing.F_OverrideMaterial.present)
	{
		if (!drawing.F_OverrideMaterial.F_value.F_sessionId)
		{
			return Result<RenderResult>::Failure("A non-null override Material requires a valid handle");
		}
		nativeDrawing.overrideMaterial =
		    TakeBindingResult(api.ResolveMaterial(ReadManagedHandle(drawing.F_OverrideMaterial.F_value)));
	}
	const auto list = TakeBindingResult(api.renderSceneContext.CreateRendererList(
	    {ReadToken(culling.F_Context), culling.F_Index, culling.F_Generation}, nativeDrawing,
	    {filtering.F_MinimumQueue, filtering.F_MaximumQueue, filtering.F_LayerMask}));
	return Result<RenderResult>::Success({WriteToken(list.context), list.index, list.generation});
}

Result<void> RenderingServices::EnqueueBatch(BindingCall& call, const Token& token, const DataView& batch,
                                             const Utf8View& name)
{
	auto& api = RenderApi(call);
	ValidateActive(api, token);
	const auto result = api.ExecuteRenderBatch(static_cast<const UInt8*>(batch.data), Size(batch.length), name);
	if (result)
	{
		++api.acceptedRenderBatchCount;
	}
	return result;
}

Result<RenderingServices::Handle> RenderingServices::CreateRenderTexture(BindingCall& call,
                                                                         const TextureDescription& descriptor)
{
	auto& api = RenderApi(call);
	if (!api.device)
	{
		return Result<Handle>::Failure("RenderTexture creation requires an active RHI device");
	}
	auto texture = TakeBindingResult(Ursa::Rendering::CreateRenderTexture(
	    *api.device, {descriptor.F_Width, descriptor.F_Height, descriptor.F_ColorFormat, descriptor.F_DepthBufferBits,
	                  descriptor.F_EnableRandomWrite != 0, descriptor.F_MsaaSamples}));
	const auto handle = TakeBindingResult(api.handles->Register(texture.get(), ManagedHandleKind::RenderTexture));
	api.ownedRenderTextures[handle.index] = std::move(texture);
	return Result<Handle>::Success(WriteManagedHandle<Handle>(handle));
}

Result<void> RenderingServices::ReleaseRenderTexture(BindingCall& call, const Handle& texture)
{
	auto& api = RenderApi(call);
	TakeBindingResult(api.ResolveRenderTexture(ReadManagedHandle(texture)));
	CompleteBindingResult(api.handles->Invalidate(ReadManagedHandle(texture)));
	api.ownedRenderTextures.erase(texture.F_index);
	return {};
}

Result<DataView> RenderingServices::ReadRenderTexture(BindingCall& call, const Handle& texture)
{
	auto& api = RenderApi(call);
	if (api.activeRenderToken || !api.device)
	{
		return Result<DataView>::Failure("RenderTexture readback requires a device and a finished render context");
	}
	const auto bytes = TakeBindingResult(Ursa::Rendering::ReadRenderTexture(
	    *api.device, *TakeBindingResult(api.ResolveRenderTexture(ReadManagedHandle(texture)))));
	return Result<DataView>::Success(WriteArray<UInt8>(call.arena, bytes, [](UInt8 value) { return value; }));
}

Result<RenderingServices::Handle> RenderingServices::CreateGraphicsBuffer(BindingCall& call, UInt32 target,
                                                                          UInt32 count, UInt32 stride)
{
	auto& api = RenderApi(call);
	if (!api.device)
	{
		return Result<Handle>::Failure("GraphicsBuffer creation requires an active RHI device");
	}
	auto buffer = TakeBindingResult(Ursa::Rendering::CreateGraphicsBuffer(*api.device, target, count, stride));
	const auto handle = TakeBindingResult(api.handles->Register(buffer.get(), ManagedHandleKind::GraphicsBuffer));
	api.ownedGraphicsBuffers[handle.index] = std::move(buffer);
	return Result<Handle>::Success(WriteManagedHandle<Handle>(handle));
}

Result<void> RenderingServices::ReleaseGraphicsBuffer(BindingCall& call, const Handle& buffer)
{
	auto& api = RenderApi(call);
	TakeBindingResult(api.ResolveGraphicsBuffer(ReadManagedHandle(buffer)));
	CompleteBindingResult(api.handles->Invalidate(ReadManagedHandle(buffer)));
	api.ownedGraphicsBuffers.erase(buffer.F_index);
	return {};
}

Result<DataView> RenderingServices::ReadGraphicsBuffer(BindingCall& call, const Handle& buffer, UInt64 offset,
                                                       UInt64 size)
{
	auto& api = RenderApi(call);
	if (api.activeRenderToken || !api.device || offset > std::numeric_limits<Size>::max() ||
	    size > std::numeric_limits<Size>::max())
	{
		return Result<DataView>::Failure(
		    "GraphicsBuffer readback requires a device, a finished frame and an addressable range");
	}
	const auto bytes = TakeBindingResult(Ursa::Rendering::ReadGraphicsBuffer(
	    *api.device, *TakeBindingResult(api.ResolveGraphicsBuffer(ReadManagedHandle(buffer))), Size(offset),
	    Size(size)));
	return Result<DataView>::Success(WriteArray<UInt8>(call.arena, bytes, [](UInt8 value) { return value; }));
}
} // namespace Ursa::Scripting::Bindings
