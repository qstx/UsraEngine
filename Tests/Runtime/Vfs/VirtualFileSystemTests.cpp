#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <Vfs/VirtualFileSystem.hpp>

#include <fstream>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

Ursa::Vector<Ursa::UInt8> BytesOf(const std::string& text)
{
	Ursa::Vector<Ursa::UInt8> bytes;
	bytes.reserve(text.size());
	for (char character : text)
	{
		bytes.push_back(static_cast<Ursa::UInt8>(character));
	}
	return bytes;
}

std::string TextOf(const Ursa::Vector<Ursa::UInt8>& bytes)
{
	return std::string(bytes.begin(), bytes.end());
}

void WriteFile(const std::filesystem::path& path, const std::string& text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output << text;
	Expect(static_cast<bool>(output), "Cannot write a test fixture");
}

// Directory symlinks need elevation on Windows, but junctions do not; both are
// reparse points that resolve through std::filesystem::canonical.
bool CreateEscapeLink(const std::filesystem::path& link, const std::filesystem::path& target)
{
#ifdef _WIN32
	const std::string command = "cmd /c mklink /J \"" + link.string() + "\" \"" + target.string() + "\" > nul 2>&1";
	return std::system(command.c_str()) == 0;
#else
	std::error_code error;
	std::filesystem::create_directory_symlink(target, link, error);
	return !error;
#endif
}

// The example published in Engine/README.md, kept compiling and running here.
Ursa::Result<void> OpenResourcesExample(const std::filesystem::path& engineRoot,
                                        const std::filesystem::path& engineMirror,
                                        const std::filesystem::path& projectAssets,
                                        const std::filesystem::path& userData)
{
	Ursa::RuntimeMountProfileConfig profile;
	profile.mode = Ursa::RuntimeMountMode::Editor;
	profile.applicationId = "UrsaEditor";
	profile.engineDirectories = {engineRoot, engineMirror};
	profile.projectAssetsDirectory = projectAssets;
	profile.projectLibraryDirectory = projectAssets.parent_path() / "Library";
	profile.userDataDirectoryOverride = userData;
	auto vfs = Ursa::OpenRuntimeFileSystem(profile);
	if (!vfs)
	{
		return Ursa::Result<void>::Failure(vfs.error);
	}
	auto shader = Ursa::VirtualPath::Parse("ursa:Shaders/Renderer3D/Cube.vert.spv");
	if (!shader)
	{
		return Ursa::Result<void>::Failure(shader.error);
	}
	auto bytes = vfs.value.Read(shader.value);
	if (!bytes)
	{
		return Ursa::Result<void>::Failure(bytes.error);
	}
	auto assetPath = Ursa::VirtualPath::Parse("prj:Generated/ShaderCopy.bin");
	auto logPath = Ursa::VirtualPath::Parse("user:Logs/UrsaEditor.log");
	if (!assetPath || !logPath)
	{
		return Ursa::Result<void>::Failure(!assetPath ? assetPath.error : logPath.error);
	}
	auto saved = vfs.value.WriteAtomic(assetPath.value, bytes.value);
	if (!saved)
	{
		return saved;
	}
	return vfs.value.Append(logPath.value, BytesOf("example completed\n"));
}

void TestParsing()
{
	const auto valid = Ursa::VirtualPath::Parse("ursa:Shaders/Renderer3D/Cube.vert");
	Expect(static_cast<bool>(valid) && valid.value.scheme == "ursa" &&
	           valid.value.relative == "Shaders/Renderer3D/Cube.vert" &&
	           valid.value.ToString() == "ursa:Shaders/Renderer3D/Cube.vert",
	       "Valid virtual path did not round trip");

	const auto normalized = Ursa::VirtualPath::Parse("PRJ:Snake\\.\\Board.ursaprefab");
	Expect(static_cast<bool>(normalized) && normalized.value.scheme == "prj" &&
	           normalized.value.relative == "Snake/Board.ursaprefab",
	       "Virtual path normalization failed");

	Expect(!Ursa::VirtualPath::Parse("Shaders/Cube.vert"), "Accepted a path without a scheme");
	Expect(!Ursa::VirtualPath::Parse(":Cube.vert"), "Accepted an empty scheme");
	Expect(!Ursa::VirtualPath::Parse("C:/Engine/Assets/Cube.vert"), "Accepted a host drive letter");
	Expect(!Ursa::VirtualPath::Parse("ursa:/Shaders/Cube.vert"), "Accepted an absolute relative path");
	Expect(!Ursa::VirtualPath::Parse("ursa:../outside.ursatex"), "Accepted a parent-directory escape");
	Expect(!Ursa::VirtualPath::Parse("ursa:Shaders/../Cube.vert"), "Accepted a parent-directory segment");
	Expect(!Ursa::VirtualPath::Parse("ursa:"), "Accepted a missing relative path");
}

