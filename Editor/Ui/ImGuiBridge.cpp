#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace Ursa::Editor
{
namespace
{
struct GuiVertex
{
	Ursa::Float32 position[2];
	Ursa::Float32 uv[2];
	Ursa::Float32 color[4];
};

ImGuiKey ToImGuiKey(Key key)
{
	switch (key)
	{
		case Key::Tab:
			return ImGuiKey_Tab;
		case Key::Left:
			return ImGuiKey_LeftArrow;
		case Key::Right:
			return ImGuiKey_RightArrow;
		case Key::Up:
			return ImGuiKey_UpArrow;
		case Key::Down:
			return ImGuiKey_DownArrow;
		case Key::PageUp:
			return ImGuiKey_PageUp;
		case Key::PageDown:
			return ImGuiKey_PageDown;
		case Key::Home:
			return ImGuiKey_Home;
		case Key::End:
			return ImGuiKey_End;
		case Key::Insert:
			return ImGuiKey_Insert;
		case Key::Delete:
			return ImGuiKey_Delete;
		case Key::Backspace:
			return ImGuiKey_Backspace;
		case Key::Space:
			return ImGuiKey_Space;
		case Key::Enter:
			return ImGuiKey_Enter;
		case Key::Escape:
			return ImGuiKey_Escape;
		case Key::LeftShift:
			return ImGuiKey_LeftShift;
		case Key::RightShift:
			return ImGuiKey_RightShift;
		case Key::LeftControl:
			return ImGuiKey_LeftCtrl;
		case Key::RightControl:
			return ImGuiKey_RightCtrl;
		case Key::LeftAlt:
			return ImGuiKey_LeftAlt;
		case Key::RightAlt:
			return ImGuiKey_RightAlt;
		case Key::LeftSuper:
			return ImGuiKey_LeftSuper;
		case Key::RightSuper:
			return ImGuiKey_RightSuper;
		case Key::A:
			return ImGuiKey_A;
		case Key::B:
			return ImGuiKey_B;
		case Key::C:
			return ImGuiKey_C;
		case Key::D:
			return ImGuiKey_D;
		case Key::E:
			return ImGuiKey_E;
		case Key::F:
			return ImGuiKey_F;
		case Key::G:
			return ImGuiKey_G;
		case Key::H:
			return ImGuiKey_H;
		case Key::I:
			return ImGuiKey_I;
		case Key::J:
			return ImGuiKey_J;
		case Key::K:
			return ImGuiKey_K;
		case Key::L:
			return ImGuiKey_L;
		case Key::M:
			return ImGuiKey_M;
		case Key::N:
			return ImGuiKey_N;
		case Key::O:
			return ImGuiKey_O;
		case Key::P:
			return ImGuiKey_P;
		case Key::Q:
			return ImGuiKey_Q;
		case Key::R:
			return ImGuiKey_R;
		case Key::S:
			return ImGuiKey_S;
		case Key::T:
			return ImGuiKey_T;
		case Key::U:
			return ImGuiKey_U;
		case Key::V:
			return ImGuiKey_V;
		case Key::W:
			return ImGuiKey_W;
		case Key::X:
			return ImGuiKey_X;
		case Key::Y:
			return ImGuiKey_Y;
		case Key::Z:
			return ImGuiKey_Z;
		default:
			break;
	}
	if (key >= Key::Digit0 && key <= Key::Digit9)
	{
		return static_cast<ImGuiKey>(ImGuiKey_0 + static_cast<int>(key) - static_cast<int>(Key::Digit0));
	}
	return ImGuiKey_None;
}

Result<Vector<Ursa::UInt8>> ReadShader(const VirtualFileSystem& files, const char* name)
{
	auto path = VirtualPath::Parse(String(engineScheme) + ":Shaders/Ui/" + name + ".spv");
	if (!path)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(path.error);
	}
	auto bytes = files.Read(path.value);
	if (!bytes)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(bytes.error);
	}
	if (bytes.value.empty())
	{
		return Result<Vector<Ursa::UInt8>>::Failure(String("Empty Editor shader: ") + name);
	}
	return bytes;
}
} // namespace

