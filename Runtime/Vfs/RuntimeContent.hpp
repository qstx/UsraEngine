#pragma once

#include <Vfs/VirtualFileSystem.hpp>

namespace Ursa
{
struct RuntimeContentEntry
{
	VirtualPath path;
	VirtualPath storage;
	String fingerprint;
	UInt64 size = 0;
};

struct RuntimeContentInput
{
	VirtualPath path;
	String fingerprint;
	UInt64 size = 0;
};

struct RuntimeContentSnapshot
{
	String target = "WindowsVulkan";
	String configuration;
	String profile = "Player";
	Vector<RuntimeContentEntry> entries;
	Vector<RuntimeContentInput> inputs;
	Vector<String> completeSourceSchemes;
};

Result<VirtualPath> RuntimeContentManifestPath(const String& configuration, const String& profile = "Player",
                                               const String& target = "WindowsVulkan");

Result<RuntimeContentInput> CaptureRuntimeContentInput(const VirtualFileSystem& files, const VirtualPath& path);

Result<void> PublishRuntimeContent(VirtualFileSystem& files, RuntimeContentSnapshot snapshot);

Result<RuntimeContentSnapshot> ReadRuntimeContent(const VirtualFileSystem& files, const String& configuration,
                                                  const String& profile = "Player",
                                                  const String& target = "WindowsVulkan",
                                                  const VirtualFileSystem* currentSources = nullptr);

Result<VirtualFileSystem> OpenRuntimeContentView(std::shared_ptr<const VirtualFileSystem> storage,
                                                 const RuntimeContentSnapshot& snapshot);
} // namespace Ursa
