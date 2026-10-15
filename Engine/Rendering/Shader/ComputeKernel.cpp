#include <Rendering/Shader/ComputeKernel.hpp>

namespace Ursa::Rendering
{
Result<UInt32> FindComputeKernel(const Shader::ShaderAsset& shader, const String& name)
{
	if (name.empty())
	{
		return Result<UInt32>::Failure("Compute kernel name is empty");
	}
	UInt32 index = 0;
	UInt32 selected = 0;
	bool found = false;
	for (const auto& subShader : shader.subShaders)
	{
		for (const auto& pass : subShader.passes)
		{
			if (!pass.computeKernel)
			{
				continue;
			}
			if (pass.name == name)
			{
				if (found)
				{
					return Result<UInt32>::Failure("Compute kernel name is ambiguous in Shader " + shader.name + ": " +
					                               name);
				}
				selected = index;
				found = true;
			}
			++index;
		}
	}
	return found ? Result<UInt32>::Success(selected)
	             : Result<UInt32>::Failure("Compute kernel is absent from Shader " + shader.name + ": " + name);
}

Result<const Shader::ShaderPassAsset*> ComputeKernelAt(const Shader::ShaderAsset& shader, UInt32 index)
{
	UInt32 current = 0;
	for (const auto& subShader : shader.subShaders)
	{
		for (const auto& pass : subShader.passes)
		{
			if (pass.computeKernel && current++ == index)
			{
				return Result<const Shader::ShaderPassAsset*>::Success(&pass);
			}
		}
	}
	return Result<const Shader::ShaderPassAsset*>::Failure("Compute kernel index is out of range in Shader " +
	                                                       shader.name);
}
} // namespace Ursa::Rendering
