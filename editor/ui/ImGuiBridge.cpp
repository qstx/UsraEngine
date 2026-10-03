#include <ui/ImGuiBridge.hpp>
#include <core/StringInterop.hpp>
#include <vfs/AssetRoots.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace ursa::editor {
namespace {
struct GuiVertex {
    float position[2];
    float uv[2];
    float color[4];
};

ImGuiKey toImGuiKey(Key key) {
    switch (key) {
    case Key::Tab: return ImGuiKey_Tab;
    case Key::Left: return ImGuiKey_LeftArrow;
    case Key::Right: return ImGuiKey_RightArrow;
    case Key::Up: return ImGuiKey_UpArrow;
    case Key::Down: return ImGuiKey_DownArrow;
    case Key::PageUp: return ImGuiKey_PageUp;
    case Key::PageDown: return ImGuiKey_PageDown;
    case Key::Home: return ImGuiKey_Home;
    case Key::End: return ImGuiKey_End;
    case Key::Insert: return ImGuiKey_Insert;
    case Key::Delete: return ImGuiKey_Delete;
    case Key::Backspace: return ImGuiKey_Backspace;
    case Key::Space: return ImGuiKey_Space;
    case Key::Enter: return ImGuiKey_Enter;
    case Key::Escape: return ImGuiKey_Escape;
    case Key::LeftShift: return ImGuiKey_LeftShift;
    case Key::RightShift: return ImGuiKey_RightShift;
    case Key::LeftControl: return ImGuiKey_LeftCtrl;
    case Key::RightControl: return ImGuiKey_RightCtrl;
    case Key::LeftAlt: return ImGuiKey_LeftAlt;
    case Key::RightAlt: return ImGuiKey_RightAlt;
    case Key::LeftSuper: return ImGuiKey_LeftSuper;
    case Key::RightSuper: return ImGuiKey_RightSuper;
    case Key::A: return ImGuiKey_A;
    case Key::B: return ImGuiKey_B;
    case Key::C: return ImGuiKey_C;
    case Key::D: return ImGuiKey_D;
    case Key::E: return ImGuiKey_E;
    case Key::F: return ImGuiKey_F;
    case Key::G: return ImGuiKey_G;
    case Key::H: return ImGuiKey_H;
    case Key::I: return ImGuiKey_I;
    case Key::J: return ImGuiKey_J;
    case Key::K: return ImGuiKey_K;
    case Key::L: return ImGuiKey_L;
    case Key::M: return ImGuiKey_M;
    case Key::N: return ImGuiKey_N;
    case Key::O: return ImGuiKey_O;
    case Key::P: return ImGuiKey_P;
    case Key::Q: return ImGuiKey_Q;
    case Key::R: return ImGuiKey_R;
    case Key::S: return ImGuiKey_S;
    case Key::T: return ImGuiKey_T;
    case Key::U: return ImGuiKey_U;
    case Key::V: return ImGuiKey_V;
    case Key::W: return ImGuiKey_W;
    case Key::X: return ImGuiKey_X;
    case Key::Y: return ImGuiKey_Y;
    case Key::Z: return ImGuiKey_Z;
    default: break;
    }
    if (key >= Key::Digit0 && key <= Key::Digit9)
        return static_cast<ImGuiKey>(ImGuiKey_0 + static_cast<int>(key) - static_cast<int>(Key::Digit0));
    return ImGuiKey_None;
}

Result<Vector<uint8_t>> readShader(const VirtualFileSystem &files, const char *name) {
    auto path = VirtualPath::parse(String(engineScheme) + ":shaders/ui/" + name + ".spv");
    if (!path)
        return Result<Vector<uint8_t>>::failure(path.error);
    auto bytes = files.read(path.value);
    if (!bytes)
        return Result<Vector<uint8_t>>::failure(bytes.error);
    if (bytes.value.empty())
        return Result<Vector<uint8_t>>::failure(String("Empty Editor shader: ") + name);
    return bytes;
}
} // namespace

ImGuiBridge::ImGuiBridge(rhi::DevicePtr device, std::shared_ptr<Window> window)
    : device_(std::move(device)), window_(std::move(window)) {
}

