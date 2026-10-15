#include <ComputeFixtures.hpp>
#include <Shader/ShaderImporter.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
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
} // namespace

Result<std::shared_ptr<Shader::ShaderAsset>> CreateComputeFixture(const Shader::ShaderCompilerTools& tools,
                                                                  ResourceId identity)
{
	try
	{
		VirtualFileSystem files;
		Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const auto sourcePath = Take(VirtualPath::Parse("prj:ComputeFixture/Fill.compute"));
		const String source = R"(
#pragma kernel Fill
[[vk::image_format("rgba8")]] RWTexture2D<float4> _Output : register(u0);
RWStructuredBuffer<uint> _Data : register(u1);
cbuffer Parameters : register(b0) { float4 _Color; int _Bias; };
[numthreads(8, 8, 1)]
void Fill(uint3 id : SV_DispatchThreadID) {
    if (id.x >= 8 || id.y >= 8) return;
    _Output[id.xy] = _Color;
    _Data[id.y * 8 + id.x] = id.y * 8 + id.x + _Bias;
}
)";
		Done(files.WriteAtomic(sourcePath, Vector<UInt8>(source.begin(), source.end())));
		const String metadata = "{\"version\":1,\"folder\":false,\"guid\":\"" + identity.ToString() + "\"}";
		Done(files.WriteAtomic(Take(VirtualPath::Parse(sourcePath.ToString() + ".meta")),
		                       Vector<UInt8>(metadata.begin(), metadata.end())));
		Shader::DxcShaderCompiler compiler(tools);
		auto imported =
		    Take(Shader::ShaderImporter::ImportAll(files, compiler, Take(compiler.Version()), "prj", "prj",
		                                           Take(VirtualPath::Parse("lib:ShaderCache")), 256, {}, sourcePath));
		if (imported.size() != 1)
		{
			throw std::runtime_error("Compute fixture did not produce one imported Shader asset");
		}
		return Result<std::shared_ptr<Shader::ShaderAsset>>::Success(
		    std::make_shared<Shader::ShaderAsset>(std::move(imported.front())));
	}
	catch (const std::exception& error)
	{
		return Result<std::shared_ptr<Shader::ShaderAsset>>::Failure(error.what());
	}
}
} // namespace Ursa::Test
