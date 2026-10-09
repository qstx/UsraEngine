#pragma once

#include <Vfs/VirtualFileSystem.hpp>

namespace Ursa
{
struct BuildResourceFile
{
	String path;
	String fingerprint;
	UInt64 size = 0;
};

struct BuildResourceManifest
{
	String configuration;
	String target = "VulkanSpirv";
	String compilerVersion;
	bool managed = false;
	Vector<BuildResourceFile> inputs;
	Vector<BuildResourceFile> outputs;
};

String BuildResourceFingerprint(const Vector<UInt8>& bytes);

Result<Vector<BuildResourceFile>> SnapshotBuildResourceFiles(const VirtualFileSystem& files, const String& scheme);

bool EqualBuildResourceFiles(const Vector<BuildResourceFile>& left, const Vector<BuildResourceFile>& right);

Result<BuildResourceManifest> ReadBuildResourceManifest(const VirtualFileSystem& files, const String& scheme);

Result<void> WriteBuildResourceManifest(VirtualFileSystem& files, const String& scheme,
                                        const BuildResourceManifest& manifest);

Result<BuildResourceManifest> ValidateBuildResources(const VirtualFileSystem& files, const String& scheme,
                                                     const String& configuration, bool requireManaged = false,
                                                     const VirtualFileSystem* sources = nullptr,
                                                     const String& sourceScheme = "source");
} // namespace Ursa
