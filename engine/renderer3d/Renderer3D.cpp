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

namespace ursa {
namespace {
template <class T> T take(Result<T> result) {
    if (!result)
        throw std::runtime_error(toStdString(result.error));
    return std::move(result.value);
}

void done(Result<void> result) {
    if (!result)
        throw std::runtime_error(toStdString(result.error));
}

Vector<uint8_t> loadShader(const VirtualFileSystem &files, const char *name) {
    auto path = VirtualPath::parse(String(engineScheme) + ":shaders/renderer3d/" + name + ".spv");
    if (!path)
        throw std::runtime_error(toStdString(path.error));
    auto bytes = files.read(path.value);
    if (!bytes)
        throw std::runtime_error(toStdString(bytes.error));
    if (bytes.value.empty())
        throw std::runtime_error(std::string("Empty engine shader ") + name);
    return std::move(bytes.value);
}

struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float tangent[3];
};

struct CubeMesh {
    Array<Vertex, 24> vertices{};
    Array<uint32_t, 36> indices{};
};

CubeMesh makeCube() {
    CubeMesh mesh;
    size_t vertex = 0;
    size_t index = 0;
    const auto face = [&](Vec3 normal, const Array<Vec3, 4> &corners) {
        const uint32_t first = static_cast<uint32_t>(vertex);
        const Vec3 tangent = normalized(corners[1] - corners[0]);
        constexpr float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (size_t corner = 0; corner < corners.size(); ++corner) {
            const Vec3 p = corners[corner];
            mesh.vertices[vertex++] = {{p.x, p.y, p.z},
                                       {normal.x, normal.y, normal.z},
                                       {uvs[corner][0], uvs[corner][1]},
                                       {tangent.x, tangent.y, tangent.z}};
        }
        for (uint32_t corner : {0u, 2u, 1u, 0u, 3u, 2u})
            mesh.indices[index++] = first + corner;
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

struct GpuDirectionalLight {
    float directionIntensity[4]{};
    float color[4]{};
};

struct GpuPointLight {
    float positionRange[4]{};
    float colorIntensity[4]{};
};

struct GpuFrameHeader {
    float viewProjection[16]{};
    float cameraPosition[4]{};
    float ambient[4]{};
    int32_t counts[4]{};
    GpuDirectionalLight directional[SceneLighting::maxDirectionalLights]{};
    GpuPointLight point[SceneLighting::maxPointLights]{};
};

struct GpuInstance {
    float model[16]{};
    float color[4]{};
};

struct GpuMaterial {
    float baseColor[4]{};
    float factors[4]{};
    float emissive[4]{};
    int32_t flags[4]{};
};

static_assert(sizeof(GpuFrameHeader) == 496, "Unexpected frame layout");
static_assert(sizeof(GpuInstance) == 80, "Unexpected instance layout");
static_assert(sizeof(GpuMaterial) == 64, "Unexpected material layout");

void writeColor(float (&out)[4], Color color) {
    out[0] = color.red;
    out[1] = color.green;
    out[2] = color.blue;
    out[3] = color.alpha;
}

Vector<ModelBoxInstance> modelsFromBoxes(const Vector<BoxInstance> &boxes) {
    Vector<ModelBoxInstance> models;
    models.reserve(boxes.size());
    for (const auto &box : boxes)
        models.push_back({box.transform.matrix(), box.color, box.material});
    return models;
}

struct DrawItem {
    const ModelBoxInstance *box = nullptr;
    std::shared_ptr<MaterialInstance> material;
    AlphaMode mode = AlphaMode::Opaque;
    float distanceSquared = 0.0f;
    size_t ordinal = 0;
};

Vector<DrawItem> orderItems(const Vector<ModelBoxInstance> &boxes, const std::shared_ptr<MaterialInstance> &fallback,
                            Vec3 cameraPosition) {
    Vector<DrawItem> items;
    items.reserve(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto &box = boxes[i];
        const Vec3 delta{box.model.at(0, 3) - cameraPosition.x, box.model.at(1, 3) - cameraPosition.y,
                         box.model.at(2, 3) - cameraPosition.z};
        auto material = box.material ? box.material : fallback;
        items.push_back({&box, material, material->parameters().alphaMode, dot(delta, delta), i});
    }
    std::stable_sort(items.begin(), items.end(), [](const DrawItem &a, const DrawItem &b) {
        if (a.mode != b.mode)
            return static_cast<int>(a.mode) < static_cast<int>(b.mode);
        if (a.mode == AlphaMode::Blend)
            return a.distanceSquared == b.distanceSquared ? a.ordinal < b.ordinal
                                                          : a.distanceSquared > b.distanceSquared;
        return std::less<const MaterialInstance *>{}(a.material.get(), b.material.get());
    });
    return items;
}
} // namespace

struct Renderer3D::Impl {
    explicit Impl(rhi::DevicePtr selectedDevice) : device(std::move(selectedDevice)) {
        fallback = std::make_shared<MaterialInstance>(MaterialType::Unlit);
    }

    ~Impl() {
        if (lastFence)
            lastFence->wait();
    }

    void initialize() {
        const CubeMesh cube = makeCube();
        vertexBuffer =
            take(device->createBuffer({sizeof(cube.vertices), rhi::BufferUsage::Vertex, rhi::Memory::Upload}));
        indexBuffer = take(device->createBuffer({sizeof(cube.indices), rhi::BufferUsage::Index, rhi::Memory::Upload}));
        done(vertexBuffer->write(0, cube.vertices.data(), sizeof(cube.vertices)));
        done(indexBuffer->write(0, cube.indices.data(), sizeof(cube.indices)));
        layout = take(device->createBindGroupLayout({
            {0, rhi::BindingType::StorageBuffer, 1, 3},
            {1, rhi::BindingType::UniformBuffer, 1, 2},
            {2, rhi::BindingType::CombinedImageSampler, 1, 2},
            {3, rhi::BindingType::CombinedImageSampler, 1, 2},
            {4, rhi::BindingType::CombinedImageSampler, 1, 2},
            {5, rhi::BindingType::CombinedImageSampler, 1, 2},
            {6, rhi::BindingType::CombinedImageSampler, 1, 2},
        }));
        auto engineFiles = openEngineFileSystem();
        if (!engineFiles)
            throw std::runtime_error(toStdString(engineFiles.error));
        pbrVertexShader = take(device->createShader(
            {rhi::ShaderStage::Vertex, rhi::ShaderFormat::SPIRV, loadShader(engineFiles.value, "cube.vert")}));
        unlitVertexShader = take(device->createShader(
            {rhi::ShaderStage::Vertex, rhi::ShaderFormat::SPIRV, loadShader(engineFiles.value, "unlit.vert")}));
        unlitShader = take(device->createShader(
            {rhi::ShaderStage::Fragment, rhi::ShaderFormat::SPIRV, loadShader(engineFiles.value, "cube.frag")}));
        pbrShader = take(device->createShader(
            {rhi::ShaderStage::Fragment, rhi::ShaderFormat::SPIRV, loadShader(engineFiles.value, "pbr.frag")}));
    }

    void waitPrevious() {
        if (lastFence) {
            done(lastFence->wait());
            lastFence.reset();
        }
        for (auto it = materialCache.begin(); it != materialCache.end();) {
            if (it->second.source.expired())
                it = materialCache.erase(it);
            else
                ++it;
        }
        for (auto it = textureCache.begin(); it != textureCache.end();) {
            if (it->second.source.use_count() == 1)
                it = textureCache.erase(it);
            else
                ++it;
        }
    }

    void prepareTarget(const rhi::TexturePtr &color, size_t count) {
        if (!color)
            throw std::runtime_error("3D renderer requires a color target");
        if (!depth || depth->desc.width != color->desc.width || depth->desc.height != color->desc.height)
            depth = take(device->createTexture(
                {color->desc.width, color->desc.height, rhi::Format::D32Float, rhi::TextureUsage::DepthAttachment}));
        if (count > capacity || !sceneBuffer) {
            capacity = std::max<size_t>(64, capacity);
            while (capacity < count)
                capacity *= 2;
            sceneBuffer = take(device->createBuffer({sizeof(GpuFrameHeader) + capacity * sizeof(GpuInstance),
                                                     rhi::BufferUsage::Storage, rhi::Memory::Upload}));
            materialCache.clear();
        }
    }

    rhi::PipelinePtr pipelineFor(rhi::Format format, MaterialType type, AlphaMode mode) {
        const auto key = std::make_tuple(static_cast<int>(format), static_cast<int>(type), static_cast<int>(mode));
        const auto found = pipelines.find(key);
        if (found != pipelines.end())
            return found->second;
        if (type != MaterialType::Unlit && type != MaterialType::PbrMetallicRoughness)
            throw std::runtime_error("Unsupported built-in material type");
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = type == MaterialType::Unlit ? unlitVertexShader : pbrVertexShader;
        desc.fragment = type == MaterialType::Unlit ? unlitShader : pbrShader;
        desc.layout = layout;
        desc.colorFormat = format;
        desc.depthTest = true;
        desc.depthWrite = mode != AlphaMode::Blend;
        desc.blend = mode == AlphaMode::Blend;
        desc.cullBack = true;
        desc.vertexStride = sizeof(Vertex);
        desc.attributes = {{0, 0, rhi::VertexFormat::Float3}, {2, offsetof(Vertex, uv), rhi::VertexFormat::Float2}};
        if (type == MaterialType::PbrMetallicRoughness) {
            desc.attributes.push_back({1, offsetof(Vertex, normal), rhi::VertexFormat::Float3});
            desc.attributes.push_back({3, offsetof(Vertex, tangent), rhi::VertexFormat::Float3});
        }
        auto pipeline = take(device->createGraphicsPipeline(desc));
        pipelines.emplace(key, pipeline);
        return pipeline;
    }

    rhi::TexturePtr textureFor(const MaterialTexturePtr &source, const rhi::CommandListPtr &command) {
        const auto found = textureCache.find(source.get());
        if (found != textureCache.end())
            return found->second.texture;
        const auto bytes = source->pixels().size();
        auto upload = take(device->createBuffer({bytes, rhi::BufferUsage::TransferSource, rhi::Memory::Upload}));
        done(upload->write(0, source->pixels().data(), bytes));
        const auto format =
            source->colorSpace() == TextureColorSpace::Srgb ? rhi::Format::RGBA8Srgb : rhi::Format::RGBA8Unorm;
        auto texture =
            take(device->createTexture({source->width(), source->height(), format,
                                        rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination}));
        done(command->barrier(texture, rhi::ResourceState::Undefined, rhi::ResourceState::TransferDestination));
        done(command->copyBufferToTexture(upload, texture));
        done(command->barrier(texture, rhi::ResourceState::TransferDestination, rhi::ResourceState::ShaderRead));
        textureCache.emplace(source.get(), GpuTexture{source, texture});
        return texture;
    }

    rhi::BindGroupPtr groupFor(const std::shared_ptr<MaterialInstance> &material, const rhi::CommandListPtr &command) {
        auto &cached = materialCache[material.get()];
        if (cached.group && cached.version == material->version())
            return cached.group;
        GpuMaterial gpu{};
        const auto &params = material->parameters();
        writeColor(gpu.baseColor, params.baseColor);
        gpu.factors[0] = params.metallic;
        gpu.factors[1] = params.roughness;
        gpu.factors[2] = params.normalScale;
        gpu.factors[3] = params.occlusionStrength;
        writeColor(gpu.emissive, params.emissive);
        gpu.emissive[3] = params.alphaCutoff;
        gpu.flags[0] = static_cast<int32_t>(material->type());
        gpu.flags[1] = static_cast<int32_t>(params.alphaMode);
        gpu.flags[2] = material == fallback ? 1 : 0;
        auto uniform = take(device->createBuffer({sizeof(gpu), rhi::BufferUsage::Uniform, rhi::Memory::Upload}));
        done(uniform->write(0, &gpu, sizeof(gpu)));
        done(command->barrier(uniform, rhi::ResourceState::Undefined, rhi::ResourceState::UniformRead));
        auto sampler = take(device->createSampler({params.sampler.linear, params.sampler.repeat}));
        Vector<rhi::BindingResource> resources{{0, sceneBuffer, {}, {}}, {1, uniform, {}, {}}};
        for (uint32_t i = 0; i < 5; ++i) {
            const auto slot = static_cast<MaterialTextureSlot>(i);
            resources.push_back({i + 2, {}, textureFor(material->resolvedTexture(slot), command), sampler});
        }
        auto group = take(device->createBindGroup({layout, std::move(resources)}));
        cached = {material, material->version(), uniform, group};
        return group;
    }

    void record(const rhi::CommandListPtr &command, const rhi::TexturePtr &color, rhi::ResourceState before,
                rhi::ResourceState after, const Mat4 &viewProjection, const Vector<ModelBoxInstance> &boxes,
                const SceneLighting &lighting, Vec3 cameraPosition) {
        if (!command)
            throw std::runtime_error("3D renderer requires a command list");
        if (lighting.directional.size() > SceneLighting::maxDirectionalLights ||
            lighting.point.size() > SceneLighting::maxPointLights)
            throw std::runtime_error("Renderer light count exceeds directional or point capacity");
        prepareTarget(color, boxes.size());
        const auto items = orderItems(boxes, fallback, cameraPosition);
        GpuFrameHeader frame{};
        std::memcpy(frame.viewProjection, viewProjection.values.data(), sizeof(frame.viewProjection));
        frame.cameraPosition[0] = cameraPosition.x;
        frame.cameraPosition[1] = cameraPosition.y;
        frame.cameraPosition[2] = cameraPosition.z;
        writeColor(frame.ambient, lighting.ambientColor);
        frame.ambient[3] = lighting.ambientIntensity;
        frame.counts[0] = static_cast<int32_t>(lighting.directional.size());
        frame.counts[1] = static_cast<int32_t>(lighting.point.size());
        for (size_t i = 0; i < lighting.directional.size(); ++i) {
            const auto &light = lighting.directional[i];
            frame.directional[i].directionIntensity[0] = light.direction.x;
            frame.directional[i].directionIntensity[1] = light.direction.y;
            frame.directional[i].directionIntensity[2] = light.direction.z;
            frame.directional[i].directionIntensity[3] = light.intensity;
            writeColor(frame.directional[i].color, light.color);
        }
        for (size_t i = 0; i < lighting.point.size(); ++i) {
            const auto &light = lighting.point[i];
            frame.point[i].positionRange[0] = light.position.x;
            frame.point[i].positionRange[1] = light.position.y;
            frame.point[i].positionRange[2] = light.position.z;
            frame.point[i].positionRange[3] = light.range;
            writeColor(frame.point[i].colorIntensity, light.color);
            frame.point[i].colorIntensity[3] = light.intensity;
        }
        done(sceneBuffer->write(0, &frame, sizeof(frame)));
        Vector<GpuInstance> instances;
        instances.reserve(items.size());
        for (const auto &item : items) {
            GpuInstance gpu{};
            std::memcpy(gpu.model, item.box->model.values.data(), sizeof(gpu.model));
            writeColor(gpu.color, item.box->material ? Color{} : item.box->color);
            instances.push_back(gpu);
        }
        if (!instances.empty())
            done(sceneBuffer->write(sizeof(frame), instances.data(), instances.size() * sizeof(GpuInstance)));

        done(command->barrier(color, before, rhi::ResourceState::ColorAttachment));
        done(command->barrier(depth, rhi::ResourceState::Undefined, rhi::ResourceState::DepthAttachment));
        done(command->barrier(sceneBuffer, rhi::ResourceState::Undefined, rhi::ResourceState::StorageReadWrite));
        Vector<rhi::BindGroupPtr> groups;
        groups.reserve(items.size());
        for (const auto &item : items)
            groups.push_back(groupFor(item.material, command));
        rhi::RenderTarget target;
        target.color = color;
        target.depth = depth;
        target.colorValue[0] = 0.035f;
        target.colorValue[1] = 0.055f;
        target.colorValue[2] = 0.09f;
        done(command->beginRendering(target));
        done(command->bindVertexBuffer(vertexBuffer));
        done(command->bindIndexBuffer(indexBuffer));
        for (size_t first = 0; first < items.size();) {
            size_t end = first + 1;
            if (items[first].mode != AlphaMode::Blend)
                while (end < items.size() && items[end].material == items[first].material &&
                       items[end].mode == items[first].mode)
                    ++end;
            done(command->bindPipeline(
                pipelineFor(color->desc.format, items[first].material->type(), items[first].mode)));
            done(command->bindGroup(groups[first]));
            done(command->drawIndexed(36, static_cast<uint32_t>(end - first), static_cast<uint32_t>(first)));
            first = end;
        }
        done(command->endRendering());
        done(command->barrier(color, rhi::ResourceState::ColorAttachment, after));
    }

    rhi::FencePtr draw(const rhi::TexturePtr &color, rhi::ResourceState before, rhi::ResourceState after,
                       const Mat4 &viewProjection, const Vector<ModelBoxInstance> &boxes, const SceneLighting &lighting,
                       Vec3 cameraPosition, const rhi::SwapchainPtr &swapchain = {}) {
        auto command = take(device->createCommandList());
        done(command->begin());
        record(command, color, before, after, viewProjection, boxes, lighting, cameraPosition);
        done(command->end());
        lastFence = take(device->submit(command, swapchain));
        return lastFence;
    }

    struct GpuTexture {
        MaterialTexturePtr source;
        rhi::TexturePtr texture;
    };

    struct GpuMaterialCache {
        std::weak_ptr<MaterialInstance> source;
        uint64_t version = 0;
        rhi::BufferPtr uniform;
        rhi::BindGroupPtr group;
    };

    rhi::DevicePtr device;
    std::shared_ptr<MaterialInstance> fallback;
    rhi::BufferPtr vertexBuffer;
    rhi::BufferPtr indexBuffer;
    rhi::BufferPtr sceneBuffer;
    rhi::LayoutPtr layout;
    rhi::ShaderPtr pbrVertexShader;
    rhi::ShaderPtr unlitVertexShader;
    rhi::ShaderPtr unlitShader;
    rhi::ShaderPtr pbrShader;
    rhi::TexturePtr depth;
    rhi::FencePtr lastFence;
    Map<std::tuple<int, int, int>, rhi::PipelinePtr> pipelines;
    UnorderedMap<const MaterialTexture *, GpuTexture> textureCache;
    UnorderedMap<const MaterialInstance *, GpuMaterialCache> materialCache;
    size_t capacity = 0;
};

Renderer3D::Renderer3D(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {
}

Renderer3D::~Renderer3D() = default;

Result<std::unique_ptr<Renderer3D>> Renderer3D::create(rhi::DevicePtr device) {
    try {
        if (!device)
            throw std::runtime_error("3D renderer requires a device");
        auto impl = std::make_unique<Impl>(std::move(device));
        impl->initialize();
        return Result<std::unique_ptr<Renderer3D>>::success(
            std::unique_ptr<Renderer3D>(new Renderer3D(std::move(impl))));
    } catch (const std::exception &error) {
        return Result<std::unique_ptr<Renderer3D>>::failure(error.what());
    }
}

Result<void> Renderer3D::render(const rhi::SwapchainPtr &swapchain, const Camera &camera,
                                const Vector<BoxInstance> &boxes) {
    try {
        if (!swapchain)
            throw std::runtime_error("3D renderer requires a swapchain");
        impl_->waitPrevious();
        const auto acquired = take(swapchain->acquire());
        if (!acquired.texture)
            return {};
        const float aspect =
            static_cast<float>(acquired.texture->desc.width) / static_cast<float>(acquired.texture->desc.height);
        impl_->draw(acquired.texture, acquired.initialState, rhi::ResourceState::Present, camera.viewProjection(aspect),
                    modelsFromBoxes(boxes), {}, camera.position, swapchain);
        done(swapchain->present());
        return {};
    } catch (const std::exception &error) {
        return Result<void>::failure(error.what());
    }
}

Result<void> Renderer3D::renderModels(const rhi::SwapchainPtr &swapchain, const Mat4 &viewProjection,
                                      const Vector<ModelBoxInstance> &boxes, const SceneLighting &lighting,
                                      Vec3 cameraPosition) {
    try {
        if (!swapchain)
            throw std::runtime_error("3D renderer requires a swapchain");
        impl_->waitPrevious();
        const auto acquired = take(swapchain->acquire());
        if (!acquired.texture)
            return {};
        impl_->draw(acquired.texture, acquired.initialState, rhi::ResourceState::Present, viewProjection, boxes,
                    lighting, cameraPosition, swapchain);
        done(swapchain->present());
        return {};
    } catch (const std::exception &error) {
        return Result<void>::failure(error.what());
    }
}

Result<rhi::FencePtr> Renderer3D::renderOffscreen(const rhi::TexturePtr &color, const Camera &camera,
                                                  const Vector<BoxInstance> &boxes) {
    try {
        impl_->waitPrevious();
        if (!color)
            throw std::runtime_error("3D renderer requires a color target");
        const float aspect = static_cast<float>(color->desc.width) / static_cast<float>(color->desc.height);
        return Result<rhi::FencePtr>::success(
            impl_->draw(color, rhi::ResourceState::Undefined, rhi::ResourceState::TransferSource,
                        camera.viewProjection(aspect), modelsFromBoxes(boxes), {}, camera.position));
    } catch (const std::exception &error) {
        return Result<rhi::FencePtr>::failure(error.what());
    }
}

Result<rhi::FencePtr> Renderer3D::renderOffscreenModels(const rhi::TexturePtr &color, const Mat4 &viewProjection,
                                                        const Vector<ModelBoxInstance> &boxes,
                                                        const SceneLighting &lighting, Vec3 cameraPosition) {
    try {
        impl_->waitPrevious();
        return Result<rhi::FencePtr>::success(impl_->draw(color, rhi::ResourceState::Undefined,
                                                          rhi::ResourceState::TransferSource, viewProjection, boxes,
                                                          lighting, cameraPosition));
    } catch (const std::exception &error) {
        return Result<rhi::FencePtr>::failure(error.what());
    }
}

Result<void> Renderer3D::recordOffscreenModels(const rhi::CommandListPtr &command, const rhi::TexturePtr &color,
                                               rhi::ResourceState initialState, rhi::ResourceState finalState,
                                               const Mat4 &viewProjection, const Vector<ModelBoxInstance> &boxes,
                                               const SceneLighting &lighting, Vec3 cameraPosition) {
    try {
        impl_->waitPrevious();
        impl_->record(command, color, initialState, finalState, viewProjection, boxes, lighting, cameraPosition);
        return {};
    } catch (const std::exception &error) {
        return Result<void>::failure(error.what());
    }
}
} // namespace ursa
