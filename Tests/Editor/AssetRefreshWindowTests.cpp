#include <Application/EditorApplication.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Core/StringInterop.hpp>
#include <chrono>
#include <iostream>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

class RefreshEditor final : public EditorApplication
{
public:
	bool Checked() const
	{
		return stage == 4;
	}

protected:
	Result<void> OnStart() override
	{
		started = std::chrono::steady_clock::now();
		return {};
	}

	Result<void> OnGui() override
	{
		auto drawn = EditorApplication::OnGui();
		if (!drawn)
		{
			return drawn;
		}
		if (std::chrono::steady_clock::now() - started > std::chrono::seconds(180))
		{
			return Result<void>::Failure("Editor asset refresh test timed out at stage " +
			                             ToUrsaString(std::to_string(stage)) + ": " + AssetStatus());
		}
		if (AssetWorkPending())
		{
			return {};
		}
		auto path = VirtualPath::Parse("prj:Broken.ursashader").value;
		auto& files = Workspace()->Index().Filesystem();
		if (stage == 0)
		{
			auto written = files.WriteAtomic(VirtualPath::Parse("prj:Auto.txt").value, {'a'});
			if (!written)
			{
				return written;
			}
			stage = 1;
		}
		else if (stage == 1)
		{
			if (AssetStatus() != "Assets are up to date")
			{
				return {};
			}
			auto status = Workspace()->Database()->CurrentImport(VirtualPath::Parse("prj:Auto.txt").value);
			if (!status || !status.value.upToDate)
			{
				return Result<void>::Failure("Automatic refresh did not import an external source");
			}
			auto written = files.WriteAtomic(path, {'i', 'n', 'v', 'a', 'l', 'i', 'd'});
			if (!written)
			{
				return written;
			}
			RefreshAssets();
			stage = 2;
		}
		else if (stage == 2)
		{
			if (AssetStatus() == "Assets are up to date" || AssetStatus().empty())
			{
				return Result<void>::Failure("Broken Shader import did not appear in Editor diagnostics");
			}
			// Repair the failed source while the same GUI and GPU remain alive.
			auto removed = files.Remove(path);
			if (removed)
			{
				removed = files.Remove(VirtualPath::Parse("prj:Broken.ursashader.meta").value);
			}
			if (!removed)
			{
				return removed;
			}
			RefreshAssets();
			stage = 3;
		}
		else if (stage == 3)
		{
			if (AssetStatus() != "Assets are up to date")
			{
				return Result<void>::Failure("Editor did not recover after repairing source: " + AssetStatus());
			}
			stage = 4;
			window->RequestClose();
		}
		return {};
	}

private:
	int stage = 0;
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};
} // namespace

int main()
{
	const auto root =
	    std::filesystem::temp_directory_path() / ("UrsaAssetRefresh-" + ToStdString(ResourceId::Generate().ToString()));
	std::filesystem::create_directories(root / "Assets");
	RefreshEditor editor;
	auto opened = editor.OpenProject(root);
	auto ran = opened ? editor.Run(1000, 720, "Asset Import Refresh Test", false, 30000) : opened;
	std::error_code error;
	std::filesystem::remove_all(root, error);
	if (!ran || !editor.Checked())
	{
		std::cerr << (ran ? "Editor asset refresh was not checked" : ran.error.c_str()) << '\n';
		return 1;
	}
	std::cout << "Editor background failure/retry, diagnostic panels and GPU shader replacement passed\n";
	return 0;
}
