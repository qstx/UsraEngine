#include <Shader/ShaderSourceLibrary.hpp>
#include <Vfs/MemoryFileProvider.hpp>

#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void WriteText(VirtualFileSystem& filesystem, const char* path, const char* text)
{
	auto virtualPath = VirtualPath::Parse(path);
	Expect(static_cast<bool>(virtualPath), "Invalid test virtual path");
	Vector<UInt8> bytes;
	for (const char* character = text; *character != '\0'; ++character)
	{
		bytes.push_back(static_cast<UInt8>(*character));
	}
	Expect(static_cast<bool>(filesystem.WriteAtomic(virtualPath.value, bytes)), "Cannot write test VFS resource");
}

VirtualFileSystem CreateFilesystem()
{
	VirtualFileSystem filesystem;
	auto provider = std::make_shared<MemoryFileProvider>();
	Expect(static_cast<bool>(filesystem.MountProvider("prj", provider, MountAccess::ReadWrite)),
	       "Cannot mount the in-memory project source");
	return filesystem;
}

const char* commonShader = R"shader(
Shader "Ursa/Common"
{
	ID "c6f1e600-b5c6-4c56-a5e1-5bba91570121"
	Version 1
	SubShader
	{
		Pass
		{
			Name "Depth"
		}
	}
}
)shader";

const char* fallbackShader = R"shader(
Shader "Ursa/Fallback"
{
	ID "c6f1e600-b5c6-4c56-a5e1-5bba91570122"
	Version 1
	SubShader
	{
		Pass { Name "Error" }
	}
}
)shader";

const char* rootShader = R"shader(
Shader "Ursa/Root"
{
	ID "c6f1e600-b5c6-4c56-a5e1-5bba91570123"
	Version 1
	HLSLINCLUDE
#include "Includes/Common.hlsli"
ENDHLSL
	SubShader
	{
		UsePass "Ursa/Common/Depth"
	}
	Fallback "Ursa/Fallback"
}
)shader";
} // namespace

int main()
{
	try
	{
		auto filesystem = CreateFilesystem();
		WriteText(filesystem, "prj:Shaders/Root.ursashader", rootShader);
		WriteText(filesystem, "prj:Shaders/Common.ursashader", commonShader);
		WriteText(filesystem, "prj:Shaders/Fallback.ursashader", fallbackShader);
		WriteText(filesystem, "prj:Shaders/Includes/Common.hlsli", "float3 CommonFunction() { return 1; }\n");
		auto library = Ursa::Shader::ShaderSourceLibrary::Load(filesystem, "prj");
		Expect(static_cast<bool>(library), "Valid shader source library did not load");
		auto dependencies = library.value.ResolveDependencies(filesystem, "Ursa/Root");
		if (!dependencies)
		{
			std::cerr << dependencies.error.c_str() << '\n';
		}
		Expect(static_cast<bool>(dependencies), "Valid shader dependencies did not resolve");
		Expect(dependencies.value.includes.size() == 1 &&
		           dependencies.value.includes[0].ToString() == "prj:Shaders/Includes/Common.hlsli",
		       "Relative HLSL include did not resolve through VFS");
		Expect(dependencies.value.shaderReferences.size() == 2, "UsePass and fallback dependencies were not recorded");

		WriteText(filesystem, "prj:Shaders/MissingInclude.ursashader",
		          "Shader \"Ursa/MissingInclude\" { ID \"c6f1e600-b5c6-4c56-a5e1-5bba91570124\" Version 1 "
		          "HLSLINCLUDE\n#include \"Missing.hlsli\"\nENDHLSL "
		          "SubShader { Pass {} } }");
		library = Ursa::Shader::ShaderSourceLibrary::Load(filesystem, "prj");
		Expect(static_cast<bool>(library), "Missing include fixture should parse before dependency resolution");
		auto missing = library.value.ResolveDependencies(filesystem, "Ursa/MissingInclude");
		Expect(!missing && missing.error.find("prj:Shaders/Missing.hlsli") != String::npos,
		       "Missing HLSL include did not report its VFS path");

		WriteText(filesystem, "prj:Shaders/Cycle.ursashader",
		          "Shader \"Ursa/Cycle\" { ID \"c6f1e600-b5c6-4c56-a5e1-5bba91570125\" Version 1 HLSLINCLUDE\n#include "
		          "\"Cycle.hlsli\"\nENDHLSL "
		          "SubShader { Pass {} } }");
		WriteText(filesystem, "prj:Shaders/Cycle.hlsli", "#include \"Cycle.ursashader\"\n");
		library = Ursa::Shader::ShaderSourceLibrary::Load(filesystem, "prj");
		Expect(static_cast<bool>(library), "Include cycle fixture should parse before dependency resolution");
		auto includeCycle = library.value.ResolveDependencies(filesystem, "Ursa/Cycle");
		Expect(!includeCycle && includeCycle.error.find("cycle") != String::npos,
		       "HLSL include cycle was not detected");

		WriteText(filesystem, "prj:Shaders/ReferenceA.ursashader",
		          "Shader \"Ursa/ReferenceA\" { ID \"c6f1e600-b5c6-4c56-a5e1-5bba91570126\" Version 1 SubShader { Pass "
		          "{} } Fallback \"Ursa/ReferenceB\" }");
		WriteText(filesystem, "prj:Shaders/ReferenceB.ursashader",
		          "Shader \"Ursa/ReferenceB\" { ID \"c6f1e600-b5c6-4c56-a5e1-5bba91570127\" Version 1 SubShader { Pass "
		          "{} } Fallback \"Ursa/ReferenceA\" }");
		library = Ursa::Shader::ShaderSourceLibrary::Load(filesystem, "prj");
		Expect(static_cast<bool>(library), "Shader reference cycle fixture should parse before dependency resolution");
		auto referenceCycle = library.value.ResolveDependencies(filesystem, "Ursa/ReferenceA");
		Expect(!referenceCycle && referenceCycle.error.find("cycle") != String::npos,
		       "Fallback reference cycle was not detected");

		WriteText(filesystem, "prj:Shaders/Duplicate.ursashader",
		          "Shader \"Ursa/Common\" { ID \"c6f1e600-b5c6-4c56-a5e1-5bba91570128\" Version 1 SubShader { Pass { "
		          "Name \"Another\" } } }");
		auto duplicates = Ursa::Shader::ShaderSourceLibrary::Load(filesystem, "prj");
		Expect(!duplicates && duplicates.error.find("Duplicate Shader name") != String::npos,
		       "Duplicate Shader names were not rejected");
		const auto duplicateNamePath = VirtualPath::Parse("prj:Shaders/Duplicate.ursashader").value;
		Expect(static_cast<bool>(filesystem.Remove(duplicateNamePath)), "Cannot remove duplicate-name test fixture");

		WriteText(filesystem, "prj:Shaders/DuplicateId.ursashader",
		          "Shader \"Ursa/DuplicateId\" { ID \"c6f1e600-b5c6-4c56-a5e1-5bba91570121\" Version 1 "
		          "SubShader { Pass {} } }");
		auto duplicateIds = Ursa::Shader::ShaderSourceLibrary::Load(filesystem, "prj");
		Expect(!duplicateIds && duplicateIds.error.find("Duplicate Shader resource ID") != String::npos,
		       "Duplicate Shader resource IDs were not rejected");

		std::cout << "Shader source library tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
