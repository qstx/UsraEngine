#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <serialization/AtomicFile.hpp>
#include <serialization/ResourceFile.hpp>
#include <serialization/ResourceIndex.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <class T> T take(ursa::Result<T> value) {
    if (!value)
        throw std::runtime_error(ursa::toStdString(value.error));
    return std::move(value.value);
}

void testIds() {
    ursa::Set<ursa::String> values;
    for (int index = 0; index < 1000; ++index) {
        const auto id = ursa::ResourceId::generate();
        expect(static_cast<bool>(id), "Generated a zero resource ID");
        expect(values.insert(id.toString()).second, "Generated duplicate resource IDs");
        const auto parsed = ursa::ResourceId::parse(id.toString());
        expect(parsed && parsed.value == id, "Resource ID text roundtrip failed");
    }
    expect(!ursa::ResourceId::parse("not-an-id"), "Accepted malformed resource ID");
    expect(!ursa::ResourceId::parse("00000000-0000-0000-0000-000000000000"), "Accepted zero resource ID");
}

void testHeaders(const std::filesystem::path &directory) {
    const auto id = ursa::ResourceId::generate();
    const auto valid = directory / "valid.ursamat";
    {
        std::ofstream output(valid);
        output << "{\"format\":\"UrsaMaterial\",\"version\":1,\"id\":\"" << ursa::toStdString(id.toString()) << "\"}";
    }
    const auto header = ursa::readResourceHeader(valid);
    expect(header && header.value.kind == ursa::ResourceKind::Material && header.value.id == id,
           "Valid material header was rejected");

    const auto unknown = directory / "future.ursascene";
    {
        std::ofstream output(unknown);
        output << "{\"format\":\"UrsaScene\",\"version\":999,\"id\":\"" << ursa::toStdString(id.toString()) << "\"}";
    }
    const auto future = ursa::readResourceHeader(unknown);
    expect(!future && future.error.find("999") != ursa::String::npos, "Unknown version lacked a diagnostic");

    const auto corrupt = directory / "corrupt.ursatex";
    {
        std::ofstream output(corrupt, std::ios::binary);
        output << "bad";
    }
    expect(!ursa::readResourceHeader(corrupt), "Truncated texture header was accepted");

    const auto oversized = directory / "oversized.ursaprefab";
    {
        std::ofstream output(oversized, std::ios::binary);
        output.seekp(static_cast<std::streamoff>(ursa::maxJsonResourceBytes));
        output.put('x');
    }
    const auto large = ursa::readResourceHeader(oversized);
    expect(!large && large.error.find("size limit") != ursa::String::npos, "Oversized JSON resource was accepted");

    std::filesystem::remove(valid);
    std::filesystem::remove(unknown);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(oversized);
}

void testIndex(const std::filesystem::path &directory) {
    const auto firstRoot = directory / "first";
    const auto secondRoot = directory / "second";
    std::filesystem::create_directory(firstRoot);
    std::filesystem::create_directory(secondRoot);
    const auto id = ursa::ResourceId::generate();
    const auto first = firstRoot / "original.ursamat";
    const auto moved = firstRoot / "renamed.ursamat";
    const auto duplicate = secondRoot / "copy.ursamat";
    const std::string document =
        "{\"format\":\"UrsaMaterial\",\"version\":1,\"id\":\"" + ursa::toStdString(id.toString()) + "\"}";
    {
        std::ofstream output(first);
        output << document;
    }
    ursa::ResourceIndex index;
    const auto firstMount = ursa::ResourceMount{ursa::projectScheme, firstRoot, ursa::MountAccess::ReadOnly};
    const auto secondMount = ursa::ResourceMount{ursa::engineScheme, secondRoot, ursa::MountAccess::ReadOnly};
    const auto firstPath = take(ursa::VirtualPath::parse("prj:original.ursamat"));
    const auto movedPath = take(ursa::VirtualPath::parse("prj:renamed.ursamat"));
    expect(index.mount(firstMount) && index.mount(secondMount), "Could not mount resource roots");
    expect(static_cast<bool>(index.rebuild()), "Could not scan resource roots");
    expect(index.resolve(id) && index.resolve(id).value.path == firstPath, "Could not resolve indexed resource");
    std::filesystem::rename(first, moved);
    expect(index.rebuild() && index.resolve(id).value.path == movedPath, "Moved resource lost its identity");
    {
        std::ofstream output(duplicate);
        output << document;
    }
    const auto collision = index.rebuild();
    expect(!collision && collision.error.find("prj:renamed.ursamat") != ursa::String::npos &&
               collision.error.find("ursa:copy.ursamat") != ursa::String::npos,
           "Duplicate resource ID did not identify both files");
    expect(index.resolve(id) && index.resolve(id).value.path == movedPath,
           "Failed index rebuild destroyed valid index");
    const auto missing = index.resolve(ursa::ResourceId::generate(), "sample scene");
    expect(!missing && missing.error.find("sample scene") != ursa::String::npos,
           "Missing resource diagnostic omitted reference source");
    std::filesystem::remove(moved);
    std::filesystem::remove(duplicate);
    std::filesystem::remove(firstRoot);
    std::filesystem::remove(secondRoot);
}

void testAtomicWrite(const std::filesystem::path &directory) {
    const auto target = directory / "atomic.ursamat";
    {
        std::ofstream output(target);
        output << "old content";
    }
    const auto content = [&] {
        std::ifstream input(target);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const auto failedWrite = ursa::writeResourceAtomically(target, [](std::ostream &stream) {
        stream << "partial";
        return ursa::Result<void>::failure("simulated write failure");
    });
    expect(!failedWrite && content() == "old content", "Write failure damaged original resource");

    const auto failedReplace = ursa::writeResourceAtomically(
        target,
        [](std::ostream &stream) {
            stream << "new content";
            return ursa::Result<void>{};
        },
        [](const std::filesystem::path &, const std::filesystem::path &) {
            return ursa::Result<void>::failure("simulated replace failure");
        });
    expect(!failedReplace && content() == "old content", "Replace failure damaged original resource");

    const auto successful = ursa::writeResourceAtomically(target, [](std::ostream &stream) {
        stream << "new content";
        return ursa::Result<void>{};
    });
    expect(static_cast<bool>(successful) && content() == "new content", "Atomic replacement failed");
    std::filesystem::remove(target);
}
} // namespace

int main() {
    try {
        testIds();
        const auto directory =
            std::filesystem::temp_directory_path() /
            ("UrsaSerializationBaseTests-" + ursa::toStdString(ursa::ResourceId::generate().toString()));
        std::filesystem::create_directory(directory);
        testHeaders(directory);
        testIndex(directory);
        testAtomicWrite(directory);
        std::filesystem::remove(directory);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