void TestReadWriteAndPermissions(const std::filesystem::path& root)
{
	const auto engineRoot = root / "engine";
	const auto projectRoot = root / "project";
	std::filesystem::create_directories(engineRoot);
	std::filesystem::create_directories(projectRoot);
	WriteFile(engineRoot / "Shaders" / "Cube.vert", "engine-shader");

	Ursa::VirtualFileSystem vfs;
	Expect(static_cast<bool>(vfs.Mount("ursa", engineRoot, Ursa::MountAccess::ReadOnly)),
	       "Cannot mount the engine root");
	Expect(static_cast<bool>(vfs.Mount("prj", projectRoot, Ursa::MountAccess::ReadWrite)),
	       "Cannot mount the project root");
	Expect(!vfs.Mount("ursa", root / "missing", Ursa::MountAccess::ReadOnly), "Mounted a missing directory");

	const auto shader = Ursa::VirtualPath::Parse("ursa:Shaders/Cube.vert").value;
	Expect(TextOf(vfs.Read(shader).value) == "engine-shader", "Read returned the wrong bytes");
	Expect(vfs.Exists(shader).value, "Existing file was reported missing");
	Expect(TextOf(vfs.Read(shader).value) == "engine-shader", "resolveToDisk returned the wrong host path");

	const auto saved = Ursa::VirtualPath::Parse("prj:Snake/new.ursatex").value;
	Expect(static_cast<bool>(vfs.Write(saved, BytesOf("saved"))), "Cannot write to a writable mount");
	Expect(TextOf(vfs.Read(saved).value) == "saved", "Written bytes did not round trip");
	Expect(TextOf(vfs.ReadRange(saved, 1, 3).value) == "ave", "Range read returned the wrong bytes");
	Expect(!vfs.ReadRange(saved, 4, 2), "An out-of-bounds range read succeeded");
	Expect(static_cast<bool>(vfs.Append(saved, BytesOf("-tail"))), "Cannot append to a writable mount");
	Expect(TextOf(vfs.Read(saved).value) == "saved-tail", "Appended bytes did not round trip");
	Expect(static_cast<bool>(vfs.WriteAtomic(saved, BytesOf("atomic"))), "Cannot atomically replace a resource");
	Expect(TextOf(vfs.Read(saved).value) == "atomic", "Atomic write did not replace the whole resource");
	std::filesystem::create_directory(projectRoot / "directory-target");
	const auto directoryTarget = Ursa::VirtualPath::Parse("prj:directory-target").value;
	Expect(!vfs.WriteAtomic(directoryTarget, BytesOf("must-not-replace-directory")) &&
	           std::filesystem::is_directory(projectRoot / "directory-target"),
	       "Atomic write failure changed a directory target");
	Expect(!vfs.Write(shader, BytesOf("changed")), "Wrote into a read-only mount");
	Expect(TextOf(vfs.Read(shader).value) == "engine-shader", "Read-only write changed the target file");

	Expect(!vfs.Read(Ursa::VirtualPath::Parse("mod:anything.ursatex").value),
	       "Read from an unmounted scheme succeeded");
	Expect(!vfs.Write(Ursa::VirtualPath::Parse("mod:anything.ursatex").value, BytesOf("x")),
	       "Write into an unmounted scheme succeeded");
	Expect(!vfs.Read(Ursa::VirtualPath::Parse("prj:Snake/missing.ursatex").value), "Missing resource was accepted");
}

void TestOverlayAndShadowing(const std::filesystem::path& root)
{
	const auto sourceRoot = root / "overlay-source";
	const auto generatedRoot = root / "overlay-generated";
	WriteFile(sourceRoot / "Shaders" / "Renderer3D" / "Cube.vert", "source-shader");
	WriteFile(generatedRoot / "Shaders" / "Renderer3D" / "Cube.vert.spv", "generated-shader");
	WriteFile(generatedRoot / "Shaders" / "Renderer3D" / "Cube.vert", "shadowed-shader");
	std::filesystem::create_directories(root / "overlay-project");
	auto documentedExample =
	    OpenResourcesExample(sourceRoot, generatedRoot, root / "overlay-project", root / "example-user");
	if (!documentedExample)
	{
		throw std::runtime_error(Ursa::ToStdString(documentedExample.error));
	}

	Ursa::VirtualFileSystem vfs;
	Expect(static_cast<bool>(vfs.Mount("ursa", sourceRoot, Ursa::MountAccess::ReadOnly)) &&
	           static_cast<bool>(vfs.Mount("ursa", generatedRoot, Ursa::MountAccess::ReadOnly)),
	       "Cannot mount the overlay roots");

	const auto roots = vfs.Roots("ursa");
	Expect(static_cast<bool>(roots) && roots.value.size() == 2, "Overlay did not keep both roots");

	const auto source = Ursa::VirtualPath::Parse("ursa:Shaders/Renderer3D/Cube.vert").value;
	const auto generated = Ursa::VirtualPath::Parse("ursa:Shaders/Renderer3D/Cube.vert.spv").value;
	Expect(TextOf(vfs.Read(source).value) == "source-shader", "Source asset did not resolve to the first root");
	Expect(TextOf(vfs.Read(generated).value) == "generated-shader",
	       "Generated asset did not resolve to the second root");

	auto listed = vfs.List("ursa");
	Expect(static_cast<bool>(listed) && listed.value.size() == 2, "Overlay listing returned the wrong entry count");
	Expect(listed.value[0].path.ToString() == "ursa:Shaders/Renderer3D/Cube.vert" &&
	           listed.value[1].path.ToString() == "ursa:Shaders/Renderer3D/Cube.vert.spv",
	       "Overlay listing returned unexpected virtual paths");
	Expect(listed.value[0].shadowedSources.size() == 1 &&
	           listed.value[0].shadowedSources.front() == "directory:overlay-generated",
	       "Shadowed provider source was not reported");
	Expect(listed.value[1].shadowedSources.empty(), "Unshadowed entry reported a shadowed provider");
	Expect(!vfs.List("mod"), "Listing an unmounted scheme succeeded");
}

