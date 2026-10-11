#include <Core/StringInterop.hpp>
#include <Renderer3D/Renderer3D.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Vfs/AssetRoots.hpp>

#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Rhi;

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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		auto filesystem = Take(OpenEngineFileSystem(std::filesystem::u8path(URSA_ENGINE_SHADER_PAK)));
		auto sourcePath = Take(VirtualPath::Parse("ursa:Shaders/Renderer3D/Unlit.ursashader"));
		Expect(!filesystem.CheckWritable(sourcePath), "Shader source inside a pak was writable");
		auto resources = Take(ResourceSession::Open(filesystem));
		auto shader = Take(resources->LoadShader(Ursa::Shader::BuiltinUnlitShaderId()));
		Expect(shader->customEditor.empty() && !shader->subShaders.empty() &&
		           !shader->subShaders[0].passes[0].variants.empty() &&
		           !shader->subShaders[0].passes[0].variants[0].stages[0].spirv.empty(),
		       "Compiled Shader variant did not load from the pak resource index");
		ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(filesystem));
		DeviceDesc description;
#ifndef NDEBUG
		description.validation = true;
#endif
		auto device = Take(CreateDevice(description));
		auto renderer = Take(Renderer3D::Create(device));
		auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		Done(material->SetShaderAsset(shader));
		Done(material->SetPropertyValue("_BaseColor", *shader,
		                                MaterialPropertyValue::ColorValue({0.1f, 0.8f, 0.2f, 1.0f})));
		auto color = Take(device->CreateTexture(
		    {64, 64, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
		Vector<ModelBoxInstance> boxes{{Mat4::Identity(), {}, material}};
		Done(Take(renderer->RenderOffscreenModels(color, Mat4::Identity(), boxes))->Wait());
		auto readback = Take(device->CreateBuffer({64 * 64 * 4, BufferUsage::TransferDestination, Memory::Readback}));
		auto command = Take(device->CreateCommandList());
		Done(command->Begin());
		Done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
		Done(command->CopyTextureToBuffer(color, readback));
		Done(command->End());
		Done(Take(device->Submit(command))->Wait());
		Array<UInt8, 4> center{};
		Done(readback->Read((32 * 64 + 32) * 4, center.data(), center.size()));
		Expect(center[0] > 60 && center[1] > 180 && center[2] > 100,
		       "Pak-backed compiled Shader did not render without invoking the authoring compiler");
		Done(device->WaitIdle());
		ConfigureEngineFileSystem({});
		std::cout << "Pak Shader runtime tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		ConfigureEngineFileSystem({});
		std::cerr << error.what() << '\n';
		return 1;
	}
}
