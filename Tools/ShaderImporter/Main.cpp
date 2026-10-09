#include <Shader/ShaderImporter.hpp>
#include <Vfs/BuildResourceManifest.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/MemoryFileProvider.hpp>

#include <Core/StringInterop.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
struct Arguments
{
	std::filesystem::path sourceRoot;
	std::filesystem::path outputRoot;
	std::filesystem::path cacheRoot;
	std::filesystem::path dxc;
	std::filesystem::path spirvValidator;
	std::filesystem::path spirvCross;
	std::filesystem::path managedRoot;
	Ursa::String configuration;
};

Arguments ParseArguments(int argc, char** argv)
{
	Arguments result;
	for (int index = 1; index < argc; ++index)
	{
		const std::string_view name(argv[index]);
		if (index + 1 >= argc)
		{
			throw std::runtime_error("Missing value for argument " + std::string(name));
		}
		if (name == "--configuration")
		{
			result.configuration = argv[++index];
			continue;
		}
		const std::filesystem::path value = std::filesystem::u8path(argv[++index]);
		if (name == "--source-root")
		{
			result.sourceRoot = value;
		}
		else if (name == "--output-root")
		{
			result.outputRoot = value;
		}
		else if (name == "--cache-root")
		{
			result.cacheRoot = value;
		}
		else if (name == "--dxc")
		{
			result.dxc = value;
		}
		else if (name == "--spirv-val")
		{
			result.spirvValidator = value;
		}
		else if (name == "--spirv-cross")
		{
			result.spirvCross = value;
		}
		else if (name == "--managed-root")
		{
			result.managedRoot = value;
		}
		else
		{
			throw std::runtime_error("Unknown argument " + std::string(name));
		}
	}
	if (result.sourceRoot.empty() || result.outputRoot.empty() || result.cacheRoot.empty() || result.dxc.empty() ||
	    result.spirvValidator.empty() || result.spirvCross.empty() || result.configuration.empty())
	{
		throw std::runtime_error(
		    "Usage: UrsaShaderImporter --source-root PATH --output-root PATH --cache-root PATH --dxc PATH "
		    "--spirv-val PATH --spirv-cross PATH --configuration CONFIG [--managed-root PATH]");
	}
	return result;
}

void CreateDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	std::filesystem::create_directories(path, error);
	if (error)
	{
		throw std::runtime_error("Cannot create directory " + path.u8string() + ": " + error.message());
	}
}
} // namespace

