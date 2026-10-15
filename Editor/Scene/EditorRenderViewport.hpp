#pragma once

#include <Core/Result.hpp>
#include <Rendering/Resources/RenderResources.hpp>
#include <Rendering/Scene/CameraData.hpp>
#include <Scene/Scene.hpp>
#include <functional>
#include <memory>
#include <optional>

namespace Ursa::Editor
{
using EditorRenderViewport =
    std::function<Result<void>(Scene&, const std::shared_ptr<Rendering::RenderTextureResource>&,
                               std::optional<Rendering::CameraType>, std::optional<Rendering::CameraData>)>;
} // namespace Ursa::Editor
