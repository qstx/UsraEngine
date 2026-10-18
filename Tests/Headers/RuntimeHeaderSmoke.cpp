#include <Core/Types.hpp>
#include <Core/Result.hpp>
#include <Rhi/RHI.hpp>
#include <Window/Window.hpp>

int TestRuntimeHeader()
{
	Ursa::Vector<Ursa::UInt8> code{1, 2, 3, 4};
	Ursa::Rhi::ShaderDesc documentedShader;
	documentedShader.code = code;
	documentedShader.entry = Ursa::String("main");
	Ursa::Result<int> result = Ursa::Result<int>::Failure("diagnostic");
	Ursa::Rhi::ShaderDesc shader;
	shader.code.push_back(1);
	Ursa::Rhi::AdapterInfo adapter;
	adapter.name = "adapter";

	return documentedShader.code.size() == 4 && documentedShader.entry == "main" && !result &&
	               result.error == "diagnostic" && shader.code.size() == 1 && adapter.name == "adapter"
	           ? 0
	           : 1;
}
