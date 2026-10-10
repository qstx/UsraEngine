#include <Vfs/RuntimeContent.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/BuildResourceManifest.hpp>
#include <Core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

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

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Path(const char* value)
{
	return Take(VirtualPath::Parse(value));
}
} // namespace

int main()
{
	try
	{
		auto files = std::make_shared<VirtualFileSystem>();
		Done(files->MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Done(files->MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const Vector<UInt8> bytes{1, 2, 3};
		Done(files->WriteAtomic(Path("prj:Probe.ursascene"), bytes));
		Done(files->WriteAtomic(Path("lib:Artifacts/first.bin"), bytes));
		RuntimeContentSnapshot snapshot;
		snapshot.configuration = "Debug";
		snapshot.inputs.push_back(Take(CaptureRuntimeContentInput(*files, Path("prj:Probe.ursascene"))));
		snapshot.entries.push_back({Path("prj:Probe.ursascene"), Path("lib:Artifacts/first.bin"),
		                            BuildResourceFingerprint(bytes), bytes.size()});
		auto lease = Take(files->AcquireExclusiveLease(Path("lib:Import.lock")));
		Expect(!files->AcquireExclusiveLease(Path("lib:Import.lock")), "Concurrent project writer was accepted");
		Done(PublishRuntimeContent(*files, snapshot));
		auto loaded = Take(ReadRuntimeContent(*files, "Debug", "Player", "WindowsVulkan", files.get()));
		auto view = Take(OpenRuntimeContentView(files, loaded));
		Expect(Take(view.Read(Path("prj:Probe.ursascene"))) == bytes, "Runtime view returned wrong content");
		Expect(!view.IsMounted("lib") && !view.Write(Path("prj:Probe.ursascene"), {4}),
		       "Runtime exposed writable import state");
		const auto originalManifest = Take(files->Read(Take(RuntimeContentManifestPath("Debug"))));
		Done(files->WriteAtomic(Path("prj:Probe.ursascene"), {4}));
		Expect(!ReadRuntimeContent(*files, "Debug", "Player", "WindowsVulkan", files.get()),
		       "Stale source passed readiness");
		Expect(!PublishRuntimeContent(*files, snapshot), "Stale candidate was published");
		Expect(Take(files->Read(Take(RuntimeContentManifestPath("Debug")))) == originalManifest,
		       "Failed publish replaced ready record");
		Expect(Take(view.Read(Path("prj:Probe.ursascene"))) == bytes,
		       "Source update changed already-published content");
		Expect(!ReadRuntimeContent(*files, "Release"), "Wrong configuration was accepted");
		Done(files->Remove(Path("lib:Artifacts/first.bin")));
		Expect(!ReadRuntimeContent(*files, "Debug"), "Missing artifact passed readiness");
		lease.reset();
		lease = Take(files->AcquireExclusiveLease(Path("lib:Import.lock")));
		Expect(!RuntimeContentManifestPath("../Escape"), "Manifest path escaped target directory");
		const auto root = std::filesystem::temp_directory_path() / "UrsaContentLeaseTests";
		std::filesystem::create_directories(root);
		VirtualFileSystem directory;
		Done(directory.Mount("lib", root, MountAccess::ReadWrite));
		auto nativeLease = Take(directory.AcquireExclusiveLease(Path("lib:Import.lock")));
		Expect(!directory.AcquireExclusiveLease(Path("lib:Import.lock")), "Native lease accepted another writer");
		nativeLease.reset();
		nativeLease = Take(directory.AcquireExclusiveLease(Path("lib:Import.lock")));
		nativeLease.reset();
		Done(directory.Remove(Path("lib:Import.lock")));
		std::cout << "Runtime content tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
