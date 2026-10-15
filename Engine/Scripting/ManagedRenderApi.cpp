#include <Scripting/ManagedSceneApi.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceSession.hpp>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace Ursa::Scripting
{
namespace
{
template <class T> T Read(const UInt8* bytes, Size offset = 0)
{
	T value{};
	std::memcpy(&value, bytes + offset, sizeof(value));
	return value;
}

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}
} // namespace

Result<void> ManagedSceneApi::ExecuteRenderBatch(const UInt8* bytes, Size byteCount)
{
	try
	{
		auto valid = Rendering::ValidateRenderCommandBatch(bytes, byteCount);
		if (!valid)
		{
			return valid;
		}
		const auto batch = Read<Rendering::RenderCommandBatchHeader>(bytes);
		if (!batch.commandCount)
		{
			return {};
		}
		if (!renderExecutor)
		{
			return Result<void>::Failure("Render command execution requires a render device");
		}
		const auto targetReference = [&](const UInt8* payload) -> Rendering::RenderTargetReference
		{
			const UInt32 kind = Read<UInt32>(payload);
			const UInt32 id = Read<UInt32>(payload, 4);
			if (kind == 0)
			{
				const auto handle = Read<ManagedHandle>(payload, 8);
				if (handle.sessionId || handle.index || handle.generation || UInt32(handle.kind) || handle.reserved)
				{
					throw std::runtime_error("Temporary target identifier contains a resource handle");
				}
				return {{}, id};
			}
			if (kind != 1 || id)
			{
				throw std::runtime_error("Render target identifier kind or reserved ID is invalid");
			}
			return {Take(ResolveRenderTexture(Read<ManagedHandle>(payload, 8))), 0};
		};
		Vector<Rendering::RenderCommand> operations;
		const auto computeReference = [&](const UInt8* payload)
		{
			const auto guid = Read<ResourceId>(payload);
			if (!resources || !guid || Read<UInt64>(payload, 16) != 0)
			{
				throw std::runtime_error("Compute command requires an available root Shader asset reference");
			}
			return Take(resources->LoadShader(guid));
		};
		Size offset = sizeof(batch);
		for (UInt32 index = 0; index < batch.commandCount; ++index)
		{
			const auto record = Read<Rendering::RenderCommandRecordHeader>(bytes, offset);
			const auto* payload = bytes + offset + sizeof(record);
			Rendering::RenderCommand command;
			command.opcode = record.opcode;
			switch (record.opcode)
			{
				case Rendering::RenderCommandOpcode::DrawRendererList:
				case Rendering::RenderCommandOpcode::DrawMesh:
				{
					const auto camera =
					    std::find_if(renderScene.Cameras().begin(), renderScene.Cameras().end(), [&](const auto& value)
					                 { return value.identity.ToString() == activeRenderCamera; });
					if (camera == renderScene.Cameras().end())
					{
						throw std::runtime_error("Scene drawing requires current camera properties");
					}
					command.camera = *camera;
					command.lighting = renderScene.Lighting();
					if (record.opcode == Rendering::RenderCommandOpcode::DrawRendererList)
					{
						const auto list = Read<Rendering::RendererList>(payload);
						const auto* entries = Take(renderSceneContext.Entries(list));
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
					else
					{
						Rendering::SceneDrawRecord draw;
						const auto mesh = Take(ResolveMesh(Read<ManagedHandle>(payload)));
						const auto material = Take(ResolveMaterial(Read<ManagedHandle>(payload, 24)));
						draw.object.mesh = mesh;
						draw.object.meshSnapshot = mesh->Snapshot();
						draw.object.material = material;
						draw.object.shader = material->Shader();
						draw.object.parameters = material->Parameters();
						std::memcpy(draw.object.world.values.data(), payload + 48, 64);
						draw.object.subMesh = Read<UInt32>(payload, 112);
						const Int32 pass = Read<Int32>(payload, 116);
						if (pass < -1)
						{
							throw std::runtime_error("DrawMesh Pass is invalid");
						}
						draw.selectedPass = pass >= 0;
						draw.passIndex = pass < 0 ? 0u : UInt32(pass);
						if (pass >= 0 && material->Shader())
						{
							draw.resolvedPass =
							    Take(Rendering::SelectGraphicsPass(material->Shader(), draw.activePipeline, {}, pass));
						}
						command.draws.push_back(std::move(draw));
					}
					break;
				}
				case Rendering::RenderCommandOpcode::SetRenderTarget:
					if (record.byteSize == 16)
					{
						command.target.id = Read<UInt32>(payload);
					}
					else if (record.byteSize == 32)
					{
						command.target.texture = Take(ResolveRenderTexture(Read<ManagedHandle>(payload)));
					}
					else
					{
						command.target = targetReference(payload);
					}
					break;
				case Rendering::RenderCommandOpcode::SetRenderTargets:
				{
					const UInt32 count = Read<UInt32>(payload);
					command.hasDepthTarget = Read<UInt32>(payload, 4) != 0;
					for (UInt32 attachment = 0; attachment < count; ++attachment)
					{
						command.colors.push_back(targetReference(payload + 8 + attachment * 32));
					}
					if (command.hasDepthTarget)
					{
						command.depthTarget = targetReference(payload + 8 + count * 32);
					}
					break;
				}
				case Rendering::RenderCommandOpcode::ClearTarget:
					command.clearFlags = Read<UInt32>(payload);
					command.color = Read<Color>(payload, 4);
					command.clearDepth = Read<Float32>(payload, 20);
					command.clearStencil = Read<UInt32>(payload, 24);
					break;
				case Rendering::RenderCommandOpcode::SetViewport:
					command.viewport = Read<Rhi::ViewportRect>(payload);
					break;
				case Rendering::RenderCommandOpcode::SetScissor:
					command.scissor = Read<Rhi::ScissorRect>(payload);
					break;
				case Rendering::RenderCommandOpcode::CopyTexture:
				case Rendering::RenderCommandOpcode::Blit:
				{
					const bool extended = record.opcode == Rendering::RenderCommandOpcode::CopyTexture
					                          ? record.byteSize == 72
					                          : record.byteSize == 104;
					if (extended)
					{
						command.source = targetReference(payload);
						command.target = targetReference(payload + 32);
					}
					else
					{
						command.source.texture = Take(ResolveRenderTexture(Read<ManagedHandle>(payload)));
						command.target.texture = Take(ResolveRenderTexture(Read<ManagedHandle>(payload, 24)));
					}
					if (record.opcode == Rendering::RenderCommandOpcode::Blit)
					{
						command.material = Take(ResolveMaterial(Read<ManagedHandle>(payload, extended ? 64 : 48)));
						command.pass = Read<Int32>(payload, extended ? 88 : 72);
					}
					break;
				}
				case Rendering::RenderCommandOpcode::DrawFullscreen:
					command.material = Take(ResolveMaterial(Read<ManagedHandle>(payload)));
					command.pass = Read<Int32>(payload, 24);
					break;
				case Rendering::RenderCommandOpcode::CopyBuffer:
					command.sourceBuffer = Take(ResolveGraphicsBuffer(Read<ManagedHandle>(payload)));
					command.targetBuffer = Take(ResolveGraphicsBuffer(Read<ManagedHandle>(payload, 24)));
					command.byteSize = Read<UInt64>(payload, 48);
					command.sourceOffset = Read<UInt64>(payload, 56);
					command.targetOffset = Read<UInt64>(payload, 64);
					break;
				case Rendering::RenderCommandOpcode::SetComputeParameters:
					command.shader = computeReference(payload);
					command.kernel = Read<UInt32>(payload, 24);
					payload += 28;
					[[fallthrough]];
				case Rendering::RenderCommandOpcode::SetGlobalParameters:
				{
					const UInt32 kind = Read<UInt32>(payload);
					const UInt32 nameBytes = Read<UInt32>(payload, 4);
					command.global.kind = kind;
					command.global.name = String(reinterpret_cast<const char*>(payload + 8), nameBytes);
					const auto* value = payload + 8 + nameBytes;
					if (kind == 3)
					{
						command.global.texture.texture = Take(ResolveRenderTexture(Read<ManagedHandle>(value)));
					}
					else if (kind == 4)
					{
						command.global.texture = targetReference(value);
					}
					else if (kind == 7)
					{
						command.global.buffer = Take(ResolveGraphicsBuffer(Read<ManagedHandle>(value)));
					}
					else
					{
						const UInt32 valueBytes = kind == 2 ? 16u : kind == 5 ? 64u : 4u;
						command.global.bytes.assign(value, value + valueBytes);
					}
					break;
				}
				case Rendering::RenderCommandOpcode::DispatchCompute:
					command.shader = computeReference(payload);
					command.kernel = Read<UInt32>(payload, 24);
					command.groups = {Read<UInt32>(payload, 28), Read<UInt32>(payload, 32), Read<UInt32>(payload, 36)};
					break;
				case Rendering::RenderCommandOpcode::GetTemporaryTarget:
					command.target.id = Read<UInt32>(payload);
					command.description = {Read<UInt32>(payload, 4),       Read<UInt32>(payload, 8),
					                       Read<UInt32>(payload, 12),      Read<UInt32>(payload, 16),
					                       Read<UInt32>(payload, 20) != 0, Read<UInt32>(payload, 24)};
					if (Read<UInt32>(payload, 20) > 1)
					{
						throw std::runtime_error("Temporary target storage usage flag is invalid");
					}
					break;
				case Rendering::RenderCommandOpcode::ReleaseTemporaryTarget:
					command.target.id = Read<UInt32>(payload);
					break;
				default:
					return Result<void>::Failure("Render command requires an installed scene/compute encoder");
			}
			operations.push_back(std::move(command));
			offset += record.byteSize;
		}
		return renderExecutor->Execute(operations);
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}
} // namespace Ursa::Scripting
