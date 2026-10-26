#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Vfs/VirtualFileSystem.hpp>

namespace Ursa::Editor
{
struct ProjectTagLayerSettings
{
	Vector<String> tags;
	Array<String, 32> layers;

	static ProjectTagLayerSettings Defaults();
	static Result<ProjectTagLayerSettings> Read(const VirtualFileSystem& files);
	Result<void> Save(VirtualFileSystem& files) const;
};
} // namespace Ursa::Editor