Result<std::unique_ptr<ImGuiBridge>> ImGuiBridge::create(rhi::DevicePtr device, std::shared_ptr<Window> window) {
    if (!device)
        return Result<std::unique_ptr<ImGuiBridge>>::failure("ImGui RHI bridge requires a device");
    auto bridge = std::unique_ptr<ImGuiBridge>(new ImGuiBridge(std::move(device), std::move(window)));
    auto initialized = bridge->initialize();
    if (!initialized)
        return Result<std::unique_ptr<ImGuiBridge>>::failure(initialized.error);
    return Result<std::unique_ptr<ImGuiBridge>>::success(std::move(bridge));
}

Result<void> ImGuiBridge::initialize() {
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);
    auto &io = ImGui::GetIO();
    io.BackendRendererName = "Ursa RHI";
    io.BackendPlatformName = "Ursa Window";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    if (window_) {
        auto &platform = ImGui::GetPlatformIO();
        platform.Platform_ClipboardUserData = this;
        platform.Platform_GetClipboardTextFn = &ImGuiBridge::getClipboardText;
        platform.Platform_SetClipboardTextFn = &ImGuiBridge::setClipboardText;
    }
    auto layout = device_->createBindGroupLayout({{0, rhi::BindingType::CombinedImageSampler, 1, 2}});
    if (!layout)
        return Result<void>::failure(layout.error);
    layout_ = std::move(layout.value);
    auto engineFiles = openEngineFileSystem();
    if (!engineFiles)
        return Result<void>::failure(engineFiles.error);
    auto vertexCode = readShader(engineFiles.value, "imgui.vert");
    if (!vertexCode)
        return Result<void>::failure(vertexCode.error);
    auto fragmentCode = readShader(engineFiles.value, "imgui.frag");
    if (!fragmentCode)
        return Result<void>::failure(fragmentCode.error);
    auto vertex = device_->createShader({rhi::ShaderStage::Vertex, rhi::ShaderFormat::SPIRV,
                                         std::move(vertexCode.value)});
    if (!vertex)
        return Result<void>::failure(vertex.error);
    vertexShader_ = std::move(vertex.value);
    auto fragment = device_->createShader({rhi::ShaderStage::Fragment, rhi::ShaderFormat::SPIRV,
                                           std::move(fragmentCode.value)});
    if (!fragment)
        return Result<void>::failure(fragment.error);
    fragmentShader_ = std::move(fragment.value);
    auto sampler = device_->createSampler({true, false});
    if (!sampler)
        return Result<void>::failure(sampler.error);
    atlasSampler_ = std::move(sampler.value);
    if (window_) {
        auto subscription = window_->subscribe([this](const WindowEvent &event) {
            ImGui::SetCurrentContext(context_);
            auto &input = ImGui::GetIO();
            switch (event.type) {
            case WindowEventType::KeyPressed:
            case WindowEventType::KeyReleased: {
                const bool pressed = event.type == WindowEventType::KeyPressed;
                if (const auto key = toImGuiKey(event.key); key != ImGuiKey_None)
                    input.AddKeyEvent(key, pressed);
                switch (event.key) {
                case Key::LeftControl:
                    leftControl_ = pressed;
                    input.AddKeyEvent(ImGuiMod_Ctrl, leftControl_ || rightControl_);
                    break;
                case Key::RightControl:
                    rightControl_ = pressed;
                    input.AddKeyEvent(ImGuiMod_Ctrl, leftControl_ || rightControl_);
                    break;
                case Key::LeftShift:
                    leftShift_ = pressed;
                    input.AddKeyEvent(ImGuiMod_Shift, leftShift_ || rightShift_);
                    break;
                case Key::RightShift:
                    rightShift_ = pressed;
                    input.AddKeyEvent(ImGuiMod_Shift, leftShift_ || rightShift_);
                    break;
                case Key::LeftAlt:
                    leftAlt_ = pressed;
                    input.AddKeyEvent(ImGuiMod_Alt, leftAlt_ || rightAlt_);
                    break;
                case Key::RightAlt:
                    rightAlt_ = pressed;
                    input.AddKeyEvent(ImGuiMod_Alt, leftAlt_ || rightAlt_);
                    break;
                case Key::LeftSuper:
                    leftSuper_ = pressed;
                    input.AddKeyEvent(ImGuiMod_Super, leftSuper_ || rightSuper_);
                    break;
                case Key::RightSuper:
                    rightSuper_ = pressed;
                    input.AddKeyEvent(ImGuiMod_Super, leftSuper_ || rightSuper_);
                    break;
                default:
                    break;
                }
                break;
            }
            case WindowEventType::KeyRepeated:
                break;
            case WindowEventType::Character:
                input.AddInputCharacter(event.codepoint);
                break;
            case WindowEventType::CursorMoved:
                input.AddMousePosEvent(static_cast<float>(event.x), static_cast<float>(event.y));
                break;
            case WindowEventType::MousePressed:
            case WindowEventType::MouseReleased:
                if (event.button != MouseButton::Other)
                    input.AddMouseButtonEvent(static_cast<int>(event.button),
                                              event.type == WindowEventType::MousePressed);
                break;
            case WindowEventType::Scrolled:
                input.AddMouseWheelEvent(static_cast<float>(event.x), static_cast<float>(event.y));
                break;
            case WindowEventType::Focused:
                input.AddFocusEvent(event.focused);
                if (!event.focused) {
                    leftControl_ = rightControl_ = false;
                    leftShift_ = rightShift_ = false;
                    leftAlt_ = rightAlt_ = false;
                    leftSuper_ = rightSuper_ = false;
                }
                break;
            }
        });
        if (!subscription)
            return Result<void>::failure(subscription.error);
        subscription_ = subscription.value;
    }
    return {};
}

