#pragma once

#include <Core/StringInterop.hpp>
#include <Scene/Scene.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <stdexcept>
#include <utility>

namespace Ursa::Test
{
using namespace Ursa::Scripting;

inline void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

inline void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

inline VirtualPath Path(const String& value)
{
	auto path = VirtualPath::Parse(value);
	if (!path)
	{
		throw std::runtime_error(ToStdString(path.error));
	}
	return path.value;
}

void TestClassicRenderingExample(DotNetHost& host, const VirtualPath& contentPath, UInt32 objectCount = 1);
void TestManagedMatrixBindings(DotNetHost& host, VirtualFileSystem& filesystem, const VirtualPath& content);
void TestPipelineAssetReload(DotNetHost& host, VirtualFileSystem& filesystem, const VirtualPath& content);
void TestManagedForward(DotNetHost& host, const VirtualPath& content);
void TestDocumentedCustomPipeline(DotNetHost& host, const VirtualPath& content);
void TestForwardCameraRegions(DotNetHost& host, const VirtualPath& content);
void TestUniversalPostProcessingBacking(DotNetHost& host, const VirtualPath& content);
void TestManagedImportSession(DotNetHost& host, VirtualFileSystem& filesystem, const ManagedHostFiles& files,
                              const VirtualPath& content);
void TestManagedRenderingComponentSerialization(DotNetHost& host, VirtualFileSystem& filesystem,
                                                const VirtualPath& contentPath);
#if defined(URSA_DOTNET_TEST_EDITOR)
void TestManagedImporterRegistration(DotNetHost& host, VirtualFileSystem& filesystem, const VirtualPath& content);
void TestEditorExtensions(DotNetHost& host, VirtualFileSystem& source, const std::filesystem::path& root,
                          bool builtinInspectorOnly = false);
#endif
} // namespace Ursa::Test
