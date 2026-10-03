#include <core/Containers.hpp>
#include <core/StringInterop.hpp>
#include <vfs/AssetRoots.hpp>
#include <vfs/VirtualFileSystem.hpp>

#include <fstream>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

ursa::Vector<uint8_t> bytesOf(const std::string &text) {
    ursa::Vector<uint8_t> bytes;
    bytes.reserve(text.size());
    for (char character : text)
        bytes.push_back(static_cast<uint8_t>(character));
    return bytes;
}

std::string textOf(const ursa::Vector<uint8_t> &bytes) {
    return std::string(bytes.begin(), bytes.end());
}

void writeFile(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    expect(static_cast<bool>(output), "Cannot write a test fixture");
}

// Directory symlinks need elevation on Windows, but junctions do not; both are
// reparse points that resolve through std::filesystem::canonical.
bool createEscapeLink(const std::filesystem::path &link, const std::filesystem::path &target) {
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
ursa::Result<void> openResourcesExample(const std::filesystem::path &engineRoot,
                                        const std::filesystem::path &engineMirror,
                                        const std::filesystem::path &projectAssets) {
    ursa::VirtualFileSystem vfs;
    if (!vfs.mount("ursa", engineRoot, ursa::MountAccess::ReadOnly) ||
        !vfs.mount("ursa", engineMirror, ursa::MountAccess::ReadOnly) ||
        !vfs.mount("prj", projectAssets, ursa::MountAccess::ReadWrite))
        return ursa::Result<void>::failure("Cannot mount resource roots");
    auto shader = ursa::VirtualPath::parse("ursa:shaders/renderer3d/cube.vert.spv");
    if (!shader)
        return ursa::Result<void>::failure(shader.error);
    auto bytes = vfs.read(shader.value);
    if (!bytes)
        return ursa::Result<void>::failure(bytes.error);
    return {};
}

void testParsing() {
    const auto valid = ursa::VirtualPath::parse("ursa:shaders/renderer3d/cube.vert");
    expect(static_cast<bool>(valid) && valid.value.scheme == "ursa" &&
               valid.value.relative == "shaders/renderer3d/cube.vert" &&
               valid.value.toString() == "ursa:shaders/renderer3d/cube.vert",
           "Valid virtual path did not round trip");

    const auto normalized = ursa::VirtualPath::parse("PRJ:snake\\.\\board.ursaprefab");
    expect(static_cast<bool>(normalized) && normalized.value.scheme == "prj" &&
               normalized.value.relative == "snake/board.ursaprefab",
           "Virtual path normalization failed");

    expect(!ursa::VirtualPath::parse("shaders/cube.vert"), "Accepted a path without a scheme");
    expect(!ursa::VirtualPath::parse(":cube.vert"), "Accepted an empty scheme");
    expect(!ursa::VirtualPath::parse("C:/engine/assets/cube.vert"), "Accepted a host drive letter");
    expect(!ursa::VirtualPath::parse("ursa:/shaders/cube.vert"), "Accepted an absolute relative path");
    expect(!ursa::VirtualPath::parse("ursa:../outside.ursatex"), "Accepted a parent-directory escape");
    expect(!ursa::VirtualPath::parse("ursa:shaders/../cube.vert"), "Accepted a parent-directory segment");
    expect(!ursa::VirtualPath::parse("ursa:"), "Accepted a missing relative path");
}

void testReadWriteAndPermissions(const std::filesystem::path &root) {
    const auto engineRoot = root / "engine";
    const auto projectRoot = root / "project";
    std::filesystem::create_directories(engineRoot);
    std::filesystem::create_directories(projectRoot);
    writeFile(engineRoot / "shaders" / "cube.vert", "engine-shader");

    ursa::VirtualFileSystem vfs;
    expect(static_cast<bool>(vfs.mount("ursa", engineRoot, ursa::MountAccess::ReadOnly)),
           "Cannot mount the engine root");
    expect(static_cast<bool>(vfs.mount("prj", projectRoot, ursa::MountAccess::ReadWrite)),
           "Cannot mount the project root");
    expect(!vfs.mount("ursa", root / "missing", ursa::MountAccess::ReadOnly),
           "Mounted a missing directory");

    const auto shader = ursa::VirtualPath::parse("ursa:shaders/cube.vert").value;
    expect(textOf(vfs.read(shader).value) == "engine-shader", "Read returned the wrong bytes");
    expect(vfs.exists(shader).value, "Existing file was reported missing");
    expect(vfs.resolveToDisk(shader).value == engineRoot / "shaders" / "cube.vert",
           "resolveToDisk returned the wrong host path");

    const auto saved = ursa::VirtualPath::parse("prj:snake/new.ursatex").value;
    expect(static_cast<bool>(vfs.write(saved, bytesOf("saved"))), "Cannot write to a writable mount");
    expect(textOf(vfs.read(saved).value) == "saved", "Written bytes did not round trip");
    expect(!vfs.write(shader, bytesOf("changed")), "Wrote into a read-only mount");
    expect(textOf(vfs.read(shader).value) == "engine-shader", "Read-only write changed the target file");

    expect(!vfs.read(ursa::VirtualPath::parse("mod:anything.ursatex").value),
           "Read from an unmounted scheme succeeded");
    expect(!vfs.write(ursa::VirtualPath::parse("mod:anything.ursatex").value, bytesOf("x")),
           "Write into an unmounted scheme succeeded");
    expect(!vfs.read(ursa::VirtualPath::parse("prj:snake/missing.ursatex").value),
           "Missing resource was accepted");
}

void testOverlayAndShadowing(const std::filesystem::path &root) {
    const auto sourceRoot = root / "overlay-source";
    const auto generatedRoot = root / "overlay-generated";
    writeFile(sourceRoot / "shaders" / "renderer3d" / "cube.vert", "source-shader");
    writeFile(generatedRoot / "shaders" / "renderer3d" / "cube.vert.spv", "generated-shader");
    writeFile(generatedRoot / "shaders" / "renderer3d" / "cube.vert", "shadowed-shader");
    std::filesystem::create_directories(root / "overlay-project");
    expect(static_cast<bool>(openResourcesExample(sourceRoot, generatedRoot, root / "overlay-project")),
           "The documented VirtualFileSystem example failed");

    ursa::VirtualFileSystem vfs;
    expect(static_cast<bool>(vfs.mount("ursa", sourceRoot, ursa::MountAccess::ReadOnly)) &&
               static_cast<bool>(vfs.mount("ursa", generatedRoot, ursa::MountAccess::ReadOnly)),
           "Cannot mount the overlay roots");

    const auto roots = vfs.roots("ursa");
    expect(static_cast<bool>(roots) && roots.value.size() == 2, "Overlay did not keep both roots");

    const auto source = ursa::VirtualPath::parse("ursa:shaders/renderer3d/cube.vert").value;
    const auto generated = ursa::VirtualPath::parse("ursa:shaders/renderer3d/cube.vert.spv").value;
    expect(textOf(vfs.read(source).value) == "source-shader", "Source asset did not resolve to the first root");
    expect(textOf(vfs.read(generated).value) == "generated-shader",
           "Generated asset did not resolve to the second root");

    auto listed = vfs.list("ursa");
    expect(static_cast<bool>(listed) && listed.value.size() == 2, "Overlay listing returned the wrong entry count");
    expect(listed.value[0].path.toString() == "ursa:shaders/renderer3d/cube.vert" &&
               listed.value[1].path.toString() == "ursa:shaders/renderer3d/cube.vert.spv",
           "Overlay listing returned unexpected virtual paths");
    expect(listed.value[0].shadowedHostPaths.size() == 1 &&
               listed.value[0].shadowedHostPaths.front() == generatedRoot / "shaders" / "renderer3d" / "cube.vert",
           "Shadowed host location was not reported");
    expect(listed.value[1].shadowedHostPaths.empty(), "Unshadowed entry reported a shadowed location");
    expect(!vfs.list("mod"), "Listing an unmounted scheme succeeded");
}

void testTraversalIsolation(const std::filesystem::path &root) {
    const auto projectRoot = root / "traversal";
    const auto outsideRoot = root / "outside";
    writeFile(projectRoot / "snake" / "snake.ursascene", "scene");
    writeFile(outsideRoot / "secret.ursatex", "secret");

    ursa::VirtualFileSystem vfs;
    expect(static_cast<bool>(vfs.mount("prj", projectRoot, ursa::MountAccess::ReadWrite)),
           "Cannot mount the traversal fixture");

    expect(createEscapeLink(projectRoot / "escape", outsideRoot), "Cannot create the escape-link fixture");
    auto listed = vfs.list("prj");
    expect(static_cast<bool>(listed) && listed.value.size() == 1 &&
               listed.value.front().path.toString() == "prj:snake/snake.ursascene",
           "Traversal followed a link out of its mount root");
    const auto throughLink = ursa::VirtualPath::parse("prj:escape/secret.ursatex").value;
    expect(!vfs.resolveToDisk(throughLink), "resolveToDisk followed a link out of its mount root");
    expect(!vfs.read(throughLink), "read followed a link out of its mount root");

    const auto escaped = ursa::VirtualPath::parse("prj:snake/../../secret.ursatex");
    expect(!escaped, "Traversal escape was accepted at parse time");
}

void testEngineAssetRootResolution(const std::filesystem::path &root) {
    const auto executable = root / "bin" / "UrsaEditor.exe";
    const auto besideExecutable = root / "bin" / "assets";
    const auto mirror = root / "cache" / "assets";
    std::filesystem::create_directories(besideExecutable);
    std::filesystem::create_directories(mirror);

    ursa::EngineAssetRootCandidates candidates;
    candidates.executablePath = executable;
    candidates.buildMirror = mirror;

    const auto adjacent = ursa::resolveEngineAssetRoot(candidates);
    expect(static_cast<bool>(adjacent) && adjacent.value == besideExecutable,
           "Executable-adjacent assets did not win over the build mirror");

    std::filesystem::remove(besideExecutable);
    const auto fallback = ursa::resolveEngineAssetRoot(candidates);
    expect(static_cast<bool>(fallback) && fallback.value == mirror,
           "Build mirror was not used when no adjacent assets exist");

    candidates.explicitRoot = root / "custom-assets";
    expect(!ursa::resolveEngineAssetRoot(candidates), "A missing explicit root was accepted");
    std::filesystem::create_directories(candidates.explicitRoot);
    const auto explicitRoot = ursa::resolveEngineAssetRoot(candidates);
    expect(static_cast<bool>(explicitRoot) && explicitRoot.value == candidates.explicitRoot,
           "Explicit engine asset root did not win");

    std::filesystem::remove_all(candidates.explicitRoot);
    std::filesystem::remove_all(mirror);
    candidates.explicitRoot.clear();
    expect(!ursa::resolveEngineAssetRoot(candidates),
           "Resolution succeeded although no candidate directory exists");
}
} // namespace

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() /
                          ("UrsaVirtualFileSystemTests-" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        testParsing();
        testReadWriteAndPermissions(root);
        testOverlayAndShadowing(root);
        testTraversalIsolation(root);
        testEngineAssetRootResolution(root);
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
