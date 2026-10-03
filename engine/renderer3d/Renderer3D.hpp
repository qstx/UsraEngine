#pragma once
#include <core/Containers.hpp>
#include <material/Color.hpp>
#include <material/Material.hpp>
#include <math/Camera.hpp>
#include <math/Transform.hpp>
#include <rhi/RHI.hpp>
#include <scene/Lighting.hpp>
#include <memory>
#include <vector>

namespace ursa {
struct BoxInstance {
    Transform transform;
    Color color;
    std::shared_ptr<MaterialInstance> material;
};

struct ModelBoxInstance {
    Mat4 model;
    Color color;
    std::shared_ptr<MaterialInstance> material;
};

class Renderer3D final {
public:
    static Result<std::unique_ptr<Renderer3D>> create(rhi::DevicePtr device);

    ~Renderer3D();

    Renderer3D(const Renderer3D &) = delete;

    Renderer3D &operator=(const Renderer3D &) = delete;

    // Acquires, renders, submits, and presents one swapchain frame.
    Result<void> render(const rhi::SwapchainPtr &swapchain, const Camera &camera, const Vector<BoxInstance> &boxes);

    Result<void> renderModels(const rhi::SwapchainPtr &swapchain, const Mat4 &viewProjection,
                              const Vector<ModelBoxInstance> &boxes, const SceneLighting &lighting = {},
                              Vec3 cameraPosition = {});

    // Leaves an offscreen color target in TransferSource state for readback.
    Result<rhi::FencePtr> renderOffscreen(const rhi::TexturePtr &color, const Camera &camera,
                                          const Vector<BoxInstance> &boxes);

    Result<rhi::FencePtr> renderOffscreenModels(const rhi::TexturePtr &color, const Mat4 &viewProjection,
                                                const Vector<ModelBoxInstance> &boxes,
                                                const SceneLighting &lighting = {}, Vec3 cameraPosition = {});

    // Record a scene pass in an existing command list. The caller submits and waits for its fence
    // before recording another frame or releasing this renderer.
    Result<void> recordOffscreenModels(const rhi::CommandListPtr &command, const rhi::TexturePtr &color,
                                       rhi::ResourceState initialState, rhi::ResourceState finalState,
                                       const Mat4 &viewProjection, const Vector<ModelBoxInstance> &boxes,
                                       const SceneLighting &lighting = {}, Vec3 cameraPosition = {});

private:
    struct Impl;

    explicit Renderer3D(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};
} // namespace ursa