ImGuiBridge::~ImGuiBridge() {
    if (window_ && subscription_)
        window_->unsubscribe(subscription_);
    if (context_) {
        ImGui::SetCurrentContext(context_);
        ImGui::DestroyContext(context_);
    }
}

const char *ImGuiBridge::getClipboardText(ImGuiContext *context) {
    ImGui::SetCurrentContext(context);
    auto *bridge = static_cast<ImGuiBridge *>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
    if (!bridge || !bridge->window_)
        return nullptr;
    bridge->clipboardText_ = bridge->window_->clipboardText();
    return bridge->clipboardText_.c_str();
}

void ImGuiBridge::setClipboardText(ImGuiContext *context, const char *text) {
    ImGui::SetCurrentContext(context);
    auto *bridge = static_cast<ImGuiBridge *>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
    if (bridge && bridge->window_)
        bridge->window_->setClipboardText(text ? String(text) : String{});
}

Result<void> ImGuiBridge::beginFrame(float deltaSeconds, uint32_t width, uint32_t height) {
    if (!width || !height)
        return Result<void>::failure("ImGui frame size must be positive");
    ImGui::SetCurrentContext(context_);
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.DeltaTime = std::max(deltaSeconds, 0.00001f);
    ImGui::NewFrame();
    return {};
}

Result<void> ImGuiBridge::endFrame() {
    ImGui::SetCurrentContext(context_);
    ImGui::Render();
    return updateAtlas();
}

Result<ImTextureID> ImGuiBridge::registerTexture(rhi::TexturePtr texture, rhi::SamplerPtr sampler) {
    if (!texture || !sampler)
        return Result<ImTextureID>::failure("ImGui image requires a texture and sampler");
    if (!rhi::has(texture->desc.usage, rhi::TextureUsage::Sampled))
        return Result<ImTextureID>::failure("ImGui image texture must have Sampled usage");
    auto group = device_->createBindGroup({layout_, {{0, {}, std::move(texture), std::move(sampler)}}});
    if (!group)
        return Result<ImTextureID>::failure(group.error);
    const ImTextureID id = nextTextureId_++;
    groups_.emplace(id, std::move(group.value));
    return Result<ImTextureID>::success(id);
}

void ImGuiBridge::unregisterTexture(ImTextureID id) {
    groups_.erase(id);
}

