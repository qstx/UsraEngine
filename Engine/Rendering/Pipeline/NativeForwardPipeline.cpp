#include <Rendering/Pipeline/NativeForwardPipeline.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Core/StringInterop.hpp>
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace Ursa::Rendering
{
namespace
{
template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}
} // namespace

Result<std::unique_ptr<NativeForwardPipeline>> NativeForwardPipeline::Create(Rhi::DevicePtr device)
{
	try
	{
		const auto files = Take(OpenEngineFileSystem());
		const auto settings = Take(ReadGraphicsSettings(files, true));
		if (settings.renderPipeline.guid)
		{
			return Result<std::unique_ptr<NativeForwardPipeline>>::Failure(
			    "Project pipeline " + settings.renderPipeline.guid.ToString() + " requires a managed render host");
		}
		auto result = std::unique_ptr<NativeForwardPipeline>(new NativeForwardPipeline());
		result->device = device;
		result->executor = Take(RenderCommandExecutor::Create(std::move(device)));
		result->neutralShadowAtlas = Take(CreateRenderTexture(*result->device, {1, 1, 2}));
		return Result<std::unique_ptr<NativeForwardPipeline>>::Success(std::move(result));
	}
	catch (const std::exception& error)
	{
		return Result<std::unique_ptr<NativeForwardPipeline>>::Failure(error.what());
	}
}

Result<void> NativeForwardPipeline::Render(Scene& scene, const Rhi::SwapchainPtr& window, FrameCapture* capture)
{
	return RenderTarget(scene, {}, window, Rhi::ResourceState::Present, capture, {}, {});
}

Result<void> NativeForwardPipeline::RenderOffscreen(const Scene& scene,
                                                    const std::shared_ptr<RenderTextureResource>& output,
                                                    Rhi::ResourceState finalState, std::optional<CameraType> filter,
                                                    std::optional<CameraData> cameraOverride)
{
	if (!output || !output->color)
	{
		return Result<void>::Failure("Native Forward preview requires a color target");
	}
	return RenderTarget(scene, output, {}, finalState, nullptr, filter, std::move(cameraOverride));
}

