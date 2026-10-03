#include <renderer3d/Renderer3D.hpp>
#include <core/StringInterop.hpp>
#include <vfs/AssetRoots.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace Ursa
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

void done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

Vector<uint8_t> LoadShader(const VirtualFileSystem& files, const char* name)
{
	auto path = VirtualPath::Parse(String(engineScheme) + ":shaders/renderer3d/" + name + ".spv");
	if (!path)
	{
		throw std::runtime_error(ToStdString(path.error));
	}
	auto bytes = files.Read(path.value);
	if (!bytes)
	{
		throw std::runtime_error(ToStdString(bytes.error));
	}
	if (bytes.value.empty())
	{
		throw std::runtime_error(std::string("Empty engine shader ") + name);
	}
	return std::move(bytes.value);
}

struct Vertex
{
	float position[3];
	float normal[3];
	float uv[2];
	float tangent[3];
};

struct CubeMesh
{
	Array<Vertex, 24> vertices{};
	Array<uint32_t, 36> indices{};
};

CubeMesh MakeCube()
{
	CubeMesh mesh;
	size_t vertex = 0;
	size_t index = 0;
	const auto face = [&](Vec3 normal, const Array<Vec3, 4>& corners)
	{
		const uint32_t first = static_cast<uint32_t>(vertex);
		const Vec3 tangent = Normalized(corners[1] - corners[0]);
		constexpr float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
		for (size_t corner = 0; corner < corners.size(); ++corner)
		{
			const Vec3 p = corners[corner];
			mesh.vertices[vertex++] = {{p.x, p.y, p.z},
			                           {normal.x, normal.y, normal.z},
			                           {uvs[corner][0], uvs[corner][1]},
			                           {tangent.x, tangent.y, tangent.z}};
		}
		for (uint32_t corner : {0u, 2u, 1u, 0u, 3u, 2u})
		{
			mesh.indices[index++] = first + corner;
		}
	};
	constexpr float h = 0.5f;
	face({0, 0, 1}, {{{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}});
	face({0, 0, -1}, {{{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}});
	face({1, 0, 0}, {{{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}}});
	face({-1, 0, 0}, {{{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}});
	face({0, 1, 0}, {{{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}});
	face({0, -1, 0}, {{{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}});
	return mesh;
}

struct GpuDirectionalLight
{
	float directionIntensity[4]{};
	float color[4]{};
};

struct GpuPointLight
{
	float positionRange[4]{};
	float colorIntensity[4]{};
};

struct GpuFrameHeader
{
	float viewProjection[16]{};
	float cameraPosition[4]{};
	float ambient[4]{};
	int32_t counts[4]{};
	GpuDirectionalLight directional[SceneLighting::maxDirectionalLights]{};
	GpuPointLight point[SceneLighting::maxPointLights]{};
};

struct GpuInstance
{
	float model[16]{};
	float color[4]{};
};

struct GpuMaterial
{
	float baseColor[4]{};
	float factors[4]{};
	float emissive[4]{};
	int32_t flags[4]{};
};

static_assert(sizeof(GpuFrameHeader) == 496, "Unexpected frame layout");
static_assert(sizeof(GpuInstance) == 80, "Unexpected instance layout");
static_assert(sizeof(GpuMaterial) == 64, "Unexpected material layout");

void WriteColor(float (&out)[4], Color color)
{
	out[0] = color.red;
	out[1] = color.green;
	out[2] = color.blue;
	out[3] = color.alpha;
}

Vector<ModelBoxInstance> ModelsFromBoxes(const Vector<BoxInstance>& boxes)
{
	Vector<ModelBoxInstance> models;
	models.reserve(boxes.size());
	for (const auto& box : boxes)
	{
		models.push_back({box.transform.Matrix(), box.color, box.material});
	}
	return models;
}

struct DrawItem
{
	const ModelBoxInstance* box = nullptr;
	std::shared_ptr<MaterialInstance> material;
	AlphaMode mode = AlphaMode::opaque;
	float distanceSquared = 0.0f;
	size_t ordinal = 0;
};

Vector<DrawItem> OrderItems(const Vector<ModelBoxInstance>& boxes, const std::shared_ptr<MaterialInstance>& fallback,
                            Vec3 cameraPosition)
{
	Vector<DrawItem> items;
	items.reserve(boxes.size());
	for (size_t i = 0; i < boxes.size(); ++i)
	{
		const auto& box = boxes[i];
		const Vec3 delta{box.model.At(0, 3) - cameraPosition.x, box.model.At(1, 3) - cameraPosition.y,
		                 box.model.At(2, 3) - cameraPosition.z};
		auto material = box.material ? box.material : fallback;
		items.push_back({&box, material, material->Parameters().alphaMode, Dot(delta, delta), i});
	}
	std::stable_sort(items.begin(), items.end(),
	                 [](const DrawItem& a, const DrawItem& b)
	                 {
		                 if (a.mode != b.mode)
		                 {
			                 return static_cast<int>(a.mode) < static_cast<int>(b.mode);
		                 }
		                 if (a.mode == AlphaMode::blend)
		                 {
			                 return a.distanceSquared == b.distanceSquared ? a.ordinal < b.ordinal
			                                                               : a.distanceSquared > b.distanceSquared;
		                 }
		                 return std::less<const MaterialInstance*>{}(a.material.get(), b.material.get());
	                 });
	return items;
}
} // namespace

struct Renderer3D::Impl
{
	explicit Impl(Rhi::DevicePtr selectedDevice) : device(std::move(selectedDevice))
	{
		fallback = std::make_shared<MaterialInstance>(MaterialType::unlit);
	}

	~Impl()
	{
		if (lastFence)
		{
			lastFence->Wait();
		}
	}

	void Initialize()
	{
		const CubeMesh cube = MakeCube();
		vertexBuffer =
		    Take(device->CreateBuffer({sizeof(cube.vertices), Rhi::BufferUsage::vertex, Rhi::Memory::upload}));
		indexBuffer = Take(device->CreateBuffer({sizeof(cube.indices), Rhi::BufferUsage::index, Rhi::Memory::upload}));
		done(vertexBuffer->Write(0, cube.vertices.data(), sizeof(cube.vertices)));
		done(indexBuffer->Write(0, cube.indices.data(), sizeof(cube.indices)));
		layout = Take(device->CreateBindGroupLayout({
		    {0, Rhi::BindingType::storageBuffer, 1, 3},
		    {1, Rhi::BindingType::uniformBuffer, 1, 2},
		    {2, Rhi::BindingType::combinedImageSampler, 1, 2},
		    {3, Rhi::BindingType::combinedImageSampler, 1, 2},
		    {4, Rhi::BindingType::combinedImageSampler, 1, 2},
		    {5, Rhi::BindingType::combinedImageSampler, 1, 2},
		    {6, Rhi::BindingType::combinedImageSampler, 1, 2},
		}));
		auto engineFiles = OpenEngineFileSystem();
		if (!engineFiles)
		{
			throw std::runtime_error(ToStdString(engineFiles.error));
		}
		pbrVertexShader = Take(device->CreateShader(
		    {Rhi::ShaderStage::vertex, Rhi::ShaderFormat::spirv, LoadShader(engineFiles.value, "cube.vert")}));
		unlitVertexShader = Take(device->CreateShader(
		    {Rhi::ShaderStage::vertex, Rhi::ShaderFormat::spirv, LoadShader(engineFiles.value, "unlit.vert")}));
		unlitShader = Take(device->CreateShader(
		    {Rhi::ShaderStage::fragment, Rhi::ShaderFormat::spirv, LoadShader(engineFiles.value, "cube.frag")}));
		pbrShader = Take(device->CreateShader(
		    {Rhi::ShaderStage::fragment, Rhi::ShaderFormat::spirv, LoadShader(engineFiles.value, "pbr.frag")}));
	}

	void WaitPrevious()
	{
		if (lastFence)
		{
			done(lastFence->Wait());
			lastFence.reset();
		}
		for (auto it = materialCache.begin(); it != materialCache.end();)
		{
			if (it->second.source.expired())
			{
				it = materialCache.erase(it);
			}
			else
			{
				++it;
			}
		}
		for (auto it = textureCache.begin(); it != textureCache.end();)
		{
			if (it->second.source.use_count() == 1)
			{
				it = textureCache.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void PrepareTarget(const Rhi::TexturePtr& color, size_t count)
	{
		if (!color)
		{
			throw std::runtime_error("3D renderer requires a color target");
		}
		if (!depth || depth->desc.width != color->desc.width || depth->desc.height != color->desc.height)
		{
			depth = Take(device->CreateTexture(
			    {color->desc.width, color->desc.height, Rhi::Format::d32Float, Rhi::TextureUsage::depthAttachment}));
		}
		if (count > capacity || !sceneBuffer)
		{
			capacity = std::max<size_t>(64, capacity);
			while (capacity < count)
			{
				capacity *= 2;
			}
			sceneBuffer = Take(device->CreateBuffer({sizeof(GpuFrameHeader) + capacity * sizeof(GpuInstance),
			                                         Rhi::BufferUsage::storage, Rhi::Memory::upload}));
			materialCache.clear();
		}
	}

	Rhi::PipelinePtr PipelineFor(Rhi::Format format, MaterialType type, AlphaMode mode)
	{
		const auto key = std::make_tuple(static_cast<int>(format), static_cast<int>(type), static_cast<int>(mode));
		const auto found = pipelines.find(key);
		if (found != pipelines.end())
		{
			return found->second;
		}
		if (type != MaterialType::unlit && type != MaterialType::pbrMetallicRoughness)
		{
			throw std::runtime_error("Unsupported built-in material type");
		}
		Rhi::GraphicsPipelineDesc desc;
		desc.vertex = type == MaterialType::unlit ? unlitVertexShader : pbrVertexShader;
		desc.fragment = type == MaterialType::unlit ? unlitShader : pbrShader;
		desc.layout = layout;
		desc.colorFormat = format;
		desc.depthTest = true;
		desc.depthWrite = mode != AlphaMode::blend;
		desc.blend = mode == AlphaMode::blend;
		desc.cullBack = true;
		desc.vertexStride = sizeof(Vertex);
		desc.attributes = {{0, 0, Rhi::VertexFormat::float3}, {2, offsetof(Vertex, uv), Rhi::VertexFormat::float2}};
		if (type == MaterialType::pbrMetallicRoughness)
		{
			desc.attributes.push_back({1, offsetof(Vertex, normal), Rhi::VertexFormat::float3});
			desc.attributes.push_back({3, offsetof(Vertex, tangent), Rhi::VertexFormat::float3});
		}
		auto pipeline = Take(device->CreateGraphicsPipeline(desc));
		pipelines.emplace(key, pipeline);
		return pipeline;
	}

	Rhi::TexturePtr TextureFor(const MaterialTexturePtr& source, const Rhi::CommandListPtr& command)
	{
		const auto found = textureCache.find(source.get());
		if (found != textureCache.end())
		{
			return found->second.texture;
		}
		const auto bytes = source->GetPixels().size();
		auto upload = Take(device->CreateBuffer({bytes, Rhi::BufferUsage::transferSource, Rhi::Memory::upload}));
		done(upload->Write(0, source->GetPixels().data(), bytes));
		const auto format =
		    source->GetColorSpace() == TextureColorSpace::srgb ? Rhi::Format::rgbA8Srgb : Rhi::Format::rgbA8Unorm;
		auto texture =
		    Take(device->CreateTexture({source->GetWidth(), source->GetHeight(), format,
		                                Rhi::TextureUsage::sampled | Rhi::TextureUsage::transferDestination}));
		done(command->Barrier(texture, Rhi::ResourceState::undefined, Rhi::ResourceState::transferDestination));
		done(command->CopyBufferToTexture(upload, texture));
		done(command->Barrier(texture, Rhi::ResourceState::transferDestination, Rhi::ResourceState::shaderRead));
		textureCache.emplace(source.get(), GpuTexture{source, texture});
		return texture;
	}

	Rhi::BindGroupPtr GroupFor(const std::shared_ptr<MaterialInstance>& material, const Rhi::CommandListPtr& command)
	{
		auto& cached = materialCache[material.get()];
		if (cached.group && cached.version == material->Version())
		{
			return cached.group;
		}
		GpuMaterial gpu{};
		const auto& params = material->Parameters();
		WriteColor(gpu.baseColor, params.baseColor);
		gpu.factors[0] = params.metallic;
		gpu.factors[1] = params.roughness;
		gpu.factors[2] = params.normalScale;
		gpu.factors[3] = params.occlusionStrength;
		WriteColor(gpu.emissive, params.emissive);
		gpu.emissive[3] = params.alphaCutoff;
		gpu.flags[0] = static_cast<int32_t>(material->Type());
		gpu.flags[1] = static_cast<int32_t>(params.alphaMode);
		gpu.flags[2] = material == fallback ? 1 : 0;
		auto uniform = Take(device->CreateBuffer({sizeof(gpu), Rhi::BufferUsage::uniform, Rhi::Memory::upload}));
		done(uniform->Write(0, &gpu, sizeof(gpu)));
		done(command->Barrier(uniform, Rhi::ResourceState::undefined, Rhi::ResourceState::uniformRead));
		auto sampler = Take(device->CreateSampler({params.sampler.linear, params.sampler.repeat}));
		Vector<Rhi::BindingResource> resources{{0, sceneBuffer, {}, {}}, {1, uniform, {}, {}}};
		for (uint32_t i = 0; i < 5; ++i)
		{
			const auto slot = static_cast<MaterialTextureSlot>(i);
			resources.push_back({i + 2, {}, TextureFor(material->ResolvedTexture(slot), command), sampler});
		}
		auto group = Take(device->CreateBindGroup({layout, std::move(resources)}));
		cached = {material, material->Version(), uniform, group};
		return group;
	}

	void record(const Rhi::CommandListPtr& command, const Rhi::TexturePtr& color, Rhi::ResourceState before,
	            Rhi::ResourceState after, const Mat4& viewProjection, const Vector<ModelBoxInstance>& boxes,
	            const SceneLighting& lighting, Vec3 cameraPosition)
	{
		if (!command)
		{
			throw std::runtime_error("3D renderer requires a command list");
		}
		if (lighting.directional.size() > SceneLighting::maxDirectionalLights ||
		    lighting.point.size() > SceneLighting::maxPointLights)
		{
			throw std::runtime_error("Renderer light count exceeds directional or point capacity");
		}
		PrepareTarget(color, boxes.size());
		const auto items = OrderItems(boxes, fallback, cameraPosition);
		GpuFrameHeader frame{};
		std::memcpy(frame.viewProjection, viewProjection.values.data(), sizeof(frame.viewProjection));
		frame.cameraPosition[0] = cameraPosition.x;
		frame.cameraPosition[1] = cameraPosition.y;
		frame.cameraPosition[2] = cameraPosition.z;
		WriteColor(frame.ambient, lighting.ambientColor);
		frame.ambient[3] = lighting.ambientIntensity;
		frame.counts[0] = static_cast<int32_t>(lighting.directional.size());
		frame.counts[1] = static_cast<int32_t>(lighting.point.size());
		for (size_t i = 0; i < lighting.directional.size(); ++i)
		{
			const auto& light = lighting.directional[i];
			frame.directional[i].directionIntensity[0] = light.direction.x;
			frame.directional[i].directionIntensity[1] = light.direction.y;
			frame.directional[i].directionIntensity[2] = light.direction.z;
			frame.directional[i].directionIntensity[3] = light.intensity;
			WriteColor(frame.directional[i].color, light.color);
		}
		for (size_t i = 0; i < lighting.point.size(); ++i)
		{
			const auto& light = lighting.point[i];
			frame.point[i].positionRange[0] = light.position.x;
			frame.point[i].positionRange[1] = light.position.y;
			frame.point[i].positionRange[2] = light.position.z;
			frame.point[i].positionRange[3] = light.range;
			WriteColor(frame.point[i].colorIntensity, light.color);
			frame.point[i].colorIntensity[3] = light.intensity;
		}
		done(sceneBuffer->Write(0, &frame, sizeof(frame)));
		Vector<GpuInstance> instances;
		instances.reserve(items.size());
		for (const auto& item : items)
		{
			GpuInstance gpu{};
			std::memcpy(gpu.model, item.box->model.values.data(), sizeof(gpu.model));
			WriteColor(gpu.color, item.box->material ? Color{} : item.box->color);
			instances.push_back(gpu);
		}
		if (!instances.empty())
		{
			done(sceneBuffer->Write(sizeof(frame), instances.data(), instances.size() * sizeof(GpuInstance)));
		}

		done(command->Barrier(color, before, Rhi::ResourceState::colorAttachment));
		done(command->Barrier(depth, Rhi::ResourceState::undefined, Rhi::ResourceState::depthAttachment));
		done(command->Barrier(sceneBuffer, Rhi::ResourceState::undefined, Rhi::ResourceState::storageReadWrite));
		Vector<Rhi::BindGroupPtr> groups;
		groups.reserve(items.size());
		for (const auto& item : items)
		{
			groups.push_back(GroupFor(item.material, command));
		}
		Rhi::RenderTarget target;
		target.color = color;
		target.depth = depth;
		target.colorValue[0] = 0.035f;
		target.colorValue[1] = 0.055f;
		target.colorValue[2] = 0.09f;
		done(command->BeginRendering(target));
		done(command->BindVertexBuffer(vertexBuffer));
		done(command->BindIndexBuffer(indexBuffer));
		for (size_t first = 0; first < items.size();)
		{
			size_t end = first + 1;
			if (items[first].mode != AlphaMode::blend)
			{
				while (end < items.size() && items[end].material == items[first].material &&
				       items[end].mode == items[first].mode)
				{
					++end;
				}
			}
			done(command->BindPipeline(
			    PipelineFor(color->desc.format, items[first].material->Type(), items[first].mode)));
			done(command->BindGroup(groups[first]));
			done(command->DrawIndexed(36, static_cast<uint32_t>(end - first), static_cast<uint32_t>(first)));
			first = end;
		}
		done(command->EndRendering());
		done(command->Barrier(color, Rhi::ResourceState::colorAttachment, after));
	}

	Rhi::FencePtr Draw(const Rhi::TexturePtr& color, Rhi::ResourceState before, Rhi::ResourceState after,
	                   const Mat4& viewProjection, const Vector<ModelBoxInstance>& boxes, const SceneLighting& lighting,
	                   Vec3 cameraPosition, const Rhi::SwapchainPtr& swapchain = {})
	{
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		record(command, color, before, after, viewProjection, boxes, lighting, cameraPosition);
		done(command->End());
		lastFence = Take(device->Submit(command, swapchain));
		return lastFence;
	}

	struct GpuTexture
	{
		MaterialTexturePtr source;
		Rhi::TexturePtr texture;
	};

	struct GpuMaterialCache
	{
		std::weak_ptr<MaterialInstance> source;
		uint64_t version = 0;
		Rhi::BufferPtr uniform;
		Rhi::BindGroupPtr group;
	};

	Rhi::DevicePtr device;
	std::shared_ptr<MaterialInstance> fallback;
	Rhi::BufferPtr vertexBuffer;
	Rhi::BufferPtr indexBuffer;
	Rhi::BufferPtr sceneBuffer;
	Rhi::LayoutPtr layout;
	Rhi::ShaderPtr pbrVertexShader;
	Rhi::ShaderPtr unlitVertexShader;
	Rhi::ShaderPtr unlitShader;
	Rhi::ShaderPtr pbrShader;
	Rhi::TexturePtr depth;
	Rhi::FencePtr lastFence;
	Map<std::tuple<int, int, int>, Rhi::PipelinePtr> pipelines;
	UnorderedMap<const MaterialTexture*, GpuTexture> textureCache;
	UnorderedMap<const MaterialInstance*, GpuMaterialCache> materialCache;
	size_t capacity = 0;
};

Renderer3D::Renderer3D(std::unique_ptr<Impl> impl) : impl(std::move(impl))
{
}

Renderer3D::~Renderer3D() = default;

Result<std::unique_ptr<Renderer3D>> Renderer3D::Create(Rhi::DevicePtr device)
{
	try
	{
		if (!device)
		{
			throw std::runtime_error("3D renderer requires a device");
		}
		auto impl = std::make_unique<Impl>(std::move(device));
		impl->Initialize();
		return Result<std::unique_ptr<Renderer3D>>::Success(
		    std::unique_ptr<Renderer3D>(new Renderer3D(std::move(impl))));
	}
	catch (const std::exception& error)
	{
		return Result<std::unique_ptr<Renderer3D>>::Failure(error.what());
	}
}

Result<void> Renderer3D::Render(const Rhi::SwapchainPtr& swapchain, const Camera& camera,
                                const Vector<BoxInstance>& boxes)
{
	try
	{
		if (!swapchain)
		{
			throw std::runtime_error("3D renderer requires a swapchain");
		}
		impl->WaitPrevious();
		const auto acquired = Take(swapchain->Acquire());
		if (!acquired.texture)
		{
			return {};
		}
		const float aspect =
		    static_cast<float>(acquired.texture->desc.width) / static_cast<float>(acquired.texture->desc.height);
		impl->Draw(acquired.texture, acquired.initialState, Rhi::ResourceState::present, camera.ViewProjection(aspect),
		           ModelsFromBoxes(boxes), {}, camera.position, swapchain);
		done(swapchain->Present());
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}

Result<void> Renderer3D::RenderModels(const Rhi::SwapchainPtr& swapchain, const Mat4& viewProjection,
                                      const Vector<ModelBoxInstance>& boxes, const SceneLighting& lighting,
                                      Vec3 cameraPosition)
{
	try
	{
		if (!swapchain)
		{
			throw std::runtime_error("3D renderer requires a swapchain");
		}
		impl->WaitPrevious();
		const auto acquired = Take(swapchain->Acquire());
		if (!acquired.texture)
		{
			return {};
		}
		impl->Draw(acquired.texture, acquired.initialState, Rhi::ResourceState::present, viewProjection, boxes,
		           lighting, cameraPosition, swapchain);
		done(swapchain->Present());
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}

Result<Rhi::FencePtr> Renderer3D::RenderOffscreen(const Rhi::TexturePtr& color, const Camera& camera,
                                                  const Vector<BoxInstance>& boxes)
{
	try
	{
		impl->WaitPrevious();
		if (!color)
		{
			throw std::runtime_error("3D renderer requires a color target");
		}
		const float aspect = static_cast<float>(color->desc.width) / static_cast<float>(color->desc.height);
		return Result<Rhi::FencePtr>::Success(
		    impl->Draw(color, Rhi::ResourceState::undefined, Rhi::ResourceState::transferSource,
		               camera.ViewProjection(aspect), ModelsFromBoxes(boxes), {}, camera.position));
	}
	catch (const std::exception& error)
	{
		return Result<Rhi::FencePtr>::Failure(error.what());
	}
}

Result<Rhi::FencePtr> Renderer3D::RenderOffscreenModels(const Rhi::TexturePtr& color, const Mat4& viewProjection,
                                                        const Vector<ModelBoxInstance>& boxes,
                                                        const SceneLighting& lighting, Vec3 cameraPosition)
{
	try
	{
		impl->WaitPrevious();
		return Result<Rhi::FencePtr>::Success(impl->Draw(color, Rhi::ResourceState::undefined,
		                                                 Rhi::ResourceState::transferSource, viewProjection, boxes,
		                                                 lighting, cameraPosition));
	}
	catch (const std::exception& error)
	{
		return Result<Rhi::FencePtr>::Failure(error.what());
	}
}

Result<void> Renderer3D::RecordOffscreenModels(const Rhi::CommandListPtr& command, const Rhi::TexturePtr& color,
                                               Rhi::ResourceState initialState, Rhi::ResourceState finalState,
                                               const Mat4& viewProjection, const Vector<ModelBoxInstance>& boxes,
                                               const SceneLighting& lighting, Vec3 cameraPosition)
{
	try
	{
		impl->WaitPrevious();
		impl->record(command, color, initialState, finalState, viewProjection, boxes, lighting, cameraPosition);
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}
} // namespace Ursa