void TestTraversalIsolation(const std::filesystem::path& root)
{
	const auto projectRoot = root / "traversal";
	const auto outsideRoot = root / "outside";
	WriteFile(projectRoot / "Snake" / "Snake.ursascene", "scene");
	WriteFile(outsideRoot / "secret.ursatex", "secret");

	Ursa::VirtualFileSystem vfs;
	Expect(static_cast<bool>(vfs.Mount("prj", projectRoot, Ursa::MountAccess::ReadWrite)),
	       "Cannot mount the traversal fixture");

	Expect(CreateEscapeLink(projectRoot / "escape", outsideRoot), "Cannot create the escape-link fixture");
	auto listed = vfs.List("prj");
	Expect(static_cast<bool>(listed) && listed.value.size() == 1 &&
	           listed.value.front().path.ToString() == "prj:Snake/Snake.ursascene",
	       "Traversal followed a link out of its mount root");
	const auto throughLink = Ursa::VirtualPath::Parse("prj:escape/secret.ursatex").value;
	Expect(!vfs.Read(throughLink), "resolveToDisk followed a link out of its mount root");
	Expect(!vfs.Read(throughLink), "read followed a link out of its mount root");

	const auto escaped = Ursa::VirtualPath::Parse("prj:Snake/../../secret.ursatex");
	Expect(!escaped, "Traversal escape was accepted at parse time");
}

void TestEngineAssetRootResolution(const std::filesystem::path& root)
{
	const auto executable = root / "bin" / "UrsaEditor.exe";
	const auto besideExecutable = root / "bin" / "Assets";
	const auto mirror = root / "cache" / "Assets";
	std::filesystem::create_directories(besideExecutable);
	std::filesystem::create_directories(mirror);

	Ursa::EngineAssetRootCandidates candidates;
	candidates.executablePath = executable;
	candidates.buildMirror = mirror;

	const auto adjacent = Ursa::ResolveEngineAssetRoot(candidates);
	Expect(static_cast<bool>(adjacent) && adjacent.value == besideExecutable,
	       "Executable-adjacent assets did not win over the build mirror");

	std::filesystem::remove(besideExecutable);
	const auto fallback = Ursa::ResolveEngineAssetRoot(candidates);
	Expect(!fallback, "Removed build mirror fallback was still accepted");

	candidates.explicitRoot = root / "custom-assets";
	Expect(!Ursa::ResolveEngineAssetRoot(candidates), "A missing explicit root was accepted");
	std::filesystem::create_directories(candidates.explicitRoot);
	const auto explicitRoot = Ursa::ResolveEngineAssetRoot(candidates);
	Expect(static_cast<bool>(explicitRoot) && explicitRoot.value == candidates.explicitRoot,
	       "Explicit engine asset root did not win");

	std::filesystem::remove_all(candidates.explicitRoot);
	std::filesystem::remove_all(mirror);
	candidates.explicitRoot.clear();
	Expect(!Ursa::ResolveEngineAssetRoot(candidates), "Resolution succeeded although no candidate directory exists");
}

