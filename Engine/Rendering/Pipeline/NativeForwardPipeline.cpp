#include <Rendering/Pipeline/NativeForwardPipeline.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Core/StringInterop.hpp>
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
		result->executor = Take(RenderCommandExecutor::Create(std::move(device)));
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
		const auto target = executor->CameraTarget();
		if (!target || !target->color)
		{
			throw std::runtime_error("Native Forward requires an acquired color target");
		}
		const auto width = target->color->desc.width, height = target->color->desc.height;
		Done(snapshot.Capture(scene, Float32(width) / Float32(height), filter, std::move(cameraOverride),
		                      [this](ResourceId id) { return executor->ResolveBuiltinShader(id); }));
		Take(context.Begin(1, ++frameId, snapshot));
		for (const auto& camera : snapshot.Cameras())
		{
			Done(executor->ResetCamera());
			commands.clear();
			const auto& rect = camera.settings.viewport;
			RenderCommand viewport;
			viewport.opcode = RenderCommandOpcode::SetViewport;
			viewport.viewport = {rect.x * width, rect.y * height, rect.width * width, rect.height * height, 0, 1};
			commands.push_back(viewport);
			RenderCommand scissor;
			scissor.opcode = RenderCommandOpcode::SetScissor;
			scissor.scissor = {Int32(std::floor(rect.x * width)), Int32(std::floor(rect.y * height)),
			                   UInt32(std::ceil((rect.x + rect.width) * width)) - UInt32(std::floor(rect.x * width)),
			                   UInt32(std::ceil((rect.y + rect.height) * height)) -
			                       UInt32(std::floor(rect.y * height))};
			commands.push_back(scissor);
			if (camera.settings.clearFlags != CameraClearFlags::Nothing)
			{
				RenderCommand clear;
				clear.opcode = RenderCommandOpcode::ClearTarget;
				clear.clearFlags = camera.settings.clearFlags == CameraClearFlags::ColorAndDepth ? 3 : 2;
				clear.color = camera.settings.background;
				commands.push_back(clear);
			}
			const auto culled = Take(context.Cull(camera));
			for (const bool transparent : {false, true})
			{
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
			}
			Done(executor->Execute(commands));
			Done(executor->Submit());
		}
		if (snapshot.Cameras().empty())
		{
			throw std::runtime_error("Scene has no active rendering camera");
		}
		Done(context.End());
		Take(executor->End());
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
