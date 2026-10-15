#include <Rendering/Shader/ShaderPassSelection.hpp>
#include <iostream>
#include <stdexcept>

using namespace Ursa;
using namespace Ursa::Rendering;

namespace
{
template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		auto base = std::make_shared<Shader::ShaderAsset>();
		base->id = ResourceId::Generate();
		base->name = "Tests/Base";
		Shader::ShaderPassAsset forward;
		forward.name = "Forward";
		forward.tags = {{"LightMode", "CustomForward"}};
		forward.renderStates = {{"ZWrite", "Off"}};
		Shader::ShaderSubShaderAsset baseSub;
		baseSub.passes.push_back(forward);
		base->subShaders.push_back(baseSub);
		auto root = std::make_shared<Shader::ShaderAsset>();
		root->id = ResourceId::Generate();
		root->name = "Tests/Root";
		Shader::ShaderSubShaderAsset rootSub;
		rootSub.tags = {{"RenderPipeline", "Custom"}};
		rootSub.renderStates = {{"Cull", "Off"}};
		Shader::ShaderPassAsset depth;
		depth.name = "Depth";
		depth.tags = {{"LightMode", "DepthOnly"}};
		rootSub.passes.push_back(depth);
		rootSub.passOrder = {{0, "Tests/Base/Forward", true}, {0, {}, false}};
		root->subShaders.push_back(rootSub);
		root->resolvedReferences.push_back({{"Tests/Base", base->id, "Forward", false}, base});
		const auto usePass = Take(SelectGraphicsPass(root, "Custom", {"CustomForward"}));
		Expect(usePass && usePass->programShader == base && usePass->renderStates.size() == 2 &&
		           usePass->renderStates[0].name == "Cull" && usePass->renderStates[1].name == "ZWrite",
		       "UsePass did not retain its program identity and merged state order");
		const auto priority = Take(SelectGraphicsPass(root, "Custom", {"DepthOnly", "CustomForward"}));
		Expect(priority && priority->programShader == root && priority->passIndex == 0,
		       "ShaderTag request priority was replaced by source declaration order");
		Expect(Take(SelectGraphicsPass(root, "Custom", {}, 0))->programShader == base &&
		           Take(SelectGraphicsPass(root, "Custom", {}, 1))->programShader == root,
		       "Explicit Pass indices did not follow the declared UsePass/local order");
		Expect(!Take(SelectGraphicsPass(root, "Other", {"CustomForward"})) &&
		           !Take(SelectGraphicsPass(root, "Custom", {"Missing"})),
		       "Optional unmatched shader filtering selected an unrelated Pass");
		root->resolvedReferences.push_back({{"Tests/Base", base->id, {}, true}, base});
		Expect(Take(SelectGraphicsPass(root, "Other", {"CustomForward"}))->programShader == base,
		       "Fallback did not resolve a compatible tagged Pass");
		Expect(!SelectGraphicsPass(root, "Custom", {}, 99), "Explicit out-of-range Pass was accepted");
		base->subShaders[0].tags = {{"RenderPipeline", "Never"}};
		base->resolvedReferences.push_back({{"Tests/Root", root->id, {}, true}, root});
		Expect(!SelectGraphicsPass(root, "Other", {"CustomForward"}), "Fallback cycle did not produce a diagnostic");
		base->resolvedReferences.clear();
		std::cout << "Shader Pass selection tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
