#include <Assets/ManagedImportEnvironment.hpp>
#include <Assets/ScriptBuildService.hpp>
#include <Assets/ManagedImportRules.hpp>
#include <Assets/ScriptedAssetImporter.hpp>
#include <Vfs/BuildResourceManifest.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace Ursa::Editor
{
namespace
{
std::atomic<UInt64> nextGeneration{1};

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

VirtualPath Path(const String& text)
{
	return Take(VirtualPath::Parse(text));
}
} // namespace

struct ManagedImportEnvironment::Impl
{
	VirtualFileSystem files;
	String configuration;
	bool buildScripts = true;
	UInt64 generation = nextGeneration.fetch_add(1);
	std::atomic<UInt64> activeGeneration{generation};
	String sourceFingerprint;
	String compiledFingerprint;
	Vector<ScriptTypeDescriptor> types;
	std::shared_ptr<const AssetImportRules> rules;
	std::unique_ptr<AssetDatabase> database;
	std::unique_ptr<ScriptBuildService> compiler;
	Vector<RuntimeContentInput> closure;
	std::thread worker;
	std::mutex mutex;
	std::condition_variable changed;
	std::deque<std::function<void()>> jobs;
	bool stopping = false;
#if defined(URSA_ENABLE_DOTNET)
	std::unique_ptr<Scripting::DotNetHost> host;
	UInt64 session = 0;
#endif
	~Impl()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
		}
		changed.notify_one();
		if (worker.joinable())
		{
			worker.join();
		}
	}

	template <class T, class F> Result<T> Call(F action)
	{
		if (worker.get_id() == std::this_thread::get_id())
		{
			return Result<T>::Failure("Synchronous managed import worker reentry is forbidden");
		}
		auto completion = std::make_shared<std::promise<Result<T>>>();
		auto result = completion->get_future();
		std::function<Result<T>()> operation(std::move(action));
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (stopping)
			{
				return Result<T>::Failure("Managed import worker is closed");
			}
			jobs.emplace_back(
			    [completion, operation = std::move(operation)]() mutable
			    {
				    Result<T> value;
				    {
					    auto execute = std::move(operation);
					    try
					    {
						    value = execute();
					    }
					    catch (const std::exception& error)
					    {
						    value = Result<T>::Failure(ToUrsaString(error.what()));
					    }
				    }
				    // Release project/session captures before waking a caller that may release the environment.
				    operation = {};
				    completion->set_value(std::move(value));
			    });
		}
		changed.notify_one();
		return result.get();
	}

	Result<bool> Current()
	{
		if (!activeGeneration.load())
		{
			return Result<bool>::Success(false);
		}
		if (Take(compiler->SourceFingerprint()) != sourceFingerprint)
		{
			activeGeneration.store(0);
			return Result<bool>::Success(false);
		}
		for (const auto& input : closure)
		{
			const auto current = CaptureRuntimeContentInput(files, input.path);
			if (!current || current.value.fingerprint != input.fingerprint)
			{
				activeGeneration.store(0);
				return Result<bool>::Success(false);
			}
		}
		return Result<bool>::Success(true);
	}

	void Initialize()
	{
#if defined(URSA_ENABLE_DOTNET)
		const auto hostFramework = std::filesystem::u8path(URSA_DOTNET_RUNTIME_ASSEMBLY_PATH).parent_path();
		const auto compileFramework = hostFramework.parent_path() / ToStdString(configuration);
		Done(files.Mount("importframework", hostFramework, MountAccess::ReadOnly));
		database = Take(AssetDatabase::Open(files));
		Done(database->Refresh());
		compiler =
		    Take(ScriptBuildService::Open(files, *database, std::filesystem::u8path(URSA_DOTNET_EXECUTABLE_PATH),
		                                  compileFramework / "Ursa.Runtime.dll", compileFramework / "Ursa.Editor.dll",
		                                  std::filesystem::u8path(URSA_DOTNET_CATALOG_GENERATOR_PATH),
		                                  std::filesystem::u8path(URSA_DOTNET_REFERENCE_PACK_PATH)));
		const auto projects = compiler->GenerateProjects();
		if (!projects)
		{
			throw std::runtime_error(
			    ToStdString(projects.error) +
			    "\nImport rules are compiled before asset generation. Types requiring this round's "
			    "generated C# output create an unsupported compile/import dependency cycle.");
		}
		types = compiler->ScriptTypes();
		bool managed = false;
		for (const auto& type : types)
		{
			managed |= type.kind == ScriptTypeKind::ScriptedImporter || type.kind == ScriptTypeKind::AssetPostprocessor;
		}
		for (const auto& source : database->ScriptSources())
		{
			managed |= source.group == ScriptAssemblyGroup::Editor;
		}
		if (!managed)
		{
			sourceFingerprint = Take(compiler->SourceFingerprint());
			compiledFingerprint = sourceFingerprint;
			rules = std::make_shared<const AssetImportRules>(Take(AssetImportRules::Create({}, {}, {}, generation)));
			return;
		}
		if (!buildScripts && !Take(compiler->IsBuildCurrent(ScriptAssemblyGroup::Editor, configuration)))
		{
			throw std::runtime_error("Managed import rules are not compiled for this configuration; run "
			                         "UrsaAssetImport without --no-scripts");
		}
		const auto built = Take(compiler->Build(ScriptAssemblyGroup::Editor, configuration));
		if (!built.succeeded)
		{
			throw std::runtime_error(ToStdString(built.diagnostics) +
			                         "\nImport rules must compile from source scripts and framework assemblies before "
			                         "asset generation. A type that requires output from this import round creates a "
			                         "compile/import dependency cycle and is unsupported.");
		}
		types = compiler->ScriptTypes();
		sourceFingerprint = Take(compiler->SourceFingerprint());
		nlohmann::json fingerprint{{"source", ToStdString(sourceFingerprint)}, {"closure", nlohmann::json::array()}};
		Set<String> paths;
		for (const String group : {"Game", "Editor"})
		{
			const auto manifestPath = Path("lib:ScriptBuild/Manifests/" + group + ".json");
			const auto manifest = nlohmann::json::parse(ToStdString(Take(files.ReadText(manifestPath))));
			paths.insert(manifestPath.ToString());
			paths.insert(ToUrsaString(manifest.at("TypeCatalog").get<std::string>()));
			for (const auto& assembly : manifest.at("Assemblies"))
			{
				paths.insert(ToUrsaString(assembly.at("Path").get<std::string>()));
				if (assembly.contains("PdbPath"))
				{
					paths.insert(ToUrsaString(assembly.at("PdbPath").get<std::string>()));
				}
			}
		}
		for (const auto& path : paths)
		{
			const auto input = Take(CaptureRuntimeContentInput(files, Path(path)));
			closure.push_back(input);
			fingerprint["closure"].push_back(
			    {{"path", ToStdString(path)}, {"fingerprint", ToStdString(input.fingerprint)}});
		}
		const auto text = fingerprint.dump();
		compiledFingerprint = BuildResourceFingerprint(Vector<UInt8>(text.data(), text.data() + text.size()));
		host = Take(Scripting::DotNetHost::Create());
		Scripting::ManagedHostFiles framework;
		framework.runtimeConfig = Path("importframework:Ursa.ManagedHost.runtimeconfig.json");
		framework.entryAssembly = Path("importframework:Ursa.ManagedHost.dll");
		for (const String file : {"Ursa.ManagedHost.deps.json", "Ursa.Runtime.dll", "Ursa.Rendering.dll",
		                          "Ursa.Bindings.dll", "Ursa.Editor.dll"})
		{
			framework.dependencies.push_back(Path("importframework:" + file));
		}
		Done(host->Connect(files, framework, [](UInt32, const String& message) { URSA_LOG_INFO("Import", message); }));
		Done(host->LoadEditorApi(Path("importframework:Ursa.Editor.dll")));
		session = Take(host->CreateImportSession());
		Done(host->LoadScriptContent(session, Path("lib:ScriptBuild/Manifests/Game.json")));
		Done(host->LoadScriptContent(session, Path("lib:ScriptBuild/Manifests/Editor.json")));
		rules = std::make_shared<const AssetImportRules>(
		    Take(DiscoverManagedImportRules(*host, session, compiledFingerprint, generation)));
#else
		throw std::runtime_error("Managed import environments require .NET support");
#endif
	}

	void Shutdown()
	{
#if defined(URSA_ENABLE_DOTNET)
		if (host && session)
		{
			host->CloseScriptSession(session);
			session = 0;
		}
		host.reset();
#endif
	}
};