Result<void> ImGuiBridge::uploadAtlas(ImTextureData *data) {
    if (!data || data->Width <= 0 || data->Height <= 0 || !data->Pixels)
        return Result<void>::failure("ImGui requested an invalid font atlas");
    const auto width = static_cast<uint32_t>(data->Width);
    const auto height = static_cast<uint32_t>(data->Height);
    if (width > std::numeric_limits<size_t>::max() / 4 / height)
        return Result<void>::failure("ImGui font atlas size overflow");
    const size_t byteCount = static_cast<size_t>(width) * height * 4;
    std::vector<uint8_t> rgba(byteCount);
    if (data->Format == ImTextureFormat_RGBA32)
        std::memcpy(rgba.data(), data->GetPixels(), byteCount);
    else if (data->Format == ImTextureFormat_Alpha8) {
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = data->Pixels[i];
        }
    } else
        return Result<void>::failure("Unsupported ImGui font atlas format");
    auto upload = device_->createBuffer({byteCount, rhi::BufferUsage::TransferSource, rhi::Memory::Upload});
    if (!upload)
        return Result<void>::failure(upload.error);
    auto written = upload.value->write(0, rgba.data(), byteCount);
    if (!written)
        return written;
    auto texture = device_->createTexture({width, height, rhi::Format::RGBA8Unorm,
                                           rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination});
    if (!texture)
        return Result<void>::failure(texture.error);
    auto command = device_->createCommandList();
    if (!command)
        return Result<void>::failure(command.error);
    auto result = command.value->begin();
    if (result)
        result = command.value->barrier(texture.value, rhi::ResourceState::Undefined,
                                        rhi::ResourceState::TransferDestination);
    if (result)
        result = command.value->copyBufferToTexture(upload.value, texture.value);
    if (result)
        result = command.value->barrier(texture.value, rhi::ResourceState::TransferDestination,
                                        rhi::ResourceState::ShaderRead);
    if (result)
        result = command.value->end();
    if (!result)
        return result;
    auto submitted = device_->submit(command.value);
    if (!submitted)
        return Result<void>::failure(submitted.error);
    result = submitted.value->wait();
    if (!result)
        return result;
    auto registered = registerTexture(std::move(texture.value), atlasSampler_);
    if (!registered)
        return Result<void>::failure(registered.error);
    if (auto found = atlasIds_.find(data); found != atlasIds_.end())
        unregisterTexture(found->second);
    atlasIds_[data] = registered.value;
    data->SetTexID(registered.value);
    data->SetStatus(ImTextureStatus_OK);
    return {};
}

