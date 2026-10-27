#pragma once

#include <Core/Result.hpp>
#include <filesystem>
#include <functional>

namespace Ursa::Editor
{
class ProjectWorkspace;
class ProjectBrowserModel;
struct EditorSelectionContext;
Result<void> CopyExternalProjectFiles(const std::filesystem::path& root, const Vector<String>& paths,
                                      const String& destinationFolder, bool* copiedAnything = nullptr);
Result<void> ImportExternalProjectFiles(ProjectWorkspace& workspace, ProjectBrowserModel& browser,
                                        EditorSelectionContext& selection, const Vector<String>& paths,
                                        const String& destinationFolder, const std::function<void()>& requestImport);
} // namespace Ursa::Editor
