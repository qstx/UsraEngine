#include <Vfs/PakBuilder.hpp>
#include <Vfs/PakFormat.hpp>
#include <Core/StringInterop.hpp>
#include <chrono>
#include <algorithm>
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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

UInt64 Number(const Vector<UInt8>& bytes, Size offset, Size count)
{
	return Take(PakFormat::Take(bytes, offset, count));
}

void RepairTableChecksum(Vector<UInt8>& bytes)
{
	const Size offset = static_cast<Size>(Number(bytes, 16, 8));
	PakFormat::Put(bytes, 32, PakChecksum(bytes.data() + offset, bytes.size() - offset), 4);
}
} // namespace

int main()
{
	const auto root = std::filesystem::temp_directory_path() /
	                  ("UrsaPakTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	try
	{
		std::filesystem::create_directories(root);
		VirtualFileSystem output;
		Done(output.Mount("out", root, MountAccess::ReadWrite));
		const auto packagePath = Take(VirtualPath::Parse("out:Content.pak"));
		Vector<UInt8> large(pakBlockBytes * 2 + 123, 7);
		Vector<PakEntryData> entries{{Take(VirtualPath::Parse("ursa:large.bin")), large},
		                             {Take(VirtualPath::Parse("prj:scene.bin")), {1, 2, 3}}};
		auto package = Take(PakBuilder::Encode(entries));
		std::reverse(entries.begin(), entries.end());
		Expect(package == Take(PakBuilder::Encode(entries)), "Pak encoding is not deterministic");
		Done(output.WriteAtomic(packagePath, package));
		auto archive = Take(PakArchive::Open(root / "Content.pak"));
		Expect(archive->VerifiedBlockCount() == 0, "Pak mounting scanned payload blocks");
		VirtualFileSystem files;
		Done(archive->Mount(files));
		const auto largePath = Take(VirtualPath::Parse("ursa:large.bin"));
		Expect(Take(files.List("ursa")).size() == 1 && Take(files.Size(largePath)) == large.size(),
		       "Pak listing or size is wrong");
		Expect(Take(files.ReadRange(largePath, pakBlockBytes - 5, 20)) == Vector<UInt8>(20, 7) &&
		           archive->VerifiedBlockCount() == 2,
		       "Range read did not validate only touched blocks");
		Take(files.ReadRange(largePath, pakBlockBytes - 5, 20));
		Expect(archive->VerifiedBlockCount() == 2, "Verified blocks were checked twice");
		Expect(Take(files.Read(largePath)) == large && archive->VerifiedBlockCount() == 3,
		       "Full Pak resource read did not match source bytes");
		Expect(!files.Write(largePath, {9}) && !files.Append(largePath, {9}), "Pak mount allowed a write");
		Expect(!files.ReadRange(largePath, large.size(), 1), "Pak range overflow was accepted");
		Expect(!files.CheckWritable(largePath), "Pak accepted a writable resource path");

		auto corruptedPayload = package;
		// Sorted prj entry is three bytes; the Engine entry follows it.
		corruptedPayload[PakFormat::headerBytes + 3 + pakBlockBytes * 2] ^= 0x80;
		Done(output.WriteAtomic(Take(VirtualPath::Parse("out:Payload.pak")), corruptedPayload));
		auto lazy = Take(PakArchive::Open(root / "Payload.pak"));
		VirtualFileSystem lazyFiles;
		Done(lazy->Mount(lazyFiles));
		Take(lazyFiles.ReadRange(largePath, 0, 1));
		Expect(lazy->VerifiedBlockCount() == 1 && !lazyFiles.ReadRange(largePath, pakBlockBytes * 2, 1),
		       "Unvisited corrupt block was scanned early or returned unchecked bytes");

		const auto reject = [&](Vector<UInt8> bytes)
		{
			Done(output.WriteAtomic(Take(VirtualPath::Parse("out:Invalid.pak")), bytes));
			Expect(!PakArchive::Open(root / "Invalid.pak"), "Invalid Pak was accepted");
		};
		auto invalid = package;
		invalid[0] = 0;
		reject(invalid);
		invalid = package;
		PakFormat::Put(invalid, 8, 99, 4);
		reject(invalid);
		invalid = package;
		PakFormat::Put(invalid, 16, ~UInt64{0}, 8);
		reject(invalid);
		invalid = package;
		invalid.back() ^= 1;
		reject(invalid);
		const Size tableOffset = static_cast<Size>(Number(package, 16, 8));
		invalid = package;
		PakFormat::Put(invalid, tableOffset + 8, ~UInt64{0}, 8);
		RepairTableChecksum(invalid);
		reject(invalid);
		invalid = package;
		const Size nameOffset = tableOffset + 32 + 4;
		const String escaped = "prj:../xx.bin";
		Expect(escaped.size() == Number(package, tableOffset, 4), "Traversal fixture length changed");
		std::copy(escaped.begin(), escaped.end(), invalid.begin() + nameOffset);
		RepairTableChecksum(invalid);
		reject(invalid);
		Expect(!PakBuilder::Encode({{largePath, {1}}, {largePath, {2}}}), "Duplicate Pak key was accepted");
		auto duplicate = Take(PakBuilder::Encode(
		    {{Take(VirtualPath::Parse("prj:a.bin")), {1}}, {Take(VirtualPath::Parse("prj:b.bin")), {2}}}));
		const Size duplicateTable = static_cast<Size>(Number(duplicate, 16, 8));
		const Size secondName = duplicateTable + 36 + 9 + 36;
		const String duplicateKey = "prj:a.bin";
		std::copy(duplicateKey.begin(), duplicateKey.end(), duplicate.begin() + secondName);
		RepairTableChecksum(duplicate);
		reject(duplicate);

		VirtualFileSystem source;
		std::filesystem::create_directories(root / "source");
		std::filesystem::create_directories(root / "shadow");
		Done(source.Mount("prj", root / "source", MountAccess::ReadWrite));
		Done(source.Write(Take(VirtualPath::Parse("prj:one.bin")), {4, 5}));
		Done(PakBuilder::Build(source, {"prj"}, output, packagePath));
		const auto previous = Take(output.Read(packagePath));
		VirtualFileSystem shadow;
		Done(shadow.Mount("prj", root / "shadow", MountAccess::ReadWrite));
		Done(shadow.Write(Take(VirtualPath::Parse("prj:one.bin")), {6}));
		Done(source.Mount("prj", root / "shadow", MountAccess::ReadOnly));
		Expect(!PakBuilder::Build(source, {"prj"}, output, packagePath) && Take(output.Read(packagePath)) == previous,
		       "Conflicting input replaced the previous Pak");
		std::filesystem::remove_all(root);
		std::cout << "Pak provider and builder tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(root);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