int main(int argc, char** argv)
{
	using namespace Ursa;
	VirtualFileSystem published;
	try
	{
		const Arguments arguments = ParseArguments(argc, argv);
		CreateDirectory(arguments.outputRoot);
		CreateDirectory(arguments.cacheRoot);
		auto publishedMount = published.Mount("built", arguments.outputRoot, MountAccess::ReadWrite);
		if (!publishedMount)
		{
			throw std::runtime_error(ToStdString(publishedMount.error));
		}
		Ursa::VirtualFileSystem filesystem;
		auto sourceMount = filesystem.Mount("ursa", arguments.sourceRoot, Ursa::MountAccess::ReadOnly);
		if (!sourceMount)
		{
			throw std::runtime_error(Ursa::ToStdString(sourceMount.error));
		}
		auto outputMount =
		    filesystem.MountProvider("generated", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite);
		if (!outputMount)
		{
			throw std::runtime_error(Ursa::ToStdString(outputMount.error));
		}
		auto cacheMount = filesystem.Mount("shadercache", arguments.cacheRoot, Ursa::MountAccess::ReadWrite);
		if (!cacheMount)
		{
			throw std::runtime_error(Ursa::ToStdString(cacheMount.error));
		}
		Ursa::Shader::DxcShaderCompiler compiler({arguments.dxc, arguments.spirvValidator, arguments.spirvCross});
		auto version = compiler.Version();
		if (!version)
		{
			throw std::runtime_error(Ursa::ToStdString(version.error));
		}
		auto inputs = SnapshotBuildResourceFiles(filesystem, "ursa");
		if (!inputs)
		{
			throw std::runtime_error(ToStdString(inputs.error));
		}
		const auto executable = CurrentExecutablePath();
		auto producerMount = filesystem.Mount("producer", executable.parent_path(), MountAccess::ReadOnly);
		if (!producerMount)
		{
			throw std::runtime_error(ToStdString(producerMount.error));
		}
		auto producerBytes =
		    filesystem.Read(VirtualPath::Parse("producer:" + PathToUrsaString(executable.filename())).value);
		if (!producerBytes)
		{
			throw std::runtime_error(ToStdString(producerBytes.error));
		}
		const String preparationVersion =
		    version.value + "\nUrsaResourceProducer " + BuildResourceFingerprint(producerBytes.value);
		Vector<BuildResourceFile> managedRecords;
		if (!arguments.managedRoot.empty())
		{
			auto managedMount = filesystem.Mount("managed", arguments.managedRoot, MountAccess::ReadOnly);
			if (!managedMount)
			{
				throw std::runtime_error(ToStdString(managedMount.error));
			}
			auto managedRoots = filesystem.Roots("managed");
			if (!managedRoots || managedRoots.value.empty() ||
			    PathToUrsaString(managedRoots.value.front().filename()) != arguments.configuration)
			{
				throw std::runtime_error("Managed input directory must identify the requested build configuration");
			}
			for (const String name : {"Ursa.ManagedHost.dll", "Ursa.ManagedHost.deps.json",
			                          "Ursa.ManagedHost.runtimeconfig.json", "Ursa.Runtime.dll"})
			{
				auto bytes = filesystem.Read(VirtualPath::Parse("managed:" + name).value);
				if (!bytes)
				{
					throw std::runtime_error(ToStdString(bytes.error));
				}
				managedRecords.push_back({"Managed/" + name, BuildResourceFingerprint(bytes.value),
				                          static_cast<UInt64>(bytes.value.size())});
				auto saved = filesystem.WriteAtomic(VirtualPath::Parse("generated:Managed/" + name).value, bytes.value);
				if (!saved)
				{
					throw std::runtime_error(ToStdString(saved.error));
				}
			}
		}
		auto current = ValidateBuildResources(published, "built", arguments.configuration,
		                                      !arguments.managedRoot.empty(), &filesystem, "ursa");
		bool managedMatches = true;
		if (current)
		{
			for (const auto& record : managedRecords)
			{
				managedMatches &= std::any_of(current.value.outputs.begin(), current.value.outputs.end(),
				                              [&](const auto& output)
				                              {
					                              return output.path == record.path &&
					                                     output.fingerprint == record.fingerprint &&
					                                     output.size == record.size;
				                              });
			}
		}
		if (current && current.value.compilerVersion == preparationVersion &&
		    current.value.managed == !arguments.managedRoot.empty() && managedMatches)
		{
			std::cout << "Engine resources are current for " << arguments.configuration.c_str() << '\n';
			return 0;
		}
		auto oldManifest = published.Exists(VirtualPath::Parse("built:BuildManifest.json").value);
		if (oldManifest && oldManifest.value)
		{
			auto removed = published.Remove(VirtualPath::Parse("built:BuildManifest.json").value);
			if (!removed)
			{
				throw std::runtime_error(ToStdString(removed.error));
			}
		}
		auto imported =
		    Ursa::Shader::ShaderImporter::ImportAll(filesystem, compiler, version.value, "ursa", "generated",
		                                            Ursa::VirtualPath::Parse("shadercache:Shader").value);
		if (!imported)
		{
			throw std::runtime_error(Ursa::ToStdString(imported.error));
		}
		for (const Ursa::Shader::ShaderAsset& shader : imported.value)
		{
			auto sourcePath = Ursa::VirtualPath::Parse(shader.sourcePath);
			if (!sourcePath)
			{
				throw std::runtime_error(Ursa::ToStdString(sourcePath.error));
			}
			auto sourceMetadataPath = Ursa::VirtualPath::Parse(shader.sourcePath + ".meta");
			if (!sourceMetadataPath)
			{
				throw std::runtime_error(Ursa::ToStdString(sourceMetadataPath.error));
			}
			auto metadata = filesystem.Read(sourceMetadataPath.value);
			if (!metadata)
			{
				throw std::runtime_error("Generated Shader assets require source .meta GUIDs: " +
				                         Ursa::ToStdString(sourcePath.value.ToString()));
			}
			std::filesystem::path relative = Ursa::PathFromUrsaString(sourcePath.value.relative);
			relative.replace_extension(".ursashaderasset");
			auto artifactPath = Ursa::VirtualPath::Parse("generated:" + Ursa::PathToUrsaString(relative));
			if (!artifactPath)
			{
				throw std::runtime_error(Ursa::ToStdString(artifactPath.error));
			}
			auto outputMetadataPath = Ursa::VirtualPath::Parse(artifactPath.value.ToString() + ".meta");
			if (!outputMetadataPath)
			{
				throw std::runtime_error(Ursa::ToStdString(outputMetadataPath.error));
			}
			auto copiedMetadata = filesystem.WriteAtomic(outputMetadataPath.value, metadata.value);
			if (!copiedMetadata)
			{
				throw std::runtime_error(Ursa::ToStdString(copiedMetadata.error));
			}
		}
		auto outputs = SnapshotBuildResourceFiles(filesystem, "generated");
		auto finalInputs = SnapshotBuildResourceFiles(filesystem, "ursa");
		if (!outputs || !finalInputs || !EqualBuildResourceFiles(inputs.value, finalInputs.value))
		{
			throw std::runtime_error("Engine resource inputs changed during preparation or outputs could not be read");
		}
		for (const auto& output : outputs.value)
		{
			auto bytes = filesystem.Read(VirtualPath::Parse("generated:" + output.path).value);
			auto saved = bytes ? published.WriteAtomic(VirtualPath::Parse("built:" + output.path).value, bytes.value)
			                   : Result<void>::Failure(bytes.error);
			if (!saved)
			{
				throw std::runtime_error(ToStdString(saved.error));
			}
		}
		auto previousFiles = published.List("built");
		if (!previousFiles)
		{
			throw std::runtime_error(ToStdString(previousFiles.error));
		}
		for (const auto& previous : previousFiles.value)
		{
			const auto extension = PathFromUrsaString(previous.path.relative).extension();
			const bool generatedShader = previous.path.relative.find("Shaders/") == 0 &&
			                             (extension == ".ursashaderasset" || extension == ".meta");
			const bool generatedManaged = previous.path.relative.find("Managed/") == 0;
			if ((generatedShader || generatedManaged) &&
			    std::none_of(outputs.value.begin(), outputs.value.end(),
			                 [&](const auto& output) { return output.path == previous.path.relative; }))
			{
				auto removed = published.Remove(previous.path);
				if (!removed)
				{
					throw std::runtime_error(ToStdString(removed.error));
				}
			}
		}
		BuildResourceManifest manifest;
		manifest.configuration = arguments.configuration;
		manifest.compilerVersion = preparationVersion;
		manifest.managed = !arguments.managedRoot.empty();
		manifest.inputs = std::move(inputs.value);
		manifest.outputs = std::move(outputs.value);
		auto savedManifest = WriteBuildResourceManifest(published, "built", manifest);
		if (!savedManifest)
		{
			throw std::runtime_error(ToStdString(savedManifest.error));
		}
		std::cout << "Prepared " << imported.value.size() << " Shader assets and engine resources for "
		          << arguments.configuration.c_str() << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		if (published.IsMounted("built"))
		{
			auto exists = published.Exists(VirtualPath::Parse("built:BuildManifest.json").value);
			if (exists && exists.value)
			{
				published.Remove(VirtualPath::Parse("built:BuildManifest.json").value);
			}
		}
		std::cerr << error.what() << '\n';
		return 1;
	}
}
