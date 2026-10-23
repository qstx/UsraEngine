#pragma once

#include <BindingTypes.generated.hpp>
#include <Rendering/Scene/CameraData.hpp>

namespace Ursa::Scripting
{
class ManagedSceneApi;
}

namespace Ursa::Scripting::Bindings
{
struct RenderingServices
{
	using Token = Wire::Value_Ursa_Rendering_RenderContextToken;
	using RenderResult = Wire::Value_Ursa_Rendering_RenderResultValue;
	using CameraFrame = Wire::Value_Ursa_Rendering_CameraFrameValue;
	using VisibleLightsValue = Wire::Value_Ursa_Rendering_VisibleLightsValue;
	using ShadowCascade = Wire::Value_Ursa_Rendering_DirectionalShadowCascadeValue;
	using Matrix = Wire::Value_Ursa_Matrix4x4;
	using Drawing = Wire::Value_Ursa_Rendering_DrawingSettingsValue;
	using Filtering = Wire::Value_Ursa_Rendering_FilteringSettings;
	using TextureDescription = Wire::Value_Ursa_Rendering_RenderTextureValue;
	using Handle = Wire::Value_Ursa_NativeHandle;
	static void ValidateActive(ManagedSceneApi& api, const Token& token);
	static CameraFrame WriteCamera(BindingCall& call, ManagedSceneApi& api, const Ursa::Rendering::CameraData& camera);
	static const Ursa::Rendering::CameraData& FindCamera(ManagedSceneApi& api, const Utf8View& identity);
	static Result<Token> Begin(BindingCall& call, UInt64 frameId, Float32 aspect);
	static Result<void> End(BindingCall& call, const Token& token);
	static Result<void> Abort(BindingCall& call, const Token& token);
	static Result<void> Finish(BindingCall& call, const Token& token, bool abort);
	static Result<void> Submit(BindingCall& call, const Token& token);
	static Result<DataView> Cameras(BindingCall& call, const Token& token);
	static Result<CameraFrame> SetupCamera(BindingCall& call, const Token& token, const Utf8View& identity);
	static Result<RenderResult> Cull(BindingCall& call, const Token& token, const Utf8View& identity);
	static Result<RenderResult> CullShadowCasters(BindingCall& call, const Token& token, const Utf8View& identity,
	                                              const Matrix& lightViewProjection);
	static Result<VisibleLightsValue> VisibleLights(BindingCall& call, const Token& token, const RenderResult& culling);
	static Result<DataView> DirectionalShadowCascades(BindingCall& call, const Token& token, const Utf8View& identity,
	                                                  const Wire::Value_Ursa_Vector3& lightDirection,
	                                                  UInt32 cascadeCount, Float32 shadowDistance,
	                                                  const DataView& splitFractions, UInt32 atlasWidth,
	                                                  UInt32 atlasHeight);
	static Result<RenderResult> CreateRendererList(BindingCall& call, const Token& token, const RenderResult& culling,
	                                               const Drawing& drawing, const Filtering& filtering);
	static Result<void> EnqueueBatch(BindingCall& call, const Token& token, const DataView& batch,
	                                 const Utf8View& name);
	static Result<Handle> CreateRenderTexture(BindingCall& call, const TextureDescription& descriptor);
	static Result<void> ReleaseRenderTexture(BindingCall& call, const Handle& texture);
	static Result<DataView> ReadRenderTexture(BindingCall& call, const Handle& texture);
	static Result<Handle> CreateGraphicsBuffer(BindingCall& call, UInt32 target, UInt32 count, UInt32 stride);
	static Result<void> ReleaseGraphicsBuffer(BindingCall& call, const Handle& buffer);
	static Result<DataView> ReadGraphicsBuffer(BindingCall& call, const Handle& buffer, UInt64 offset, UInt64 size);
};
} // namespace Ursa::Scripting::Bindings
