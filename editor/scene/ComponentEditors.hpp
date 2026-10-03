#pragma once

#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <scene/Scene.hpp>
#include <functional>

namespace ursa::editor {
using ComponentEditor = std::function<Result<bool>(Component &, Scene &, NodeId)>;

class ComponentEditors final {
public:
    Result<void> registerEditor(String type, ComponentEditor editor);

    const ComponentEditor *find(const String &type) const;

private:
    Map<String, ComponentEditor> editors_;
};
} // namespace ursa::editor