Result<void> NativeForwardPipeline::RenderTarget(const Scene& scene,
                                                 const std::shared_ptr<RenderTextureResource>& output,
                                                 const Rhi::SwapchainPtr& window, Rhi::ResourceState finalState,
                                                 FrameCapture* capture, std::optional<CameraType> filter,
                                                 std::optional<CameraData> cameraOverride)
{
	try
	{
		executor->SetFrameCapture(capture);
		Done(executor->Begin(output, finalState, window));
		if (!finalOutputMaterial)
		{
			finalOutputMaterial = std::make_shared<MaterialInstance>(MaterialType::Unlit);
			Done(finalOutputMaterial->SetShaderAsset(
			    Take(executor->ResolveBuiltinShader(Shader::BuiltinFinalPostShaderId()))));
		}
		const auto target = executor->CameraTarget();
		if (!target || !target->color)
		{
			throw std::runtime_error("Native Forward requires an acquired color target");
		}
		const auto width = target->color->desc.width, height = target->color->desc.height;
		const String targetKey =
		    output ? "offscreen:" + ToUrsaString(std::to_string(output->identity)) + ":" +
		                 ToUrsaString(std::to_string(output->generation)) + ":" +
		                 ToUrsaString(std::to_string(UInt32(finalState)))
		           : "window:" + ToUrsaString(std::to_string(reinterpret_cast<Ursa::UIntPtr>(window.get()))) + ":" +
		                 ToUrsaString(std::to_string(window->Statistics().rebuilds + 1));
		if (!sceneBacking || sceneBackingKey != targetKey || sceneBacking->description.width != width ||
		    sceneBacking->description.height != height)
		{
			sceneBacking = Take(CreateRenderTexture(*device, {width, height, 1, 32}));
			sceneBackingKey = targetKey;
			sceneBackingInitialized = false;
		}
		bool backingInitializedThisFrame = sceneBackingInitialized;
		Done(snapshot.Capture(scene, Float32(width) / Float32(height), filter, std::move(cameraOverride),
		                      [this](ResourceId id) { return executor->ResolveBuiltinShader(id); }));
		Take(context.Begin(1, ++frameId, snapshot));
		for (const auto& camera : snapshot.Cameras())
		{
			Done(executor->ResetCamera(camera));
			commands.clear();
			const auto& rect = camera.settings.viewport;
			RenderCommand sceneTarget;
			sceneTarget.opcode = RenderCommandOpcode::SetRenderTarget;
			sceneTarget.target.texture = sceneBacking;
			commands.push_back(std::move(sceneTarget));
			RenderCommand neutralShadowTarget;
			neutralShadowTarget.opcode = RenderCommandOpcode::SetRenderTarget;
			neutralShadowTarget.target.texture = neutralShadowAtlas;
			commands.push_back(std::move(neutralShadowTarget));
			RenderCommand shadowViewport;
			shadowViewport.opcode = RenderCommandOpcode::SetViewport;
			shadowViewport.viewport = {0, 0, 1, 1, 0, 1};
			commands.push_back(shadowViewport);
			RenderCommand shadowScissor;
			shadowScissor.opcode = RenderCommandOpcode::SetScissor;
			shadowScissor.scissor = {0, 0, 1, 1};
			commands.push_back(shadowScissor);
			RenderCommand shadowClear;
			shadowClear.opcode = RenderCommandOpcode::ClearTarget;
			shadowClear.clearFlags = 2;
			commands.push_back(shadowClear);
			RenderCommand restoreSceneTarget;
			restoreSceneTarget.opcode = RenderCommandOpcode::SetRenderTarget;
			restoreSceneTarget.target.texture = sceneBacking;
			commands.push_back(std::move(restoreSceneTarget));
			RenderCommand shadowAtlasGlobal;
			shadowAtlasGlobal.opcode = RenderCommandOpcode::SetGlobalParameters;
			shadowAtlasGlobal.global.name = "_UrsaDirectionalShadowAtlas";
			shadowAtlasGlobal.global.kind = 3;
			shadowAtlasGlobal.global.texture.texture = neutralShadowAtlas;
			commands.push_back(std::move(shadowAtlasGlobal));
			RenderCommand shadowCountGlobal;
			shadowCountGlobal.opcode = RenderCommandOpcode::SetGlobalParameters;
			shadowCountGlobal.global.name = "_UrsaDirectionalShadowCascadeCount";
			shadowCountGlobal.global.kind = 6;
			shadowCountGlobal.global.bytes.resize(sizeof(Int32));
			const Int32 shadowCascadeCount = 0;
			std::memcpy(shadowCountGlobal.global.bytes.data(), &shadowCascadeCount, sizeof(shadowCascadeCount));
			commands.push_back(std::move(shadowCountGlobal));
			if (!backingInitializedThisFrame)
			{
				RenderCommand fullViewport;
				fullViewport.opcode = RenderCommandOpcode::SetViewport;
				fullViewport.viewport = {0, 0, Float32(width), Float32(height), 0, 1};
				commands.push_back(fullViewport);
				RenderCommand fullScissor;
				fullScissor.opcode = RenderCommandOpcode::SetScissor;
				fullScissor.scissor = {0, 0, width, height};
				commands.push_back(fullScissor);
				RenderCommand initialize;
				initialize.opcode = RenderCommandOpcode::ClearTarget;
				initialize.clearFlags = 3;
				initialize.color = {0, 0, 0, 1};
				commands.push_back(initialize);
			}
			RenderCommand viewport;
			viewport.opcode = RenderCommandOpcode::SetViewport;
			viewport.viewport = {rect.x * width, rect.y * height, rect.width * width, rect.height * height, 0, 1};
			commands.push_back(viewport);
			auto sample = [&](RenderCommandOpcode opcode, const char* name)
			{
				RenderCommand marker;
				marker.opcode = opcode;
				marker.debugName = name;
				commands.push_back(std::move(marker));
			};
			RenderCommand scissor;
			scissor.opcode = RenderCommandOpcode::SetScissor;
			scissor.scissor = {Int32(std::floor(rect.x * width)), Int32(std::floor(rect.y * height)),
			                   UInt32(std::ceil((rect.x + rect.width) * width)) - UInt32(std::floor(rect.x * width)),
			                   UInt32(std::ceil((rect.y + rect.height) * height)) -
			                       UInt32(std::floor(rect.y * height))};
			commands.push_back(scissor);
			if (camera.settings.clearFlags != CameraClearFlags::Nothing)
			{
				sample(RenderCommandOpcode::BeginSample, "Clear");
				RenderCommand clear;
				clear.opcode = RenderCommandOpcode::ClearTarget;
				clear.clearFlags = camera.settings.clearFlags == CameraClearFlags::ColorAndDepth ? 3 : 2;
				clear.color = camera.settings.background;
				commands.push_back(clear);
				sample(RenderCommandOpcode::EndSample, "Clear");
			}
			const auto culled = Take(context.Cull(camera));
			for (const bool transparent : {false, true})
			{
				const char* stageName = transparent ? "Draw Transparent" : "Draw Opaque";
				sample(RenderCommandOpcode::BeginSample, stageName);
				DrawingSettings drawing;
				drawing.shaderTags = {{"SRPDefaultUnlit"}, {"UniversalForward"}, {"ForwardBase"}};
				drawing.sorting = {transparent ? SortingCriteria::CommonTransparent : SortingCriteria::CommonOpaque,
				                   camera.position};
				FilteringSettings filtering;
				filtering.minimumQueue = transparent ? 2501 : 0;
				filtering.maximumQueue = transparent ? 5000 : 2500;
				const auto list = Take(context.CreateRendererList(culled, drawing, filtering));
				RenderCommand draw;
				draw.opcode = RenderCommandOpcode::DrawRendererList;
				draw.camera = camera;
				draw.lighting = snapshot.Lighting();
				for (const auto& entry : *Take(context.Entries(list)))
				{
					SceneDrawRecord record;
					record.object = snapshot.Objects()[entry.objectIndex];
					record.subShaderIndex = entry.subShaderIndex;
					record.passIndex = entry.passIndex;
					record.selectedPass = true;
					record.resolvedPass = entry.resolvedPass;
					draw.draws.push_back(std::move(record));
				}
				commands.push_back(std::move(draw));
				sample(RenderCommandOpcode::EndSample, stageName);
			}
			RenderCommand outputTarget;
			outputTarget.opcode = RenderCommandOpcode::SetRenderTarget;
			outputTarget.target.id = 0;
			commands.push_back(std::move(outputTarget));
			RenderCommand exposure;
			exposure.opcode = RenderCommandOpcode::SetGlobalParameters;
			exposure.global.name = "_UrsaExposureEV";
			exposure.global.kind = 1;
			exposure.global.bytes.resize(sizeof(Float32));
			const Float32 exposureValue = 0.0f;
			std::memcpy(exposure.global.bytes.data(), &exposureValue, sizeof(exposureValue));
			commands.push_back(std::move(exposure));
			RenderCommand toneMapping;
			toneMapping.opcode = RenderCommandOpcode::SetGlobalParameters;
			toneMapping.global.name = "_UrsaToneMappingMode";
			toneMapping.global.kind = 6;
			toneMapping.global.bytes.resize(sizeof(Int32));
			const Int32 toneMappingValue = 1;
			std::memcpy(toneMapping.global.bytes.data(), &toneMappingValue, sizeof(toneMappingValue));
			commands.push_back(std::move(toneMapping));
			RenderCommand bloomTexture;
			bloomTexture.opcode = RenderCommandOpcode::SetGlobalParameters;
			bloomTexture.global.name = "_UrsaBloomTexture";
			bloomTexture.global.kind = 3;
			bloomTexture.global.texture.texture = sceneBacking;
			commands.push_back(std::move(bloomTexture));
			RenderCommand bloomIntensity;
			bloomIntensity.opcode = RenderCommandOpcode::SetGlobalParameters;
			bloomIntensity.global.name = "_UrsaBloomIntensity";
			bloomIntensity.global.kind = 1;
			bloomIntensity.global.bytes.resize(sizeof(Float32));
			const Float32 bloomIntensityValue = 0.0f;
			std::memcpy(bloomIntensity.global.bytes.data(), &bloomIntensityValue, sizeof(bloomIntensityValue));
			commands.push_back(std::move(bloomIntensity));
			RenderCommand sourceRect;
			sourceRect.opcode = RenderCommandOpcode::SetGlobalParameters;
			sourceRect.global.name = "_UrsaFinalSourceRect";
			sourceRect.global.kind = 2;
			sourceRect.global.bytes.resize(sizeof(Array<Float32, 4>));
			const Array<Float32, 4> sourceRectValue = {rect.x, rect.y, rect.width, rect.height};
			std::memcpy(sourceRect.global.bytes.data(), sourceRectValue.data(), sizeof(sourceRectValue));
			commands.push_back(std::move(sourceRect));
			RenderCommand outputEncoding;
			outputEncoding.opcode = RenderCommandOpcode::SetGlobalParameters;
			outputEncoding.global.name = "_UrsaFinalOutputIsSrgb";
			outputEncoding.global.kind = 6;
			outputEncoding.global.bytes.resize(sizeof(Int32));
			const Int32 outputIsSrgb = target->color->desc.format == Rhi::Format::RgbA8Srgb;
			std::memcpy(outputEncoding.global.bytes.data(), &outputIsSrgb, sizeof(outputIsSrgb));
			commands.push_back(std::move(outputEncoding));
			sample(RenderCommandOpcode::BeginSample, "Final Output");
			RenderCommand finalOutput;
			finalOutput.opcode = RenderCommandOpcode::Blit;
			finalOutput.source.texture = sceneBacking;
			finalOutput.target.id = 0;
			finalOutput.material = finalOutputMaterial;
			finalOutput.pass = 0;
			commands.push_back(std::move(finalOutput));
			sample(RenderCommandOpcode::EndSample, "Final Output");
			Done(executor->Execute(commands));
			Done(executor->Submit());
			backingInitializedThisFrame = true;
		}
		if (snapshot.Cameras().empty())
		{
			throw std::runtime_error("Scene has no active rendering camera");
		}
		Done(context.End());
		Take(executor->End());
		sceneBackingInitialized = backingInitializedThisFrame;
		return {};
	}
	catch (const std::exception& error)
	{
		if (executor->IsActive())
		{
			executor->Abort();
		}
		context.End();
		return Result<void>::Failure(error.what());
	}
}
} // namespace Ursa::Rendering
