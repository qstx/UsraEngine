#include <scene/ComponentEditors.hpp>

namespace ursa::editor {
Result<void> ComponentEditors::registerEditor(String type, ComponentEditor editor) {
    if (type.empty() || !editor)
        return Result<void>::failure("Component editor requires a type and callback");
    if (!editors_.emplace(std::move(type), std::move(editor)).second)
        return Result<void>::failure("Component editor already registered");
    return {};
}

const ComponentEditor *ComponentEditors::find(const String &type) const {
    auto found = editors_.find(type);
    return found == editors_.end() ? nullptr : &found->second;
}
} // namespace ursa::editor