ManagedImportEnvironment::ManagedImportEnvironment(std::unique_ptr<Impl> implementation)
    : implementation(std::move(implementation))
{
}

Result<std::shared_ptr<ManagedImportEnvironment>>
ManagedImportEnvironment::Open(const VirtualFileSystem& source, String configuration, bool buildScripts)
{
	try
	{
		auto state = std::make_unique<Impl>();
		state->configuration = std::move(configuration);
		state->buildScripts = buildScripts;
		std::filesystem::path userRoot;
		std::filesystem::path libraryRoot;
		for (const auto& provider : source.Providers())
		{
			if (provider.scheme == userDataScheme)
			{
				if (provider.access == MountAccess::ReadWrite && !provider.provider->DirectoryRoot().empty())
				{
					userRoot = provider.provider->DirectoryRoot();
				}
				continue;
			}
			if (provider.scheme == "importframework")
			{
				continue;
			}
			Done(state->files.MountProvider(provider.scheme, provider.provider, provider.access));
			if (provider.scheme == editorLibraryScheme && provider.access == MountAccess::ReadWrite &&
			    !provider.provider->DirectoryRoot().empty())
			{
				libraryRoot = provider.provider->DirectoryRoot();
			}
		}
		if (libraryRoot.empty())
		{
			return Result<std::shared_ptr<ManagedImportEnvironment>>::Failure(
			    "Managed import requires a directory-backed writable project Library");
		}
		if (userRoot.empty())
		{
			userRoot = libraryRoot / "ManagedImportHost";
		}
		Done(state->files.MountProvider(userDataScheme, Take(DirectoryFileProvider::Open(userRoot, true)),
		                                MountAccess::ReadWrite));
		auto ready = std::make_shared<std::promise<Result<void>>>();
		auto initialized = ready->get_future();
		auto* owner = state.get();
		state->worker = std::thread(
		    [owner, ready]
		    {
			    bool started = false;
			    try
			    {
				    owner->Initialize();
				    if (!Take(owner->Current()))
				    {
					    throw std::runtime_error("Script inputs changed while preparing import rules");
				    }
				    started = true;
				    ready->set_value({});
			    }
			    catch (const std::exception& error)
			    {
				    ready->set_value(Result<void>::Failure(ToUrsaString(error.what())));
			    }
			    while (started)
			    {
				    std::function<void()> job;
				    {
					    std::unique_lock<std::mutex> lock(owner->mutex);
					    owner->changed.wait(lock, [&] { return owner->stopping || !owner->jobs.empty(); });
					    if (owner->jobs.empty() && owner->stopping)
					    {
						    break;
					    }
					    job = std::move(owner->jobs.front());
					    owner->jobs.pop_front();
				    }
				    job();
			    }
			    owner->Shutdown();
		    });
		const auto result = initialized.get();
		if (!result)
		{
			state->worker.join();
			return Result<std::shared_ptr<ManagedImportEnvironment>>::Failure(result.error);
		}
		return Result<std::shared_ptr<ManagedImportEnvironment>>::Success(
		    std::shared_ptr<ManagedImportEnvironment>(new ManagedImportEnvironment(std::move(state))));
	}
	catch (const std::exception& error)
	{
		return Result<std::shared_ptr<ManagedImportEnvironment>>::Failure(ToUrsaString(error.what()));
	}
}

