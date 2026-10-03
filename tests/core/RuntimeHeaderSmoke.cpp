#include <core/Result.hpp>
#include <rhi/RHI.hpp>
#include <window/Window.hpp>

int main() {
    ursa::Vector<uint8_t> code{1, 2, 3, 4};
    ursa::rhi::ShaderDesc documentedShader;
    documentedShader.code = code;
    documentedShader.entry = ursa::String("main");
    ursa::Result<int> result = ursa::Result<int>::failure("diagnostic");
    ursa::rhi::ShaderDesc shader;
    shader.code.push_back(1);
    ursa::rhi::AdapterInfo adapter;
    adapter.name = "adapter";

    return documentedShader.code.size() == 4 && documentedShader.entry == "main" && !result &&
                   result.error == "diagnostic" && shader.code.size() == 1 && adapter.name == "adapter"
               ? 0
               : 1;
}
