#include <Rendering/Shader/ShaderPassSelection.hpp>
#include <algorithm>

namespace Ursa::Rendering
{
namespace
{
Result<Vector<ResolvedGraphicsPass>> Visit(const std::shared_ptr<const Shader::ShaderAsset>& shader,
                                           const String& pipeline, Set<String>& active)
{
	if (!shader)
	{
		return Result<Vector<ResolvedGraphicsPass>>::Failure("Graphics pass selection requires a Shader asset");
	}
	const String identity = shader->id.ToString();
	if (!active.insert(identity).second)
	{
		return Result<Vector<ResolvedGraphicsPass>>::Failure("Shader fallback cycle: " + shader->name);
	}

	struct Leave
	{
		Set<String>& active;
		String identity;

		~Leave()
		{
			active.erase(identity);
		}
	} leave{active, identity};

	for (Size subIndex = 0; subIndex < shader->subShaders.size(); ++subIndex)
	{
		const auto& subShader = shader->subShaders[subIndex];
		const bool compatible = std::none_of(subShader.tags.begin(), subShader.tags.end(), [&](const auto& tag)
		                                     { return tag.name == "RenderPipeline" && tag.value != pipeline; });
		if (!compatible)
		{
			continue;
		}
		Vector<Shader::SubShaderPassOrderEntry> order = subShader.passOrder;
		if (order.empty())
		{
			for (UInt32 pass = 0; pass < subShader.passes.size(); ++pass)
			{
				order.push_back({pass, {}, false});
			}
		}
		Vector<ResolvedGraphicsPass> result;
		for (const auto& entry : order)
		{
			ResolvedGraphicsPass selected;
			selected.programShader = shader;
			selected.subShaderIndex = UInt32(subIndex);
			selected.passIndex = entry.localPassIndex;
			selected.renderStates = subShader.renderStates;
			if (entry.isReused)
			{
				const auto slash = entry.reusedPass.rfind('/');
				if (slash == String::npos)
				{
					return Result<Vector<ResolvedGraphicsPass>>::Failure("Invalid UsePass reference: " +
					                                                     entry.reusedPass);
				}
				const String name = entry.reusedPass.substr(0, slash);
				const String passName = entry.reusedPass.substr(slash + 1);
				const auto reference =
				    std::find_if(shader->resolvedReferences.begin(), shader->resolvedReferences.end(),
				                 [&](const auto& value)
				                 {
					                 return !value.reference.fallback && value.reference.shaderName == name &&
					                        value.reference.passName == passName;
				                 });
				if (reference == shader->resolvedReferences.end() || !reference->asset)
				{
					return Result<Vector<ResolvedGraphicsPass>>::Failure("Unresolved UsePass: " + entry.reusedPass +
					                                                     " in " + shader->name);
				}
				selected.programShader = reference->asset;
				bool found = false;
				for (Size sub = 0; sub < reference->asset->subShaders.size() && !found; ++sub)
				{
					for (Size pass = 0; pass < reference->asset->subShaders[sub].passes.size(); ++pass)
					{
						if (reference->asset->subShaders[sub].passes[pass].name == passName)
						{
							selected.subShaderIndex = UInt32(sub);
							selected.passIndex = UInt32(pass);
							const auto& states = reference->asset->subShaders[sub].renderStates;
							selected.renderStates.insert(selected.renderStates.end(), states.begin(), states.end());
							found = true;
							break;
						}
					}
				}
				if (!found)
				{
					return Result<Vector<ResolvedGraphicsPass>>::Failure("UsePass target is absent: " +
					                                                     entry.reusedPass);
				}
			}
			if (selected.passIndex >= selected.programShader->subShaders[selected.subShaderIndex].passes.size())
			{
				return Result<Vector<ResolvedGraphicsPass>>::Failure("Shader pass order is out of range: " +
				                                                     shader->name);
			}
			const auto& pass = selected.programShader->subShaders[selected.subShaderIndex].passes[selected.passIndex];
			if (pass.computeKernel)
			{
				continue;
			}
			selected.renderStates.insert(selected.renderStates.end(), pass.renderStates.begin(),
			                             pass.renderStates.end());
			result.push_back(std::move(selected));
		}
		if (!result.empty())
		{
			return Result<Vector<ResolvedGraphicsPass>>::Success(std::move(result));
		}
	}
	const auto fallback = std::find_if(shader->resolvedReferences.begin(), shader->resolvedReferences.end(),
	                                   [](const auto& value) { return value.reference.fallback; });
	return fallback == shader->resolvedReferences.end() ? Result<Vector<ResolvedGraphicsPass>>::Success({})
	                                                    : Visit(fallback->asset, pipeline, active);
}
} // namespace

Result<Vector<ResolvedGraphicsPass>> OrderedGraphicsPasses(const std::shared_ptr<const Shader::ShaderAsset>& shader,
                                                           const String& activePipeline)
{
	Set<String> active;
	return Visit(shader, activePipeline, active);
}

Result<std::optional<ResolvedGraphicsPass>> SelectGraphicsPass(const std::shared_ptr<const Shader::ShaderAsset>& shader,
                                                               const String& activePipeline,
                                                               const Vector<String>& shaderTags, Int32 explicitPass)
{
	if (explicitPass < -1)
	{
		return Result<std::optional<ResolvedGraphicsPass>>::Failure("Explicit Shader Pass is invalid");
	}
	auto ordered = OrderedGraphicsPasses(shader, activePipeline);
	if (!ordered)
	{
		return Result<std::optional<ResolvedGraphicsPass>>::Failure(ordered.error);
	}
	if (explicitPass >= 0)
	{
		if (Size(explicitPass) >= ordered.value.size())
		{
			return Result<std::optional<ResolvedGraphicsPass>>::Failure("Explicit Shader Pass is out of range");
		}
		return Result<std::optional<ResolvedGraphicsPass>>::Success(ordered.value[Size(explicitPass)]);
	}
	for (const auto& requested : shaderTags)
	{
		for (const auto& selected : ordered.value)
		{
			const auto& pass = selected.programShader->subShaders[selected.subShaderIndex].passes[selected.passIndex];
			const auto tag = std::find_if(pass.tags.begin(), pass.tags.end(),
			                              [](const auto& value) { return value.name == "LightMode"; });
			if (tag == pass.tags.end() ? requested == "SRPDefaultUnlit" : tag->value == requested)
			{
				return Result<std::optional<ResolvedGraphicsPass>>::Success(selected);
			}
		}
	}
	return Result<std::optional<ResolvedGraphicsPass>>::Success({});
}
} // namespace Ursa::Rendering