ImGuiBridge::ImGuiBridge(Rhi::DevicePtr selectedDevice, std::shared_ptr<Window> selectedWindow)
    : device(std::move(selectedDevice)), window(std::move(selectedWindow))
{
}

Result<std::unique_ptr<ImGuiBridge>> ImGuiBridge::Create(Rhi::DevicePtr selectedDevice,
                                                         std::shared_ptr<Window> selectedWindow)
{
	if (!selectedDevice)
	{
		return Result<std::unique_ptr<ImGuiBridge>>::Failure("ImGui RHI bridge requires a device");
	}
	auto bridge = std::unique_ptr<ImGuiBridge>(new ImGuiBridge(std::move(selectedDevice), std::move(selectedWindow)));
	auto initialized = bridge->Initialize();
	if (!initialized)
	{
		return Result<std::unique_ptr<ImGuiBridge>>::Failure(initialized.error);
	}
	return Result<std::unique_ptr<ImGuiBridge>>::Success(std::move(bridge));
}

Result<void> ImGuiBridge::Initialize()
{
	IMGUI_CHECKVERSION();
	context = ImGui::CreateContext();
	ImGui::SetCurrentContext(context);
	auto& io = ImGui::GetIO();
	io.BackendRendererName = "Ursa RHI";
	io.BackendPlatformName = "Ursa Window";
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.IniFilename = nullptr;
	if (window)
	{
		auto& platform = ImGui::GetPlatformIO();
		platform.Platform_ClipboardUserData = this;
		platform.Platform_GetClipboardTextFn = &ImGuiBridge::GetClipboardText;
		platform.Platform_SetClipboardTextFn = &ImGuiBridge::SetClipboardText;
	}
	auto layoutResult = device->CreateBindGroupLayout({{0, Rhi::BindingType::CombinedImageSampler, 1, 2}});
	if (!layoutResult)
	{
		return Result<void>::Failure(layoutResult.error);
	}
	layout = std::move(layoutResult.value);
	auto engineFiles = OpenEngineFileSystem();
	if (!engineFiles)
	{
		return Result<void>::Failure(engineFiles.error);
	}
	auto vertexCode = ReadShader(engineFiles.value, "ImGui.vert");
	if (!vertexCode)
	{
		return Result<void>::Failure(vertexCode.error);
	}
	auto fragmentCode = ReadShader(engineFiles.value, "ImGui.frag");
	if (!fragmentCode)
	{
		return Result<void>::Failure(fragmentCode.error);
	}
	auto vertex =
	    device->CreateShader({Rhi::ShaderStage::Vertex, Rhi::ShaderFormat::Spirv, std::move(vertexCode.value)});
	if (!vertex)
	{
		return Result<void>::Failure(vertex.error);
	}
	vertexShader = std::move(vertex.value);
	auto fragment =
	    device->CreateShader({Rhi::ShaderStage::Fragment, Rhi::ShaderFormat::Spirv, std::move(fragmentCode.value)});
	if (!fragment)
	{
		return Result<void>::Failure(fragment.error);
	}
	fragmentShader = std::move(fragment.value);
	auto sampler = device->CreateSampler({true, false});
	if (!sampler)
	{
		return Result<void>::Failure(sampler.error);
	}
	atlasSampler = std::move(sampler.value);
	if (window)
	{
		auto subscriptionResult = window->Subscribe(
		    [this](const WindowEvent& event)
		    {
			    ImGui::SetCurrentContext(context);
			    auto& input = ImGui::GetIO();
			    switch (event.type)
			    {
				    case WindowEventType::KeyPressed:
				    case WindowEventType::KeyReleased:
				    {
					    const bool pressed = event.type == WindowEventType::KeyPressed;
					    if (const auto key = ToImGuiKey(event.key); key != ImGuiKey_None)
					    {
						    input.AddKeyEvent(key, pressed);
					    }
					    switch (event.key)
					    {
						    case Key::LeftControl:
							    leftControl = pressed;
							    input.AddKeyEvent(ImGuiMod_Ctrl, leftControl || rightControl);
							    break;
						    case Key::RightControl:
							    rightControl = pressed;
							    input.AddKeyEvent(ImGuiMod_Ctrl, leftControl || rightControl);
							    break;
						    case Key::LeftShift:
							    leftShift = pressed;
							    input.AddKeyEvent(ImGuiMod_Shift, leftShift || rightShift);
							    break;
						    case Key::RightShift:
							    rightShift = pressed;
							    input.AddKeyEvent(ImGuiMod_Shift, leftShift || rightShift);
							    break;
						    case Key::LeftAlt:
							    leftAlt = pressed;
							    input.AddKeyEvent(ImGuiMod_Alt, leftAlt || rightAlt);
							    break;
						    case Key::RightAlt:
							    rightAlt = pressed;
							    input.AddKeyEvent(ImGuiMod_Alt, leftAlt || rightAlt);
							    break;
						    case Key::LeftSuper:
							    leftSuper = pressed;
							    input.AddKeyEvent(ImGuiMod_Super, leftSuper || rightSuper);
							    break;
						    case Key::RightSuper:
							    rightSuper = pressed;
							    input.AddKeyEvent(ImGuiMod_Super, leftSuper || rightSuper);
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
					    input.AddMousePosEvent(static_cast<Ursa::Float32>(event.x),
					                           static_cast<Ursa::Float32>(event.y));
					    break;
				    case WindowEventType::MousePressed:
				    case WindowEventType::MouseReleased:
					    if (event.button != MouseButton::Other)
					    {
						    input.AddMouseButtonEvent(static_cast<int>(event.button),
						                              event.type == WindowEventType::MousePressed);
					    }
					    break;
				    case WindowEventType::Scrolled:
					    input.AddMouseWheelEvent(static_cast<Ursa::Float32>(event.x),
					                             static_cast<Ursa::Float32>(event.y));
					    break;
				    case WindowEventType::Focused:
					    input.AddFocusEvent(event.focused);
					    if (!event.focused)
					    {
						    leftControl = rightControl = false;
						    leftShift = rightShift = false;
						    leftAlt = rightAlt = false;
						    leftSuper = rightSuper = false;
					    }
					    break;
			    }
		    });
		if (!subscriptionResult)
		{
			return Result<void>::Failure(subscriptionResult.error);
		}
		subscription = subscriptionResult.value;
	}
	return {};
}

ImGuiBridge::~ImGuiBridge()
{
	if (window && subscription)
	{
		window->Unsubscribe(subscription);
	}
	if (context)
	{
		ImGui::SetCurrentContext(context);
		ImGui::DestroyContext(context);
	}
}

const char* ImGuiBridge::GetClipboardText(ImGuiContext* guiContext)
{
	ImGui::SetCurrentContext(guiContext);
	auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
	if (!bridge || !bridge->window)
	{
		return nullptr;
	}
	bridge->clipboardText = bridge->window->ClipboardText();
	return bridge->clipboardText.c_str();
}

void ImGuiBridge::SetClipboardText(ImGuiContext* guiContext, const char* text)
{
	ImGui::SetCurrentContext(guiContext);
	auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
	if (bridge && bridge->window)
	{
		bridge->window->SetClipboardText(text ? String(text) : String{});
	}
}

Result<void> ImGuiBridge::BeginFrame(Ursa::Float32 deltaSeconds, Ursa::UInt32 width, Ursa::UInt32 height)
{
	if (!width || !height)
	{
		return Result<void>::Failure("ImGui frame size must be positive");
	}
	ImGui::SetCurrentContext(context);
	auto& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(static_cast<Ursa::Float32>(width), static_cast<Ursa::Float32>(height));
	io.DisplayFramebufferScale = ImVec2(1, 1);
	io.DeltaTime = std::max(deltaSeconds, 0.00001f);
	ImGui::NewFrame();
	return {};
}

Result<void> ImGuiBridge::EndFrame()
{
	ImGui::SetCurrentContext(context);
	ImGui::Render();
	return UpdateAtlas();
}

Result<ImTextureID> ImGuiBridge::RegisterTexture(Rhi::TexturePtr texture, Rhi::SamplerPtr sampler)
{
	if (!texture || !sampler)
	{
		return Result<ImTextureID>::Failure("ImGui image requires a texture and sampler");
	}
	if (!Rhi::Has(texture->desc.usage, Rhi::TextureUsage::Sampled))
	{
		return Result<ImTextureID>::Failure("ImGui image texture must have Sampled usage");
	}
	auto group = device->CreateBindGroup({layout, {{0, {}, std::move(texture), std::move(sampler)}}});
	if (!group)
	{
		return Result<ImTextureID>::Failure(group.error);
	}
	const ImTextureID id = nextTextureId++;
	groups.emplace(id, std::move(group.value));
	return Result<ImTextureID>::Success(id);
}

void ImGuiBridge::UnregisterTexture(ImTextureID id)
{
	groups.erase(id);
}

Result<void> ImGuiBridge::UploadAtlas(ImTextureData* data)
{
	if (!data || data->Width <= 0 || data->Height <= 0 || !data->Pixels)
	{
		return Result<void>::Failure("ImGui requested an invalid font atlas");
	}
	const auto width = static_cast<Ursa::UInt32>(data->Width);
	const auto height = static_cast<Ursa::UInt32>(data->Height);
	if (width > std::numeric_limits<Ursa::Size>::max() / 4 / height)
	{
		return Result<void>::Failure("ImGui font atlas size overflow");
	}
	const Ursa::Size byteCount = static_cast<Ursa::Size>(width) * height * 4;
	Ursa::Vector<Ursa::UInt8> rgba(byteCount);
	if (data->Format == ImTextureFormat_RGBA32)
	{
		std::memcpy(rgba.data(), data->GetPixels(), byteCount);
	}
	else if (data->Format == ImTextureFormat_Alpha8)
	{
		for (Ursa::Size i = 0; i < static_cast<Ursa::Size>(width) * height; ++i)
		{
			rgba[i * 4 + 0] = 255;
			rgba[i * 4 + 1] = 255;
			rgba[i * 4 + 2] = 255;
			rgba[i * 4 + 3] = data->Pixels[i];
		}
	}
	else
	{
		return Result<void>::Failure("Unsupported ImGui font atlas format");
	}
	auto upload = device->CreateBuffer({byteCount, Rhi::BufferUsage::TransferSource, Rhi::Memory::Upload});
	if (!upload)
	{
		return Result<void>::Failure(upload.error);
	}
	auto written = upload.value->Write(0, rgba.data(), byteCount);
	if (!written)
	{
		return written;
	}
	auto texture = device->CreateTexture(
	    {width, height, Rhi::Format::RgbA8Unorm, Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination});
	if (!texture)
	{
		return Result<void>::Failure(texture.error);
	}
	auto command = device->CreateCommandList();
	if (!command)
	{
		return Result<void>::Failure(command.error);
	}
	auto result = command.value->Begin();
	if (result)
	{
		result = command.value->Barrier(texture.value, Rhi::ResourceState::Undefined,
		                                Rhi::ResourceState::TransferDestination);
	}
	if (result)
	{
		result = command.value->CopyBufferToTexture(upload.value, texture.value);
	}
	if (result)
	{
		result = command.value->Barrier(texture.value, Rhi::ResourceState::TransferDestination,
		                                Rhi::ResourceState::ShaderRead);
	}
	if (result)
	{
		result = command.value->End();
	}
	if (!result)
	{
		return result;
	}
	auto submitted = device->Submit(command.value);
	if (!submitted)
	{
		return Result<void>::Failure(submitted.error);
	}
	result = submitted.value->Wait();
	if (!result)
	{
		return result;
	}
	auto registered = RegisterTexture(std::move(texture.value), atlasSampler);
	if (!registered)
	{
		return Result<void>::Failure(registered.error);
	}
	if (auto found = atlasIds.find(data); found != atlasIds.end())
	{
		UnregisterTexture(found->second);
	}
	atlasIds[data] = registered.value;
	data->SetTexID(registered.value);
	data->SetStatus(ImTextureStatus_OK);
	return {};
}

Result<void> ImGuiBridge::UpdateAtlas()
{
	auto* drawData = ImGui::GetDrawData();
	if (!drawData || !drawData->Textures)
	{
		return {};
	}
	for (ImTextureData* data : *drawData->Textures)
	{
		if (data->Status == ImTextureStatus_WantCreate || data->Status == ImTextureStatus_WantUpdates)
		{
			auto uploaded = UploadAtlas(data);
			if (!uploaded)
			{
				return uploaded;
			}
		}
		else if (data->Status == ImTextureStatus_WantDestroy)
		{
			if (auto found = atlasIds.find(data); found != atlasIds.end())
			{
				UnregisterTexture(found->second);
				atlasIds.erase(found);
			}
			data->SetTexID(ImTextureID_Invalid);
			data->SetStatus(ImTextureStatus_Destroyed);
		}
	}
	return {};
}

Result<void> ImGuiBridge::Record(const Rhi::CommandListPtr& command, Rhi::Format colorFormat,
                                 Ursa::UInt32 framebufferWidth, Ursa::UInt32 framebufferHeight)
{
	if (!command || !framebufferWidth || !framebufferHeight)
	{
		return Result<void>::Failure("ImGui record requires a command list and nonzero framebuffer");
	}
	ImGui::SetCurrentContext(context);
	const ImDrawData* data = ImGui::GetDrawData();
	if (!data || data->TotalVtxCount == 0 || data->TotalIdxCount == 0)
	{
		return {};
	}
	const int formatKey = static_cast<int>(colorFormat);
	auto pipeline = pipelines.find(formatKey);
	if (pipeline == pipelines.end())
	{
		Rhi::GraphicsPipelineDesc desc;
		desc.vertex = vertexShader;
		desc.fragment = fragmentShader;
		desc.layout = layout;
		desc.colorFormat = colorFormat;
		desc.blend = true;
		desc.cullBack = false;
		desc.vertexStride = sizeof(GuiVertex);
		desc.attributes = {
		    {0, 0, Rhi::VertexFormat::Float2}, {1, 8, Rhi::VertexFormat::Float2}, {2, 16, Rhi::VertexFormat::Float4}};
		auto created = device->CreateGraphicsPipeline(desc);
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		pipeline = pipelines.emplace(formatKey, std::move(created.value)).first;
	}
	Ursa::Vector<GuiVertex> vertices;
	Ursa::Vector<Ursa::UInt32> indices;
	vertices.reserve(static_cast<Ursa::Size>(data->TotalVtxCount));
	indices.reserve(static_cast<Ursa::Size>(data->TotalIdxCount));
	for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex)
	{
		const ImDrawList* list = data->CmdLists[listIndex];
		for (const ImDrawVert& source : list->VtxBuffer)
		{
			GuiVertex vertex{};
			vertex.position[0] = (source.pos.x - data->DisplayPos.x) / data->DisplaySize.x * 2.f - 1.f;
			vertex.position[1] = 1.f - (source.pos.y - data->DisplayPos.y) / data->DisplaySize.y * 2.f;
			vertex.uv[0] = source.uv.x;
			vertex.uv[1] = source.uv.y;
			for (int channel = 0; channel < 4; ++channel)
			{
				vertex.color[channel] = static_cast<Ursa::Float32>((source.col >> (channel * 8)) & 255u) / 255.f;
			}
			vertices.push_back(vertex);
		}
		for (ImDrawIdx index : list->IdxBuffer)
		{
			indices.push_back(static_cast<Ursa::UInt32>(index));
		}
	}
	auto vertexBufferResult =
	    device->CreateBuffer({vertices.size() * sizeof(GuiVertex), Rhi::BufferUsage::Vertex, Rhi::Memory::Upload});
	if (!vertexBufferResult)
	{
		return Result<void>::Failure(vertexBufferResult.error);
	}
	auto indexBufferResult =
	    device->CreateBuffer({indices.size() * sizeof(Ursa::UInt32), Rhi::BufferUsage::Index, Rhi::Memory::Upload});
	if (!indexBufferResult)
	{
		return Result<void>::Failure(indexBufferResult.error);
	}
	auto result = vertexBufferResult.value->Write(0, vertices.data(), vertices.size() * sizeof(GuiVertex));
	if (result)
	{
		result = indexBufferResult.value->Write(0, indices.data(), indices.size() * sizeof(Ursa::UInt32));
	}
	if (!result)
	{
		return result;
	}
	vertexBuffer = std::move(vertexBufferResult.value);
	indexBuffer = std::move(indexBufferResult.value);
	result = command->BindPipeline(pipeline->second);
	if (result)
	{
		result = command->BindVertexBuffer(vertexBuffer);
	}
	if (result)
	{
		result = command->BindIndexBuffer(indexBuffer);
	}
	if (!result)
	{
		return result;
	}
	Ursa::UInt32 globalVertex = 0;
	Ursa::UInt32 globalIndex = 0;
	for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex)
	{
		const ImDrawList* list = data->CmdLists[listIndex];
		for (const ImDrawCmd& draw : list->CmdBuffer)
		{
			if (draw.UserCallback)
			{
				if (draw.UserCallback == ImDrawCallback_ResetRenderState)
				{
					result = command->BindPipeline(pipeline->second);
					if (result)
					{
						result = command->BindVertexBuffer(vertexBuffer);
					}
					if (result)
					{
						result = command->BindIndexBuffer(indexBuffer);
					}
				}
				else
				{
					draw.UserCallback(list, &draw);
				}
				if (!result)
				{
					return result;
				}
				continue;
			}
			const Ursa::Float32 minX = (draw.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x;
			const Ursa::Float32 minY = (draw.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y;
			const Ursa::Float32 maxX = (draw.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x;
			const Ursa::Float32 maxY = (draw.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y;
			const Ursa::Int32 left = std::max<Ursa::Int32>(0, static_cast<Ursa::Int32>(std::floor(minX)));
			const Ursa::Int32 top = std::max<Ursa::Int32>(0, static_cast<Ursa::Int32>(std::floor(minY)));
			const Ursa::Int32 right = std::min<Ursa::Int32>(static_cast<Ursa::Int32>(framebufferWidth),
			                                                static_cast<Ursa::Int32>(std::ceil(maxX)));
			const Ursa::Int32 bottom = std::min<Ursa::Int32>(static_cast<Ursa::Int32>(framebufferHeight),
			                                                 static_cast<Ursa::Int32>(std::ceil(maxY)));
			if (right <= left || bottom <= top || draw.ElemCount == 0)
			{
				continue;
			}
			const auto group = groups.find(draw.GetTexID());
			if (group == groups.end())
			{
				return Result<void>::Failure("ImGui draw references an unregistered texture");
			}
			result = command->BindGroup(group->second);
			if (result)
			{
				result = command->SetScissor(
				    {left, top, static_cast<Ursa::UInt32>(right - left), static_cast<Ursa::UInt32>(bottom - top)});
			}
			if (result)
			{
				result = command->DrawIndexed(draw.ElemCount, 1, 0, globalIndex + draw.IdxOffset,
				                              static_cast<Ursa::Int32>(globalVertex + draw.VtxOffset));
			}
			if (!result)
			{
				return result;
			}
		}
		globalVertex += static_cast<Ursa::UInt32>(list->VtxBuffer.Size);
		globalIndex += static_cast<Ursa::UInt32>(list->IdxBuffer.Size);
	}
	return {};
}
} // namespace Ursa::Editor
