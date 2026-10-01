#include <Ursa/Application.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
template <class T> T take(ursa::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error);
    return std::move(result.value);
}

void done(ursa::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error);
}

std::vector<uint8_t> loadShader(const std::string &name) {
    std::ifstream file(std::string(URSA_SHADER_DIR) + "/" + name + ".spv", std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open shader " + name);
    const auto size = file.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(bytes.data()), size);
    if (!file)
        throw std::runtime_error("Cannot read shader " + name);
    return bytes;
}

class TriangleApp final : public ursa::Application {
    ursa::rhi::PipelinePtr pipeline_;
    bool stressWindow_ = false;
    uint32_t frame_ = 0;

    ursa::Result<void> onStart() override {
        const auto vertex = take(device_->createShader(
            {ursa::rhi::ShaderStage::Vertex, ursa::rhi::ShaderFormat::SPIRV, loadShader("triangle.vert")}));
        const auto fragment = take(device_->createShader(
            {ursa::rhi::ShaderStage::Fragment, ursa::rhi::ShaderFormat::SPIRV, loadShader("triangle.frag")}));
        ursa::rhi::GraphicsPipelineDesc desc;
        desc.vertex = vertex;
        desc.fragment = fragment;
        desc.colorFormat = swapchain_->format();
        pipeline_ = take(device_->createGraphicsPipeline(desc));
        pipelineFormat_ = desc.colorFormat;
        std::cout << "Adapter: " << device_->adapterInfo().name << '\n';
        return {};
    }

    ursa::Result<void> onFrame(float) override {
        if (stressWindow_ && ++frame_ == 5)
            window_->resize(800, 600);
        if (stressWindow_ && frame_ == 10) {
            window_->minimize();
            window_->restore();
        }
        if (window_->keyDown(ursa::Key::Escape))
            window_->requestClose();
        auto acquired = take(swapchain_->acquire());
        if (!acquired.texture)
            return {};
        if (pipelineFormat_ != swapchain_->format()) {
            const auto vertex = take(device_->createShader(
                {ursa::rhi::ShaderStage::Vertex, ursa::rhi::ShaderFormat::SPIRV, loadShader("triangle.vert")}));
            const auto fragment = take(device_->createShader(
                {ursa::rhi::ShaderStage::Fragment, ursa::rhi::ShaderFormat::SPIRV, loadShader("triangle.frag")}));
            ursa::rhi::GraphicsPipelineDesc desc;
            desc.vertex = vertex;
            desc.fragment = fragment;
            desc.colorFormat = swapchain_->format();
            pipeline_ = take(device_->createGraphicsPipeline(desc));
            pipelineFormat_ = desc.colorFormat;
        }
        auto cmd = take(device_->createCommandList());
        done(cmd->begin());
        done(cmd->barrier(acquired.texture, acquired.initialState, ursa::rhi::ResourceState::ColorAttachment));
        ursa::rhi::RenderTarget target;
        target.color = acquired.texture;
        done(cmd->beginRendering(target));
        done(cmd->bindPipeline(pipeline_));
        done(cmd->draw(3));
        done(cmd->endRendering());
        done(cmd->barrier(acquired.texture, ursa::rhi::ResourceState::ColorAttachment,
                          ursa::rhi::ResourceState::Present));
        done(cmd->end());
        take(device_->submit(cmd, swapchain_));
        done(swapchain_->present());
        return {};
    }

    void onStop() override {
        pipeline_.reset();
    }

    ursa::rhi::Format pipelineFormat_ = ursa::rhi::Format::BGRA8Unorm;

public:
    explicit TriangleApp(bool stressWindow) : stressWindow_(stressWindow) {
    }
};
} // namespace

int main(int argc, char **argv) {
    try {
        bool visible = true;
        bool stressWindow = false;
        uint32_t frames = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--hidden")
                visible = false;
            else if (arg == "--stress-window")
                stressWindow = true;
            else if (arg == "--frames" && i + 1 < argc)
                frames = static_cast<uint32_t>(std::stoul(argv[++i]));
            else
                throw std::runtime_error("Usage: UrsaTriangle [--hidden] [--frames N] [--stress-window]");
        }
        TriangleApp app(stressWindow);
        auto result = app.run(1280, 720, "UrsaEngine", visible, frames);
        if (!result)
            throw std::runtime_error(result.error);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "UrsaTriangle: " << e.what() << '\n';
        return 1;
    }
}