ManagedImportEnvironment::~ManagedImportEnvironment()
{
	Cancel();
	{
		std::lock_guard<std::mutex> lock(implementation->mutex);
		implementation->stopping = true;
	}
	implementation->changed.notify_one();
	if (implementation->worker.joinable())
	{
		implementation->worker.join();
	}
}

Result<bool> ManagedImportEnvironment::IsCurrent()
{
	return implementation->Call<bool>([this] { return implementation->Current(); });
}

void ManagedImportEnvironment::Cancel()
{
	implementation->activeGeneration.store(0);
}

UInt64 ManagedImportEnvironment::Generation() const
{
	return implementation->activeGeneration.load();
}

const String& ManagedImportEnvironment::Fingerprint() const
{
	return implementation->compiledFingerprint;
}

const Vector<ScriptTypeDescriptor>& ManagedImportEnvironment::ScriptTypes() const
{
	return implementation->types;
}

const std::shared_ptr<const AssetImportRules>& ManagedImportEnvironment::Rules() const
{
	return implementation->rules;
}

Result<void> ManagedImportEnvironment::Install(ProjectAssetImporter& importer)
{
#if defined(URSA_ENABLE_DOTNET)
	if (!Generation())
	{
		return Result<void>::Failure("Managed import rules are obsolete");
	}
	if (importer.ManagedRulesToken() == implementation->generation)
	{
		return {};
	}
	ProjectAssetImporter prepared;
	const auto scriptable = prepared.RegisterScriptableAssets(implementation->types);
	if (!scriptable)
	{
		return scriptable;
	}
	if (implementation->host)
	{
		auto owner = shared_from_this();
		auto installed = RegisterScriptedAssetImporters(
		    prepared, implementation->rules, *implementation->host, implementation->session,
		    [owner](const ProjectAssetImporter::ImportFunction& callback, AssetDatabase& database,
		            const VirtualPath& source, bool force)
		    {
			    return owner->implementation->Call<AssetImportResult>(
			        [owner, callback, &database, source, force]
			        {
				        if (!Take(owner->implementation->Current()))
				        {
					        return Result<AssetImportResult>::Failure(
					            "Managed import rules are obsolete; rebuild the current Game/Editor scripts");
				        }
				        auto result = callback(database, source, force);
				        if (!Take(owner->implementation->Current()))
				        {
					        return Result<AssetImportResult>::Failure(
					            "Script inputs changed during import; candidate discarded");
				        }
				        return result;
			        });
		    },
		    [owner] { return owner->Generation(); });
		if (!installed)
		{
			return installed;
		}
	}
	return importer.ReplaceManagedRules(prepared, implementation->generation);
#else
	(void)importer;
	return Result<void>::Failure("Managed import environments require .NET support");
#endif
}
} // namespace Ursa::Editor
