#include <Vfs/BuildResourceManifest.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

void Expect(bool condition, const char* message)
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

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}
} // namespace

int main()
{
	try
	{
		VirtualFileSystem files;
		Done(files.MountProvider("source", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Done(files.MountProvider("built", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const auto source = VirtualPath::Parse("source:Shaders/Test.ursashader").value;
		const auto shader = VirtualPath::Parse("built:Shaders/Test.ursashaderasset").value;
		Done(files.WriteAtomic(source, {1, 2, 3}));
		Done(files.WriteAtomic(shader, {4, 5, 6}));
		Done(files.WriteAtomic(VirtualPath::Parse(shader.ToString() + ".meta").value, {7}));
		BuildResourceManifest manifest;
		manifest.configuration = "Debug";
		manifest.compilerVersion = "compiler version";
		manifest.inputs = Take(SnapshotBuildResourceFiles(files, "source"));
		manifest.outputs = Take(SnapshotBuildResourceFiles(files, "built"));
		Done(WriteBuildResourceManifest(files, "built", manifest));
		Expect(static_cast<bool>(ValidateBuildResources(files, "built", "Debug", false, &files)),
		       "Valid resource set was rejected");
		Expect(!ValidateBuildResources(files, "built", "Release"), "Mismatched configuration was accepted");
		Expect(!ValidateBuildResources(files, "built", "Debug", true), "Missing managed content was accepted");
		Done(files.Remove(shader));
		Expect(!ValidateBuildResources(files, "built", "Debug"), "Missing output passed a surviving manifest");
		Done(files.WriteAtomic(shader, {4, 5, 9}));
		Expect(!ValidateBuildResources(files, "built", "Debug"), "Corrupt output passed its fingerprint");
		Done(files.WriteAtomic(shader, {4, 5, 6}));
		Done(files.WriteAtomic(source, {1, 2, 9}));
		Expect(!ValidateBuildResources(files, "built", "Debug", false, &files), "Stale source was accepted");
		Done(files.WriteAtomic(source, {1, 2, 3}));
		manifest.outputs.front().path = "../Escape";
		Done(WriteBuildResourceManifest(files, "built", manifest));
		Expect(!ReadBuildResourceManifest(files, "built"), "Escaping manifest path was accepted");
		manifest.outputs = Take(SnapshotBuildResourceFiles(files, "built"));
		manifest.outputs.push_back(manifest.outputs.front());
		Done(WriteBuildResourceManifest(files, "built", manifest));
		Expect(!ReadBuildResourceManifest(files, "built"), "Duplicate output path was accepted");
		manifest.outputs = Take(SnapshotBuildResourceFiles(files, "built"));
		manifest.managed = true;
		Done(WriteBuildResourceManifest(files, "built", manifest));
		Expect(!ValidateBuildResources(files, "built", "Debug", true), "Incomplete managed manifest was accepted");
		std::cout << "Build resource manifest tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
