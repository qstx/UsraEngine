#include <ui/ImGuiBridge.hpp>
#include <core/StringInterop.hpp>
#include <vfs/AssetRoots.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace Ursa::Editor
{
namespace
{
struct GuiVertex
{
	float position[2];
	float uv[2];
	float color[4];
};

ImGuiKey ToImGuiKey(Key key)
{
	switch (key)
	{
		case Key::tab:
			return ImGuiKey_Tab;
		case Key::left:
			return ImGuiKey_LeftArrow;
		case Key::right:
			return ImGuiKey_RightArrow;
		case Key::up:
			return ImGuiKey_UpArrow;
		case Key::down:
			return ImGuiKey_DownArrow;
		case Key::pageUp:
			return ImGuiKey_PageUp;
		case Key::pageDown:
			return ImGuiKey_PageDown;
		case Key::home:
			return ImGuiKey_Home;
		case Key::end:
			return ImGuiKey_End;
		case Key::insert:
			return ImGuiKey_Insert;
		case Key::Delete:
			return ImGuiKey_Delete;
		case Key::backspace:
			return ImGuiKey_Backspace;
		case Key::space:
			return ImGuiKey_Space;
		case Key::enter:
			return ImGuiKey_Enter;
		case Key::escape:
			return ImGuiKey_Escape;
		case Key::leftShift:
			return ImGuiKey_LeftShift;
		case Key::rightShift:
			return ImGuiKey_RightShift;
		case Key::leftControl:
			return ImGuiKey_LeftCtrl;
		case Key::rightControl:
			return ImGuiKey_RightCtrl;
		case Key::leftAlt:
			return ImGuiKey_LeftAlt;
		case Key::rightAlt:
			return ImGuiKey_RightAlt;
		case Key::leftSuper:
			return ImGuiKey_LeftSuper;
		case Key::rightSuper:
			return ImGuiKey_RightSuper;
		case Key::a:
			return ImGuiKey_A;
		case Key::b:
			return ImGuiKey_B;
		case Key::c:
			return ImGuiKey_C;
		case Key::d:
			return ImGuiKey_D;
		case Key::e:
			return ImGuiKey_E;
		case Key::f:
			return ImGuiKey_F;
		case Key::g:
			return ImGuiKey_G;
		case Key::h:
			return ImGuiKey_H;
		case Key::i:
			return ImGuiKey_I;
		case Key::j:
			return ImGuiKey_J;
		case Key::k:
			return ImGuiKey_K;
		case Key::l:
			return ImGuiKey_L;
		case Key::m:
			return ImGuiKey_M;
		case Key::n:
			return ImGuiKey_N;
		case Key::o:
			return ImGuiKey_O;
		case Key::p:
			return ImGuiKey_P;
		case Key::q:
			return ImGuiKey_Q;
		case Key::r:
			return ImGuiKey_R;
		case Key::s:
			return ImGuiKey_S;
		case Key::t:
			return ImGuiKey_T;
		case Key::u:
			return ImGuiKey_U;
		case Key::v:
			return ImGuiKey_V;
		case Key::w:
			return ImGuiKey_W;
		case Key::x:
			return ImGuiKey_X;
		case Key::y:
			return ImGuiKey_Y;
		case Key::z:
			return ImGuiKey_Z;
		default:
			break;
	}
	if (key >= Key::digit0 && key <= Key::digit9)
	{
		return static_cast<ImGuiKey>(ImGuiKey_0 + static_cast<int>(key) - static_cast<int>(Key::digit0));
	}
	return ImGuiKey_None;
}

Result<Vector<uint8_t>> ReadShader(const VirtualFileSystem& files, const char* name)
{
	auto path = VirtualPath::Parse(String(engineScheme) + ":shaders/ui/" + name + ".spv");
	if (!path)
	{
		return Result<Vector<uint8_t>>::Failure(path.error);
	}
	auto bytes = files.Read(path.value);
	if (!bytes)
	{
		return Result<Vector<uint8_t>>::Failure(bytes.error);
	}
	if (bytes.value.empty())
	{
		return Result<Vector<uint8_t>>::Failure(String("Empty Editor shader: ") + name);
	}
	return bytes;
}
} // namespace

ImGuiBridge::ImGuiBridge(Rhi::DevicePtr device, std::shared_ptr<Window> window)
    : device(std::move(device)), window(std::move(window))
{
}

Result<std::unique_ptr<ImGuiBridge>> ImGuiBridge::Create(Rhi::DevicePtr device, std::shared_ptr<Window> window)
{
	if (!device)
	{
		return Result<std::unique_ptr<ImGuiBridge>>::Failure("ImGui RHI bridge requires a device");
	}
	auto bridge = std::unique_ptr<ImGuiBridge>(new ImGuiBridge(std::move(device), std::move(window)));
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
	auto layoutResult = device->CreateBindGroupLayout({{0, Rhi::BindingType::combinedImageSampler, 1, 2}});
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
	auto vertexCode = ReadShader(engineFiles.value, "imgui.vert");
	if (!vertexCode)
	{
		return Result<void>::Failure(vertexCode.error);
	}
	auto fragmentCode = ReadShader(engineFiles.value, "imgui.frag");
	if (!fragmentCode)
	{
		return Result<void>::Failure(fragmentCode.error);
	}
	auto vertex =
	    device->CreateShader({Rhi::ShaderStage::vertex, Rhi::ShaderFormat::spirv, std::move(vertexCode.value)});
	if (!vertex)
	{
		return Result<void>::Failure(vertex.error);
	}
	vertexShader = std::move(vertex.value);
	auto fragment =
	    device->CreateShader({Rhi::ShaderStage::fragment, Rhi::ShaderFormat::spirv, std::move(fragmentCode.value)});
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
				    case WindowEventType::keyPressed:
				    case WindowEventType::keyReleased:
				    {
					    const bool pressed = event.type == WindowEventType::keyPressed;
					    if (const auto key = ToImGuiKey(event.key); key != ImGuiKey_None)
					    {
						    input.AddKeyEvent(key, pressed);
					    }
					    switch (event.key)
					    {
						    case Key::leftControl:
							    leftControl = pressed;
							    input.AddKeyEvent(ImGuiMod_Ctrl, leftControl || rightControl);
							    break;
						    case Key::rightControl:
							    rightControl = pressed;
							    input.AddKeyEvent(ImGuiMod_Ctrl, leftControl || rightControl);
							    break;
						    case Key::leftShift:
							    leftShift = pressed;
							    input.AddKeyEvent(ImGuiMod_Shift, leftShift || rightShift);
							    break;
						    case Key::rightShift:
							    rightShift = pressed;
							    input.AddKeyEvent(ImGuiMod_Shift, leftShift || rightShift);
							    break;
						    case Key::leftAlt:
							    leftAlt = pressed;
							    input.AddKeyEvent(ImGuiMod_Alt, leftAlt || rightAlt);
							    break;
						    case Key::rightAlt:
							    rightAlt = pressed;
							    input.AddKeyEvent(ImGuiMod_Alt, leftAlt || rightAlt);
							    break;
						    case Key::leftSuper:
							    leftSuper = pressed;
							    input.AddKeyEvent(ImGuiMod_Super, leftSuper || rightSuper);
							    break;
						    case Key::rightSuper:
							    rightSuper = pressed;
							    input.AddKeyEvent(ImGuiMod_Super, leftSuper || rightSuper);
							    break;
						    default:
							    break;
					    }
					    break;
				    }
				    case WindowEventType::keyRepeated:
					    break;
				    case WindowEventType::character:
					    input.AddInputCharacter(event.codepoint);
					    break;
				    case WindowEventType::cursorMoved:
					    input.AddMousePosEvent(static_cast<float>(event.x), static_cast<float>(event.y));
					    break;
				    case WindowEventType::mousePressed:
				    case WindowEventType::mouseReleased:
					    if (event.button != MouseButton::other)
					    {
						    input.AddMouseButtonEvent(static_cast<int>(event.button),
						                              event.type == WindowEventType::mousePressed);
					    }
					    break;
				    case WindowEventType::scrolled:
					    input.AddMouseWheelEvent(static_cast<float>(event.x), static_cast<float>(event.y));
					    break;
				    case WindowEventType::focused:
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

const char* ImGuiBridge::GetClipboardText(ImGuiContext* context)
{
	ImGui::SetCurrentContext(context);
	auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
	if (!bridge || !bridge->window)
	{
		return nullptr;
	}
	bridge->clipboardText = bridge->window->ClipboardText();
	return bridge->clipboardText.c_str();
}

void ImGuiBridge::SetClipboardText(ImGuiContext* context, const char* text)
{
	ImGui::SetCurrentContext(context);
	auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
	if (bridge && bridge->window)
	{
		bridge->window->SetClipboardText(text ? String(text) : String{});
	}
}

Result<void> ImGuiBridge::BeginFrame(float deltaSeconds, uint32_t width, uint32_t height)
{
	if (!width || !height)
	{
		return Result<void>::Failure("ImGui frame size must be positive");
	}
	ImGui::SetCurrentContext(context);
	auto& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
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
	if (!Rhi::Has(texture->desc.usage, Rhi::TextureUsage::sampled))
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
	const auto width = static_cast<uint32_t>(data->Width);
	const auto height = static_cast<uint32_t>(data->Height);
	if (width > std::numeric_limits<size_t>::max() / 4 / height)
	{
		return Result<void>::Failure("ImGui font atlas size overflow");
	}
	const size_t byteCount = static_cast<size_t>(width) * height * 4;
	std::vector<uint8_t> rgba(byteCount);
	if (data->Format == ImTextureFormat_RGBA32)
	{
		std::memcpy(rgba.data(), data->GetPixels(), byteCount);
	}
	else if (data->Format == ImTextureFormat_Alpha8)
	{
		for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
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
	auto upload = device->CreateBuffer({byteCount, Rhi::BufferUsage::transferSource, Rhi::Memory::upload});
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
	    {width, height, Rhi::Format::rgbA8Unorm, Rhi::TextureUsage::sampled | Rhi::TextureUsage::transferDestination});
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
		result = command.value->Barrier(texture.value, Rhi::ResourceState::undefined,
		                                Rhi::ResourceState::transferDestination);
	}
	if (result)
	{
		result = command.value->CopyBufferToTexture(upload.value, texture.value);
	}
	if (result)
	{
		result = command.value->Barrier(texture.value, Rhi::ResourceState::transferDestination,
		                                Rhi::ResourceState::shaderRead);
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

Result<void> ImGuiBridge::Record(const Rhi::CommandListPtr& command, Rhi::Format colorFormat, uint32_t framebufferWidth,
                                 uint32_t framebufferHeight)
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
		    {0, 0, Rhi::VertexFormat::float2}, {1, 8, Rhi::VertexFormat::float2}, {2, 16, Rhi::VertexFormat::float4}};
		auto created = device->CreateGraphicsPipeline(desc);
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		pipeline = pipelines.emplace(formatKey, std::move(created.value)).first;
	}
	std::vector<GuiVertex> vertices;
	std::vector<uint32_t> indices;
	vertices.reserve(static_cast<size_t>(data->TotalVtxCount));
	indices.reserve(static_cast<size_t>(data->TotalIdxCount));
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
				vertex.color[channel] = static_cast<float>((source.col >> (channel * 8)) & 255u) / 255.f;
			}
			vertices.push_back(vertex);
		}
		for (ImDrawIdx index : list->IdxBuffer)
		{
			indices.push_back(static_cast<uint32_t>(index));
		}
	}
	auto vertexBufferResult =
	    device->CreateBuffer({vertices.size() * sizeof(GuiVertex), Rhi::BufferUsage::vertex, Rhi::Memory::upload});
	if (!vertexBufferResult)
	{
		return Result<void>::Failure(vertexBufferResult.error);
	}
	auto indexBufferResult =
	    device->CreateBuffer({indices.size() * sizeof(uint32_t), Rhi::BufferUsage::index, Rhi::Memory::upload});
	if (!indexBufferResult)
	{
		return Result<void>::Failure(indexBufferResult.error);
	}
	auto result = vertexBufferResult.value->Write(0, vertices.data(), vertices.size() * sizeof(GuiVertex));
	if (result)
	{
		result = indexBufferResult.value->Write(0, indices.data(), indices.size() * sizeof(uint32_t));
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
	uint32_t globalVertex = 0;
	uint32_t globalIndex = 0;
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
			const float minX = (draw.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x;
			const float minY = (draw.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y;
			const float maxX = (draw.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x;
			const float maxY = (draw.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y;
			const int32_t left = std::max<int32_t>(0, static_cast<int32_t>(std::floor(minX)));
			const int32_t top = std::max<int32_t>(0, static_cast<int32_t>(std::floor(minY)));
			const int32_t right =
			    std::min<int32_t>(static_cast<int32_t>(framebufferWidth), static_cast<int32_t>(std::ceil(maxX)));
			const int32_t bottom =
			    std::min<int32_t>(static_cast<int32_t>(framebufferHeight), static_cast<int32_t>(std::ceil(maxY)));
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
				    {left, top, static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top)});
			}
			if (result)
			{
				result = command->DrawIndexed(draw.ElemCount, 1, 0, globalIndex + draw.IdxOffset,
				                              static_cast<int32_t>(globalVertex + draw.VtxOffset));
			}
			if (!result)
			{
				return result;
			}
		}
		globalVertex += static_cast<uint32_t>(list->VtxBuffer.Size);
		globalIndex += static_cast<uint32_t>(list->IdxBuffer.Size);
	}
	return {};
}
} // namespace Ursa::Editor