void TestRuntimeMountProfiles(const std::filesystem::path& root)
{
	const auto engineRoot = root / "profile-engine";
	const auto projectRoot = root / "profile-project";
	const auto editorLibraryRoot = root / "profile-editor-library";
	const auto editorUserRoot = root / "profile-editor-user";
	const auto looseUserRoot = root / "profile-loose-user";
	const auto packageEngineRoot = root / "profile-package-engine";
	const auto packageProjectRoot = root / "profile-package-project";
	const auto packageUserRoot = root / "profile-package-user";
	WriteFile(engineRoot / "engine.txt", "engine");
	WriteFile(projectRoot / "project.txt", "project");
	WriteFile(packageEngineRoot / "engine.txt", "packed-engine");
	WriteFile(packageProjectRoot / "project.txt", "packed-project");

	Ursa::RuntimeMountProfileConfig editorConfig;
	editorConfig.mode = Ursa::RuntimeMountMode::Editor;
	editorConfig.engineDirectories.push_back(engineRoot);
	editorConfig.projectAssetsDirectory = projectRoot;
	editorConfig.projectLibraryDirectory = editorLibraryRoot;
	editorConfig.userDataDirectoryOverride = editorUserRoot;
	auto editor = Ursa::OpenRuntimeFileSystem(editorConfig);
	Expect(static_cast<bool>(editor), "Cannot create the Editor mount profile");
	const auto engineAsset = Ursa::VirtualPath::Parse("ursa:engine.txt").value;
	const auto projectAsset = Ursa::VirtualPath::Parse("prj:project.txt").value;
	const auto editorArtifact = Ursa::VirtualPath::Parse("lib:Artifacts/test.bin").value;
	const auto editorLog = Ursa::VirtualPath::Parse("user:Logs/editor.log").value;
	Expect(TextOf(editor.value.Read(engineAsset).value) == "engine" &&
	           TextOf(editor.value.Read(projectAsset).value) == "project",
	       "Editor mount profile did not expose Engine and project resources");
	Expect(static_cast<bool>(editor.value.Write(projectAsset, BytesOf("edited"))) &&
	           static_cast<bool>(editor.value.Write(editorArtifact, BytesOf("artifact"))) &&
	           TextOf(editor.value.Read(editorArtifact).value) == "artifact" &&
	           !editor.value.Write(engineAsset, BytesOf("changed")) &&
	           static_cast<bool>(editor.value.Append(editorLog, BytesOf("log"))),
	       "Editor mount profile has incorrect resource or user-data access");
	const auto editorUserRoots = editor.value.Roots(Ursa::userDataScheme);
	Expect(editorUserRoots && editorUserRoots.value.size() == 1 && editorUserRoots.value.front() == editorUserRoot,
	       "Editor user-data override was not mounted");

	Ursa::RuntimeMountProfileConfig looseConfig = editorConfig;
	looseConfig.mode = Ursa::RuntimeMountMode::LooseRuntime;
	looseConfig.userDataDirectoryOverride = looseUserRoot;
	auto loose = Ursa::OpenRuntimeFileSystem(looseConfig);
	Expect(loose && !loose.value.IsMounted(Ursa::editorLibraryScheme) &&
	           !loose.value.Write(projectAsset, BytesOf("runtime edit")) &&
	           static_cast<bool>(loose.value.Write(Ursa::VirtualPath::Parse("user:save.dat").value, BytesOf("save"))),
	       "Loose runtime mount profile has incorrect project or user-data access");

	auto packageEngine = Ursa::DirectoryFileProvider::Open(packageEngineRoot);
	auto packageProject = Ursa::DirectoryFileProvider::Open(packageProjectRoot);
	Expect(packageEngine && packageProject, "Cannot create packaged profile provider fixtures");
	Ursa::RuntimeMountProfileConfig packagedConfig;
	packagedConfig.mode = Ursa::RuntimeMountMode::PackagedRuntime;
	packagedConfig.userDataDirectoryOverride = packageUserRoot;
	packagedConfig.packagedProviders.push_back({Ursa::engineScheme, packageEngine.value});
	packagedConfig.packagedProviders.push_back({Ursa::projectScheme, packageProject.value});
	auto packaged = Ursa::OpenRuntimeFileSystem(packagedConfig);
	Expect(packaged && !packaged.value.IsMounted(Ursa::editorLibraryScheme) &&
	           TextOf(packaged.value.Read(engineAsset).value) == "packed-engine" &&
	           TextOf(packaged.value.Read(projectAsset).value) == "packed-project",
	       "Packaged mount profile did not expose its scheme providers");
	Expect(
	    !packaged.value.Write(projectAsset, BytesOf("overwrite")) &&
	        static_cast<bool>(packaged.value.Write(Ursa::VirtualPath::Parse("user:save.dat").value, BytesOf("save"))),
	    "Packaged content was writable or user data was not writable");
	Expect(!Ursa::ResolveUserDataDirectory("../outside"),
	       "Invalid application ID was accepted for user-data resolution");
}
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::temp_directory_path() /
		                  ("UrsaVirtualFileSystemTests-" +
		                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(root);
		TestParsing();
		TestReadWriteAndPermissions(root);
		TestOverlayAndShadowing(root);
		TestTraversalIsolation(root);
		TestEngineAssetRootResolution(root);
		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
