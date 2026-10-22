#include <Assets/ProjectAssetImporter.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

class LibraryProvider final : public VirtualFileProvider
{
public:
	bool failManifest = false;
	UInt32 writes = 0;
	MemoryFileProvider memory;

	String Identity() const override
	{
		return "candidate-test:" + memory.Identity();
	}

	String SourceLabel() const override
	{
		return "candidate-test";
	}

	Result<bool> Exists(const String& path) const override
	{
		return memory.Exists(path);
	}

	Result<UInt64> Size(const String& path) const override
	{
		return memory.Size(path);
	}

	Result<Vector<UInt8>> ReadRange(const String& path, UInt64 offset, UInt64 size) const override
	{
		return memory.ReadRange(path, offset, size);
	}

	Result<Vector<String>> List() const override
	{
		return memory.List();
	}

	Result<void> WriteAtomic(const String& path, const Vector<UInt8>& bytes, bool createDirectories) override
	{
		if (failManifest && path == "AssetDatabase/prj.json")
		{
			return Result<void>::Failure("Injected import manifest write failure");
		}
		++writes;
		return memory.WriteAtomic(path, bytes, createDirectories);
	}

	Result<void> Append(const String& path, const Vector<UInt8>& bytes, bool createDirectories) override
	{
		return memory.Append(path, bytes, createDirectories);
	}

	Result<void> Remove(const String& path) override
	{
		return memory.Remove(path);
	}
};

void TestCandidatePublication()
{
	VirtualFileSystem files;
	auto library = std::make_shared<LibraryProvider>();
	Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(files.MountProvider("lib", library, MountAccess::ReadWrite));
	const auto source = Take(VirtualPath::Parse("prj:Example.probe"));
	const auto dependency = Take(VirtualPath::Parse("prj:Dependency.data"));
	const auto manifest = Take(VirtualPath::Parse("lib:AssetDatabase/prj.json"));
	Done(files.WriteAtomic(source, {1}));
	Done(files.WriteAtomic(dependency, {4}));
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	const auto guid = Take(database->Metadata(source)).guid;
	const auto dependencyGuid = Take(database->Metadata(dependency)).guid;
	UInt8 output = 1;
	bool failAfterStaging = false;
	bool changeInput = false;
	bool changeRules = false;
	ProjectAssetImporter importer;
	Done(importer.Register(
	    ".probe",
	    [&](AssetDatabase& candidate, const VirtualPath& path, bool) -> Result<AssetImportResult>
	    {
		    const auto existed = Take(files.Exists(manifest));
		    const auto oldManifest = existed ? Take(files.Read(manifest)) : Vector<UInt8>{};
		    auto published = candidate.PublishImport(dependency, "fixture.Dependency", 1, "cpu",
		                                             nlohmann::json::object(), {}, {{0, "Default", {output, 99}}});
		    if (!published)
		    {
			    return Result<AssetImportResult>::Failure(published.error);
		    }
		    published = candidate.PublishImport(path, "fixture.Candidate", 1, "cpu", nlohmann::json::object(),
		                                        {dependency}, {{0, "Default", {output}}, {7, "Default", {output, 7}}});
		    if (!published)
		    {
			    return Result<AssetImportResult>::Failure(published.error);
		    }
		    Expect(Take(files.Exists(manifest)) == existed && (!existed || Take(files.Read(manifest)) == oldManifest),
		           "Candidate generation changed the real Library manifest before input validation");
		    if (failAfterStaging)
		    {
			    return Result<AssetImportResult>::Failure("Injected postprocess failure after candidate generation");
		    }
		    if (changeInput)
		    {
			    Done(files.WriteAtomic(source, {9}));
		    }
		    if (changeRules)
		    {
			    Done(importer.Register(".unused", [](AssetDatabase&, const VirtualPath&, bool)
			                           { return Result<AssetImportResult>::Failure("Unused callback"); }));
		    }
		    const auto status = Take(candidate.CurrentImport(path));
		    return Result<AssetImportResult>::Success({guid, status.artifacts, status.dependencies, false});
	    }));
	const auto first = Take(importer.Import(*database, source));
	Expect(first.artifacts.size() == 2 && Take(files.Read(first.artifacts[0].path)) == Vector<UInt8>({1}),
	       "Candidate outputs did not publish as one collection");
	const auto dependencyPath = Take(database->Resolve(dependencyGuid));
	Expect(Take(files.Read(dependencyPath)) == Vector<UInt8>({1, 99}),
	       "Model-style dependent import was left in the private candidate Library");
	const auto firstManifest = Take(files.Read(manifest));
	const auto firstFiles = Take(library->List());
	output = 2;
	failAfterStaging = true;
	Expect(!importer.Import(*database, source, true) && Take(files.Read(manifest)) == firstManifest &&
	           Take(library->List()) == firstFiles && Take(files.Read(dependencyPath)) == Vector<UInt8>({1, 99}),
	       "Failed postprocessing published candidate files or replaced the prior collection");
	failAfterStaging = false;
	changeRules = true;
	const auto previousGeneration = importer.Generation();
	Expect(!importer.Import(*database, source, true) && importer.Generation() > previousGeneration &&
	           Take(files.Read(manifest)) == firstManifest && Take(library->List()) == firstFiles,
	       "A candidate from an obsolete registration snapshot was published");
	changeRules = false;
	changeInput = true;
	Expect(!importer.Import(*database, source, true) && Take(files.Read(manifest)) == firstManifest &&
	           Take(library->List()) == firstFiles,
	       "An obsolete candidate was published before checking changed source inputs");
	changeInput = false;
	Done(files.WriteAtomic(source, {1}));
	output = 3;
	library->failManifest = true;
	Expect(!importer.Import(*database, source, true) && Take(files.Read(manifest)) == firstManifest &&
	           Take(files.Read(first.artifacts[0].path)) == Vector<UInt8>({1}) && Take(library->List()) == firstFiles,
	       "Manifest failure overwrote previous content or leaked partially staged artifacts");
	library->failManifest = false;
	const auto next = Take(importer.Import(*database, source, true));
	Expect(next.artifacts.size() == 2 && next.artifacts[0].path != first.artifacts[0].path &&
	           Take(files.Read(next.artifacts[0].path)) == Vector<UInt8>({3}) &&
	           Take(files.Read(first.artifacts[0].path)) == Vector<UInt8>({1}),
	       "Force reimport ignored new output or overwrote resources still using old content");
	const auto writesBeforeRepeat = library->writes;
	Take(importer.Import(*database, source));
	Expect(library->writes == writesBeforeRepeat, "Unchanged complete output rewrote persistent Library files");
}
} // namespace

int main()
{
	try
	{
		TestCandidatePublication();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