Result<void> ImGuiBridge::updateAtlas() {
    auto *drawData = ImGui::GetDrawData();
    if (!drawData || !drawData->Textures)
        return {};
    for (ImTextureData *data : *drawData->Textures) {
        if (data->Status == ImTextureStatus_WantCreate || data->Status == ImTextureStatus_WantUpdates) {
            auto uploaded = uploadAtlas(data);
            if (!uploaded)
                return uploaded;
        } else if (data->Status == ImTextureStatus_WantDestroy) {
            if (auto found = atlasIds_.find(data); found != atlasIds_.end()) {
                unregisterTexture(found->second);
                atlasIds_.erase(found);
            }
            data->SetTexID(ImTextureID_Invalid);
            data->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    return {};
}

Result<void> ImGuiBridge::record(const rhi::CommandListPtr &command, rhi::Format colorFormat,
                                 uint32_t framebufferWidth, uint32_t framebufferHeight) {
    if (!command || !framebufferWidth || !framebufferHeight)
        return Result<void>::failure("ImGui record requires a command list and nonzero framebuffer");
    ImGui::SetCurrentContext(context_);
    const ImDrawData *data = ImGui::GetDrawData();
    if (!data || data->TotalVtxCount == 0 || data->TotalIdxCount == 0)
        return {};
    const int formatKey = static_cast<int>(colorFormat);
    auto pipeline = pipelines_.find(formatKey);
    if (pipeline == pipelines_.end()) {
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = vertexShader_;
        desc.fragment = fragmentShader_;
        desc.layout = layout_;
        desc.colorFormat = colorFormat;
        desc.blend = true;
        desc.cullBack = false;
        desc.vertexStride = sizeof(GuiVertex);
        desc.attributes = {{0, 0, rhi::VertexFormat::Float2}, {1, 8, rhi::VertexFormat::Float2},
                           {2, 16, rhi::VertexFormat::Float4}};
        auto created = device_->createGraphicsPipeline(desc);
        if (!created)
            return Result<void>::failure(created.error);
        pipeline = pipelines_.emplace(formatKey, std::move(created.value)).first;
    }
    std::vector<GuiVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(data->TotalVtxCount));
    indices.reserve(static_cast<size_t>(data->TotalIdxCount));
    for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex) {
        const ImDrawList *list = data->CmdLists[listIndex];
        for (const ImDrawVert &source : list->VtxBuffer) {
            GuiVertex vertex{};
            vertex.position[0] = (source.pos.x - data->DisplayPos.x) / data->DisplaySize.x * 2.f - 1.f;
            vertex.position[1] = 1.f - (source.pos.y - data->DisplayPos.y) / data->DisplaySize.y * 2.f;
            vertex.uv[0] = source.uv.x;
            vertex.uv[1] = source.uv.y;
            for (int channel = 0; channel < 4; ++channel)
                vertex.color[channel] = static_cast<float>((source.col >> (channel * 8)) & 255u) / 255.f;
            vertices.push_back(vertex);
        }
        for (ImDrawIdx index : list->IdxBuffer)
            indices.push_back(static_cast<uint32_t>(index));
    }
    auto vertexBuffer = device_->createBuffer({vertices.size() * sizeof(GuiVertex), rhi::BufferUsage::Vertex,
                                                rhi::Memory::Upload});
    if (!vertexBuffer)
        return Result<void>::failure(vertexBuffer.error);
    auto indexBuffer = device_->createBuffer({indices.size() * sizeof(uint32_t), rhi::BufferUsage::Index,
                                               rhi::Memory::Upload});
    if (!indexBuffer)
        return Result<void>::failure(indexBuffer.error);
    auto result = vertexBuffer.value->write(0, vertices.data(), vertices.size() * sizeof(GuiVertex));
    if (result)
        result = indexBuffer.value->write(0, indices.data(), indices.size() * sizeof(uint32_t));
    if (!result)
        return result;
    vertexBuffer_ = std::move(vertexBuffer.value);
    indexBuffer_ = std::move(indexBuffer.value);
    result = command->bindPipeline(pipeline->second);
    if (result)
        result = command->bindVertexBuffer(vertexBuffer_);
    if (result)
        result = command->bindIndexBuffer(indexBuffer_);
    if (!result)
        return result;
    uint32_t globalVertex = 0;
    uint32_t globalIndex = 0;
    for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex) {
        const ImDrawList *list = data->CmdLists[listIndex];
        for (const ImDrawCmd &draw : list->CmdBuffer) {
            if (draw.UserCallback) {
                if (draw.UserCallback == ImDrawCallback_ResetRenderState) {
                    result = command->bindPipeline(pipeline->second);
                    if (result)
                        result = command->bindVertexBuffer(vertexBuffer_);
                    if (result)
                        result = command->bindIndexBuffer(indexBuffer_);
                } else
                    draw.UserCallback(list, &draw);
                if (!result)
                    return result;
                continue;
            }
            const float minX = (draw.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x;
            const float minY = (draw.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y;
            const float maxX = (draw.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x;
            const float maxY = (draw.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y;
            const int32_t left = std::max<int32_t>(0, static_cast<int32_t>(std::floor(minX)));
            const int32_t top = std::max<int32_t>(0, static_cast<int32_t>(std::floor(minY)));
            const int32_t right = std::min<int32_t>(static_cast<int32_t>(framebufferWidth),
                                                     static_cast<int32_t>(std::ceil(maxX)));
            const int32_t bottom = std::min<int32_t>(static_cast<int32_t>(framebufferHeight),
                                                      static_cast<int32_t>(std::ceil(maxY)));
            if (right <= left || bottom <= top || draw.ElemCount == 0)
                continue;
            const auto group = groups_.find(draw.GetTexID());
            if (group == groups_.end())
                return Result<void>::failure("ImGui draw references an unregistered texture");
            result = command->bindGroup(group->second);
            if (result)
                result = command->setScissor({left, top, static_cast<uint32_t>(right - left),
                                              static_cast<uint32_t>(bottom - top)});
            if (result)
                result = command->drawIndexed(draw.ElemCount, 1, 0, globalIndex + draw.IdxOffset,
                                               static_cast<int32_t>(globalVertex + draw.VtxOffset));
            if (!result)
                return result;
        }
        globalVertex += static_cast<uint32_t>(list->VtxBuffer.Size);
        globalIndex += static_cast<uint32_t>(list->IdxBuffer.Size);
    }
    return {};
}
} // namespace ursa::editor
