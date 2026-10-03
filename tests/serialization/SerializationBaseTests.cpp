#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <serialization/AtomicFile.hpp>
#include <serialization/ResourceFile.hpp>
#include <serialization/ResourceIndex.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Ursa::Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(Ursa::ToStdString(value.error));
	}
	return std::move(value.value);
}

void TestIds()
{
	Ursa::Set<Ursa::String> values;
	for (int index = 0; index < 1000; ++index)
	{
		const auto id = Ursa::ResourceId::Generate();
		Expect(static_cast<bool>(id), "Generated a zero resource ID");
		Expect(values.insert(id.ToString()).second, "Generated duplicate resource IDs");
		const auto parsed = Ursa::ResourceId::Parse(id.ToString());
		Expect(parsed && parsed.value == id, "Resource ID text roundtrip failed");
	}
	Expect(!Ursa::ResourceId::Parse("not-an-id"), "Accepted malformed resource ID");
	Expect(!Ursa::ResourceId::Parse("00000000-0000-0000-0000-000000000000"), "Accepted zero resource ID");
}

void TestHeaders(const std::filesystem::path& directory)
{
	const auto id = Ursa::ResourceId::Generate();
	const auto valid = directory / "valid.ursamat";
	{
		std::ofstream output(valid);
		output << "{\"format\":\"UrsaMaterial\",\"version\":1,\"id\":\"" << Ursa::ToStdString(id.ToString()) << "\"}";
	}
	const auto header = Ursa::ReadResourceHeader(valid);
	Expect(header && header.value.kind == Ursa::ResourceKind::material && header.value.id == id,
	       "Valid material header was rejected");

	const auto unknown = directory / "future.ursascene";
	{
		std::ofstream output(unknown);
		output << "{\"format\":\"UrsaScene\",\"version\":999,\"id\":\"" << Ursa::ToStdString(id.ToString()) << "\"}";
	}
	const auto future = Ursa::ReadResourceHeader(unknown);
	Expect(!future && future.error.find("999") != Ursa::String::npos, "Unknown version lacked a diagnostic");

	const auto corrupt = directory / "corrupt.ursatex";
	{
		std::ofstream output(corrupt, std::ios::binary);
		output << "bad";
	}
	Expect(!Ursa::ReadResourceHeader(corrupt), "Truncated texture header was accepted");

	const auto oversized = directory / "oversized.ursaprefab";
	{
		std::ofstream output(oversized, std::ios::binary);
		output.seekp(static_cast<std::streamoff>(Ursa::maxJsonResourceBytes));
		output.put('x');
	}
	const auto large = Ursa::ReadResourceHeader(oversized);
	Expect(!large && large.error.find("size limit") != Ursa::String::npos, "Oversized JSON resource was accepted");

	std::filesystem::remove(valid);
	std::filesystem::remove(unknown);
	std::filesystem::remove(corrupt);
	std::filesystem::remove(oversized);
}

void TestIndex(const std::filesystem::path& directory)
{
	const auto firstRoot = directory / "first";
	const auto secondRoot = directory / "second";
	std::filesystem::create_directory(firstRoot);
	std::filesystem::create_directory(secondRoot);
	const auto id = Ursa::ResourceId::Generate();
	const auto first = firstRoot / "original.ursamat";
	const auto moved = firstRoot / "renamed.ursamat";
	const auto duplicate = secondRoot / "copy.ursamat";
	const std::string document =
	    "{\"format\":\"UrsaMaterial\",\"version\":1,\"id\":\"" + Ursa::ToStdString(id.ToString()) + "\"}";
	{
		std::ofstream output(first);
		output << document;
	}
	Ursa::ResourceIndex index;
	const auto firstMount = Ursa::ResourceMount{Ursa::projectScheme, firstRoot, Ursa::MountAccess::readOnly};
	const auto secondMount = Ursa::ResourceMount{Ursa::engineScheme, secondRoot, Ursa::MountAccess::readOnly};
	const auto firstPath = Take(Ursa::VirtualPath::Parse("prj:original.ursamat"));
	const auto movedPath = Take(Ursa::VirtualPath::Parse("prj:renamed.ursamat"));
	Expect(index.Mount(firstMount) && index.Mount(secondMount), "Could not mount resource roots");
	Expect(static_cast<bool>(index.Rebuild()), "Could not scan resource roots");
	Expect(index.Resolve(id) && index.Resolve(id).value.path == firstPath, "Could not resolve indexed resource");
	std::filesystem::rename(first, moved);
	Expect(index.Rebuild() && index.Resolve(id).value.path == movedPath, "Moved resource lost its identity");
	{
		std::ofstream output(duplicate);
		output << document;
	}
	const auto collision = index.Rebuild();
	Expect(!collision && collision.error.find("prj:renamed.ursamat") != Ursa::String::npos &&
	           collision.error.find("ursa:copy.ursamat") != Ursa::String::npos,
	       "Duplicate resource ID did not identify both files");
	Expect(index.Resolve(id) && index.Resolve(id).value.path == movedPath,
	       "Failed index rebuild destroyed valid index");
	const auto missing = index.Resolve(Ursa::ResourceId::Generate(), "sample scene");
	Expect(!missing && missing.error.find("sample scene") != Ursa::String::npos,
	       "Missing resource diagnostic omitted reference source");
	std::filesystem::remove(moved);
	std::filesystem::remove(duplicate);
	std::filesystem::remove(firstRoot);
	std::filesystem::remove(secondRoot);
}

void TestAtomicWrite(const std::filesystem::path& directory)
{
	const auto target = directory / "atomic.ursamat";
	{
		std::ofstream output(target);
		output << "old content";
	}
	const auto content = [&]
	{
		std::ifstream input(target);
		return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	};
	const auto failedWrite =
	    Ursa::WriteResourceAtomically(target,
	                                  [](std::ostream& stream)
	                                  {
		                                  stream << "partial";
		                                  return Ursa::Result<void>::Failure("simulated write failure");
	                                  });
	Expect(!failedWrite && content() == "old content", "Write failure damaged original resource");

	const auto failedReplace = Ursa::WriteResourceAtomically(
	    target,
	    [](std::ostream& stream)
	    {
		    stream << "new content";
		    return Ursa::Result<void>{};
	    },
	    [](const std::filesystem::path&, const std::filesystem::path&)
	    { return Ursa::Result<void>::Failure("simulated replace failure"); });
	Expect(!failedReplace && content() == "old content", "Replace failure damaged original resource");

	const auto successful = Ursa::WriteResourceAtomically(target,
	                                                      [](std::ostream& stream)
	                                                      {
		                                                      stream << "new content";
		                                                      return Ursa::Result<void>{};
	                                                      });
	Expect(static_cast<bool>(successful) && content() == "new content", "Atomic replacement failed");
	std::filesystem::remove(target);
}
} // namespace

int main()
{
	try
	{
		TestIds();
		const auto directory =
		    std::filesystem::temp_directory_path() /
		    ("UrsaSerializationBaseTests-" + Ursa::ToStdString(Ursa::ResourceId::Generate().ToString()));
		std::filesystem::create_directory(directory);
		TestHeaders(directory);
		TestIndex(directory);
		TestAtomicWrite(directory);
		std::filesystem::remove(directory);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
