#include <RenderingFixtures.hpp>
#include <MeshFixtures.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Ursa::Test
{
namespace
{
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

Vector<MeshDraw> FromCubes(const Vector<CubeDraw>& cubes)
{
	Vector<MeshDraw> draws;
	for (const auto& cube : cubes)
	{
		draws.push_back({cube.transform.Matrix(), cube.color, cube.material});
	}
	return draws;
}
} // namespace

Result<std::unique_ptr<SceneRenderer>> SceneRenderer::Create(Rhi::DevicePtr selectedDevice)
{
	auto commands = Rendering::RenderCommandExecutor::Create(selectedDevice);
	if (!commands)
	{
		return Result<std::unique_ptr<SceneRenderer>>::Failure(commands.error);
	}
	auto result = std::make_unique<SceneRenderer>();
	result->device = std::move(selectedDevice);
	result->executor = std::move(commands.value);
	result->neutralShadowAtlas = Take(Rendering::CreateRenderTexture(*result->device, {1, 1, 2}));
	return Result<std::unique_ptr<SceneRenderer>>::Success(std::move(result));
}

Result<void> SceneRenderer::Record(const Mat4& viewProjection, const Vector<MeshDraw>& draws,
                                   const SceneLighting& lighting, Vec3 cameraPosition)
{
	using namespace Rendering;
	RenderCommand clear;
	clear.opcode = RenderCommandOpcode::ClearTarget;
	clear.clearFlags = 3;
	RenderCommand shadowAtlas;
	shadowAtlas.opcode = RenderCommandOpcode::SetGlobalParameters;
	shadowAtlas.global.name = "_UrsaDirectionalShadowAtlas";
	shadowAtlas.global.kind = 3;
	shadowAtlas.global.texture.texture = neutralShadowAtlas;
	RenderCommand shadowCount;
	shadowCount.opcode = RenderCommandOpcode::SetGlobalParameters;
	shadowCount.global.name = "_UrsaDirectionalShadowCascadeCount";
	shadowCount.global.kind = 6;
	shadowCount.global.bytes.resize(sizeof(Int32));
	const Int32 noShadowCascades = 0;
	std::memcpy(shadowCount.global.bytes.data(), &noShadowCascades, sizeof(noShadowCascades));
	clear.color = {0.035f, 0.055f, 0.09f, 1};
	RenderCommand command;
	command.opcode = RenderCommandOpcode::DrawMesh;
	command.camera.viewProjection = viewProjection;
	command.camera.position = cameraPosition;
	command.lighting = lighting;
	Vector<MeshDraw> ordered = draws;
	std::stable_sort(ordered.begin(), ordered.end(),
	                 [&](const MeshDraw& a, const MeshDraw& b)
	                 {
		                 const auto modeA = a.material ? a.material->Parameters().alphaMode : AlphaMode::Opaque;
		                 const auto modeB = b.material ? b.material->Parameters().alphaMode : AlphaMode::Opaque;
		                 if (modeA != modeB)
		                 {
			                 return modeA < modeB;
		                 }
		                 const auto distance = [&](const Mat4& model)
		                 {
			                 const Vec3 delta{model.At(0, 3) - cameraPosition.x, model.At(1, 3) - cameraPosition.y,
			                                  model.At(2, 3) - cameraPosition.z};
			                 return Dot(delta, delta);
		                 };
		                 return modeA == AlphaMode::Blend
		                            ? distance(a.model) > distance(b.model)
		                            : std::less<const MaterialInstance*>{}(a.material.get(), b.material.get());
	                 });
	for (const auto& draw : ordered)
	{
		auto shader = draw.material ? draw.material->Shader() : nullptr;
		if (!shader || shader->subShaders.empty())
		{
			shader = Take(executor->ResolveBuiltinShader(draw.material && draw.material->Type() ==
			                                                                  MaterialType::PbrMetallicRoughness
			                                                 ? Shader::BuiltinPbrShaderId()
			                                                 : Shader::BuiltinUnlitShaderId()));
		}
		const auto passes = Take(OrderedGraphicsPasses(shader, "UrsaUniversal"));
		for (const auto& pass : passes)
		{
			const auto& tags = pass.programShader->subShaders[pass.subShaderIndex].passes[pass.passIndex].tags;
			const auto mode =
			    std::find_if(tags.begin(), tags.end(), [](const auto& tag) { return tag.name == "LightMode"; });
			if (mode != tags.end() && mode->value != "Forward" && mode->value != "SRPDefaultUnlit" &&
			    mode->value != "UniversalForward" && mode->value != "ForwardBase")
			{
				continue;
			}
			SceneDrawRecord record;
			record.object.world = draw.model;
			record.object.material = draw.material;
			record.object.parameters.baseColor = draw.color;
			record.object.mesh = draw.mesh ? draw.mesh : Take(Cube());
			record.object.meshSnapshot = draw.meshSnapshot ? draw.meshSnapshot : record.object.mesh->Snapshot();
			record.object.subMesh = draw.subMesh;
			record.object.jointPalette = draw.jointPalette;
			if (record.object.meshSnapshot && (record.object.meshSnapshot->data.positions.empty() ||
			                                   record.object.meshSnapshot->data.indices.empty() ||
			                                   record.object.meshSnapshot->data.subMeshes.empty()))
			{
				continue;
			}
			record.resolvedPass = pass;
			command.draws.push_back(std::move(record));
		}
	}
	Done(executor->Execute({clear, shadowAtlas, shadowCount, command}));
	return executor->Submit();
}

Result<Rhi::FencePtr> SceneRenderer::DrawOffscreen(const std::shared_ptr<Rendering::RenderTextureResource>& target,
                                                   const Mat4& viewProjection,
                                                   const Vector<Rendering::RenderObject>& draws,
                                                   const SceneLighting& lighting, Vec3 cameraPosition,
                                                   Rhi::ResourceState finalState)
{
	output = target;
	return DrawOffscreen(target->color, viewProjection, draws, lighting, cameraPosition, finalState);
}

Result<Rhi::FencePtr> SceneRenderer::DrawOffscreen(const Rhi::TexturePtr& color, const Mat4& viewProjection,
                                                   const Vector<MeshDraw>& draws, const SceneLighting& lighting,
                                                   Vec3 cameraPosition, Rhi::ResourceState finalState)
{
	try
	{
		if (!output || output->color != color)
		{
			output = std::make_shared<Rendering::RenderTextureResource>();
			output->color = color;
			output->description = {color->desc.width, color->desc.height, 0, 32};
			output->depth =
			    Take(device->CreateTexture({color->desc.width, color->desc.height, Rhi::Format::D32FloatS8Uint,
			                                Rhi::TextureUsage::DepthAttachment}));
		}
		Done(executor->Begin(output, finalState));
		Done(Record(viewProjection, draws, lighting, cameraPosition));
		return executor->End();
	}
	catch (const std::exception& error)
	{
		if (executor->IsActive())
		{
			executor->Abort();
		}
		return Result<Rhi::FencePtr>::Failure(error.what());
	}
}

Result<Rhi::FencePtr> SceneRenderer::DrawOffscreen(const Rhi::TexturePtr& color, const Mat4& viewProjection,
                                                   const Vector<Rendering::RenderObject>& objects,
                                                   const SceneLighting& lighting, Vec3 cameraPosition,
                                                   Rhi::ResourceState finalState)
{
	return DrawOffscreen(color, viewProjection, MeshDraws(objects), lighting, cameraPosition, finalState);
}

Result<Rhi::FencePtr> SceneRenderer::DrawCubes(const Rhi::TexturePtr& color, const Camera& camera,
                                               const Vector<CubeDraw>& cubes)
{
	return DrawOffscreen(color, camera.ViewProjection(Float32(color->desc.width) / color->desc.height),
	                     FromCubes(cubes), {}, camera.position);
}

Result<void> SceneRenderer::Render(const Rhi::SwapchainPtr& window, const Camera& camera, const Vector<CubeDraw>& cubes)
{
	try
	{
		Done(executor->Begin({}, Rhi::ResourceState::Present, window));
		const auto color = executor->CameraTarget()->color;
		Done(Record(camera.ViewProjection(Float32(color->desc.width) / color->desc.height), FromCubes(cubes), {},
		            camera.position));
		Take(executor->End());
		return {};
	}
	catch (const std::exception& error)
	{
		if (executor->IsActive())
		{
			executor->Abort();
		}
		return Result<void>::Failure(error.what());
	}
}

Size SceneRenderer::MeshCacheSize() const
{
	return executor->Statistics().meshCacheEntries;
}
} // namespace Ursa::Test
