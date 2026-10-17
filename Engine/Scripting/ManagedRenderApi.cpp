#include <Rendering.Commands.generated.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/Bindings/BindingAssets.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/Bindings/BindingMath.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>
#include <cstring>
#include <type_traits>

namespace Ursa::Scripting
{
Result<void> ManagedSceneApi::ExecuteRenderBatch(const UInt8* bytes, Size byteCount)
{
	namespace B = Bindings;
	namespace C = Bindings::Rendering::Commands;
	try
	{
		B::BindingArena arena;
		const auto decoded = C::Decode(bytes, byteCount, arena);
		if (decoded.empty())
		{
			return {};
		}
		if (!renderExecutor)
		{
			return Result<void>::Failure("Render command execution requires a render device");
		}
		const auto target =
		    [&](const B::Wire::Value_Ursa_Rendering_RenderTargetValue& value) -> Rendering::RenderTargetReference
		{
			if (value.F_Kind == 0 && !value.F_Texture.present)
			{
				return {{}, value.F_Id};
			}
			if (value.F_Kind != 1 || value.F_Id || !value.F_Texture.present || !value.F_Texture.F_value.F_sessionId)
			{
				throw std::runtime_error("Render target kind, identifier or resource handle is invalid");
			}
			return {B::TakeBindingResult(ResolveRenderTexture(B::ReadManagedHandle(value.F_Texture.F_value))), 0};
		};
		const auto compute = [&](const B::Wire::Value_Ursa_NativeAssetReference& value, Int32 kernel)
		{
			const auto asset = B::ReadAssetValue(value);
			if (!resources || asset.localFileId || kernel < 0)
			{
				throw std::runtime_error("Compute command requires a root Shader asset and nonnegative kernel");
			}
			return B::TakeBindingResult(resources->LoadShader(asset.guid));
		};
		const auto setBytes = [](Rendering::RenderCommand& command, UInt32 kind, const auto& value)
		{
			command.global.kind = kind;
			command.global.bytes.resize(sizeof(value));
			std::memcpy(command.global.bytes.data(), &value, sizeof(value));
		};
		const auto camera = [&](Rendering::RenderCommand& command)
		{
			const auto found =
			    std::find_if(renderScene.Cameras().begin(), renderScene.Cameras().end(),
			                 [&](const auto& value) { return value.identity.ToString() == activeRenderCamera; });
			if (found == renderScene.Cameras().end())
			{
				throw std::runtime_error("Scene drawing requires current camera properties");
			}
			command.camera = *found;
			command.lighting = renderScene.Lighting();
		};
		Vector<Rendering::RenderCommand> operations;
		operations.reserve(decoded.size());
		for (const auto& record : decoded)
		{
			Rendering::RenderCommand command;
			std::visit(
			    [&](const auto& value)
			    {
				    using T = std::decay_t<decltype(value)>;
				    using Opcode = Rendering::RenderCommandOpcode;
				    if constexpr (std::is_same_v<T, C::RenderCommands_SetRenderTarget>)
				    {
					    command.opcode = Opcode::SetRenderTarget;
					    command.target = target(value.F_target);
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_SetRenderTargets>)
				    {
					    command.opcode = Opcode::SetRenderTargets;
					    if (value.F_colors.length > 8)
					    {
						    throw std::runtime_error("A render target set supports at most eight colors");
					    }
					    const auto* colors =
					        static_cast<const B::Wire::Value_Ursa_Rendering_RenderTargetValue*>(value.F_colors.data);
					    for (UInt64 index = 0; index < value.F_colors.length; ++index)
					    {
						    command.colors.push_back(target(colors[index]));
					    }
					    command.hasDepthTarget = value.F_depth.present != 0;
					    if (command.hasDepthTarget)
					    {
						    command.depthTarget = target(value.F_depth.F_value);
					    }
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_ClearTarget>)
				    {
					    command.opcode = Opcode::ClearTarget;
					    command.clearFlags = value.F_flags;
					    command.color = B::ReadColor(value.F_color);
					    command.clearDepth = value.F_depth;
					    command.clearStencil = value.F_stencil;
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_SetViewport>)
				    {
					    command.opcode = Opcode::SetViewport;
					    command.viewport = {
					        value.F_x,           value.F_y, value.F_width, value.F_height, value.F_minimumDepth,
					        value.F_maximumDepth};
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_SetScissor>)
				    {
					    command.opcode = Opcode::SetScissor;
					    command.scissor = {value.F_x, value.F_y, value.F_width, value.F_height};
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_DrawRendererList>)
				    {
					    command.opcode = Opcode::DrawRendererList;
					    camera(command);
					    const auto& token = value.F_list.F_Context;
					    const Rendering::RendererList list{
					        {token.F_sessionId, token.F_frameId, token.F_index, token.F_generation},
					        value.F_list.F_Index,
					        value.F_list.F_Generation};
					    const auto* entries = B::TakeBindingResult(renderSceneContext.Entries(list));
					    for (const auto& entry : *entries)
					    {
						    Rendering::SceneDrawRecord draw;
						    draw.object = renderScene.Objects()[entry.objectIndex];
						    draw.object.material = entry.material;
						    draw.subShaderIndex = entry.subShaderIndex;
						    draw.passIndex = entry.passIndex;
						    draw.selectedPass = true;
						    draw.activePipeline = entry.activePipeline;
						    draw.resolvedPass = entry.resolvedPass;
						    command.draws.push_back(std::move(draw));
					    }
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_DrawMesh>)
				    {
					    command.opcode = Opcode::DrawMesh;
					    camera(command);
					    Rendering::SceneDrawRecord draw;
					    const auto mesh = B::TakeBindingResult(ResolveMesh(B::ReadManagedHandle(value.F_mesh)));
					    const auto material =
					        B::TakeBindingResult(ResolveMaterial(B::ReadManagedHandle(value.F_material)));
					    draw.object.mesh = mesh;
					    draw.object.meshSnapshot = mesh->Snapshot();
					    draw.object.material = material;
					    draw.object.shader = material->Shader();
					    draw.object.parameters = material->Parameters();
					    draw.object.world = B::ReadMatrix(value.F_localToWorld);
					    draw.object.subMesh = value.F_subMesh;
					    if (value.F_pass < -1)
					    {
						    throw std::runtime_error("DrawMesh Pass is invalid");
					    }
					    draw.selectedPass = value.F_pass >= 0;
					    draw.passIndex = value.F_pass < 0 ? 0u : UInt32(value.F_pass);
					    if (draw.selectedPass && material->Shader())
					    {
						    draw.resolvedPass = B::TakeBindingResult(Rendering::SelectGraphicsPass(
						        material->Shader(), draw.activePipeline, {}, value.F_pass));
					    }
					    command.draws.push_back(std::move(draw));
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_CopyTexture> ||
				                       std::is_same_v<T, C::RenderCommands_Blit>)
				    {
					    command.source = target(value.F_source);
					    command.target = target(value.F_destination);
					    if constexpr (std::is_same_v<T, C::RenderCommands_Blit>)
					    {
						    command.opcode = Opcode::Blit;
						    command.material =
						        B::TakeBindingResult(ResolveMaterial(B::ReadManagedHandle(value.F_material)));
						    command.pass = value.F_pass;
					    }
					    else
					    {
						    command.opcode = Opcode::CopyTexture;
					    }
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_DrawFullscreen>)
				    {
					    command.opcode = Opcode::DrawFullscreen;
					    command.material =
					        B::TakeBindingResult(ResolveMaterial(B::ReadManagedHandle(value.F_material)));
					    command.pass = value.F_pass;
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_CopyBuffer>)
				    {
					    command.opcode = Opcode::CopyBuffer;
					    command.sourceBuffer =
					        B::TakeBindingResult(ResolveGraphicsBuffer(B::ReadManagedHandle(value.F_source)));
					    command.targetBuffer =
					        B::TakeBindingResult(ResolveGraphicsBuffer(B::ReadManagedHandle(value.F_destination)));
					    command.byteSize = value.F_size;
					    command.sourceOffset = value.F_sourceOffset;
					    command.targetOffset = value.F_destinationOffset;
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_GetTemporaryTarget>)
				    {
					    command.opcode = Opcode::GetTemporaryTarget;
					    command.target.id = value.F_id;
					    const auto& description = value.F_descriptor;
					    command.description = {description.F_Width,
					                           description.F_Height,
					                           description.F_ColorFormat,
					                           description.F_DepthBufferBits,
					                           description.F_EnableRandomWrite != 0,
					                           description.F_MsaaSamples};
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_ReleaseTemporaryTarget>)
				    {
					    command.opcode = Opcode::ReleaseTemporaryTarget;
					    command.target.id = value.F_id;
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_DispatchCompute>)
				    {
					    command.opcode = Opcode::DispatchCompute;
					    command.shader = compute(value.F_shader, value.F_kernel);
					    command.kernel = UInt32(value.F_kernel);
					    command.groups = {value.F_groupsX, value.F_groupsY, value.F_groupsZ};
				    }
				    else if constexpr (std::is_same_v<T, C::RenderCommands_ResetCameraState>)
				    {
					    throw std::runtime_error("Camera reset is reserved for native execution");
				    }
				    else
				    {
					    constexpr bool isCompute = std::is_same_v<T, C::RenderCommands_SetComputeFloat> ||
					                               std::is_same_v<T, C::RenderCommands_SetComputeInt> ||
					                               std::is_same_v<T, C::RenderCommands_SetComputeVector> ||
					                               std::is_same_v<T, C::RenderCommands_SetComputeMatrix> ||
					                               std::is_same_v<T, C::RenderCommands_SetComputeTexture> ||
					                               std::is_same_v<T, C::RenderCommands_SetComputeBuffer>;
					    if constexpr (isCompute)
					    {
						    command.opcode = Opcode::SetComputeParameters;
						    command.shader = compute(value.F_shader, value.F_kernel);
						    command.kernel = UInt32(value.F_kernel);
					    }
					    else
					    {
						    command.opcode = Opcode::SetGlobalParameters;
					    }
					    command.global.name = B::ReadString(value.F_name);
					    if constexpr (std::is_same_v<T, C::RenderCommands_SetGlobalFloat> ||
					                  std::is_same_v<T, C::RenderCommands_SetComputeFloat>)
					    {
						    setBytes(command, 1, value.F_value);
					    }
					    else if constexpr (std::is_same_v<T, C::RenderCommands_SetGlobalInt> ||
					                       std::is_same_v<T, C::RenderCommands_SetComputeInt>)
					    {
						    setBytes(command, 6, value.F_value);
					    }
					    else if constexpr (std::is_same_v<T, C::RenderCommands_SetGlobalVector> ||
					                       std::is_same_v<T, C::RenderCommands_SetComputeVector>)
					    {
						    const auto& vector = value.F_value;
						    setBytes(command, 2, Array<Float32, 4>{vector.F_X, vector.F_Y, vector.F_Z, vector.F_W});
					    }
					    else if constexpr (std::is_same_v<T, C::RenderCommands_SetGlobalMatrix> ||
					                       std::is_same_v<T, C::RenderCommands_SetComputeMatrix>)
					    {
						    setBytes(command, 5, B::ReadMatrix(value.F_value).values);
					    }
					    else if constexpr (std::is_same_v<T, C::RenderCommands_SetGlobalTexture> ||
					                       std::is_same_v<T, C::RenderCommands_SetComputeTexture>)
					    {
						    command.global.kind = 4;
						    command.global.texture = target(value.F_value);
					    }
					    else if constexpr (std::is_same_v<T, C::RenderCommands_SetGlobalBuffer> ||
					                       std::is_same_v<T, C::RenderCommands_SetComputeBuffer>)
					    {
						    command.global.kind = 7;
						    command.global.buffer =
						        B::TakeBindingResult(ResolveGraphicsBuffer(B::ReadManagedHandle(value.F_value)));
					    }
				    }
			    },
			    record);
			operations.push_back(std::move(command));
		}
		return renderExecutor->Execute(operations);
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}
} // namespace Ursa::Scripting
