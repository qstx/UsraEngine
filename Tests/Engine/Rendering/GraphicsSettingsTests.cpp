#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>

using namespace Ursa;
using namespace Ursa::Rendering;

namespace
{
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
} // namespace

int main()
{
	try
	{
		VirtualFileSystem files;
		auto provider = std::make_shared<MemoryFileProvider>();
		Done(files.MountProvider(projectSettingsScheme, provider, MountAccess::ReadWrite));
		Expect(!Take(ReadGraphicsSettings(files)).renderPipeline.guid, "An unset project did not select the default");
		GraphicsSettings selected{{ResourceId::Generate(), 7}};
		const auto path = Take(VirtualPath::Parse(graphicsSettingsSource));
		Expect(!SaveGraphicsSettings(files, selected), "A custom selection bypassed asset type validation");
		UInt32 validations = 0;
		Done(SaveGraphicsSettings(files, selected,
		                          [&](AssetReference reference)
		                          {
			                          ++validations;
			                          return reference.guid == selected.renderPipeline.guid &&
			                                         reference.localFileId == 7
			                                     ? Result<void>{}
			                                     : Result<void>::Failure("Unexpected reference");
		                          }));
		const auto original = Take(files.Read(path));
		Expect(validations == 1 && Take(ReadGraphicsSettings(files)).renderPipeline.localFileId == 7,
		       "The saved selection was not validated and restored");
		Expect(!SaveGraphicsSettings(files, {{ResourceId::Generate(), 1}},
		                             [](AssetReference) { return Result<void>::Failure("Not a pipeline asset"); }) &&
		           Take(files.Read(path)) == original,
		       "A failed candidate overwrote the source configuration");
		VirtualFileSystem readOnly;
		Done(readOnly.MountProvider(projectSettingsScheme, provider, MountAccess::ReadOnly));
		Expect(!SaveGraphicsSettings(readOnly, {}) && Take(files.Read(path)) == original,
		       "A failed save replaced a read-only configuration");
		for (const auto* malformed :
		     {"{", "{}", "{\"format\":\"UrsaGraphicsSettings\",\"version\":2,\"renderPipeline\":null}",
		      "{\"format\":\"UrsaGraphicsSettings\",\"version\":1,\"renderPipeline\":\"invalid\"}"})
		{
			const String text = malformed;
			Done(files.WriteAtomic(path, Vector<UInt8>(text.data(), text.data() + text.size())));
			Expect(!ReadGraphicsSettings(files), "Malformed or incompatible Graphics settings were accepted");
		}
		Done(SaveGraphicsSettings(files, {}));
		Expect(!Take(ReadGraphicsSettings(files)).renderPipeline.guid, "Clearing Graphics did not restore the default");
		std::cout << "Graphics settings validation and VFS tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
