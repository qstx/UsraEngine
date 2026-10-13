#pragma once

#include <Core/Containers.hpp>
#include <Serialization/ResourceId.hpp>
#include <Vfs/VirtualFileSystem.hpp>

namespace Ursa
{
struct ScriptFieldTypeDescriptor
{
	String name;
	String kind;
	String elementKind;
	String typeName;
	String defaultValueJson;
	String elementTypeName;
	Vector<String> enumValues;
	Vector<String> enumNumericValuesJson;
	String enumUnderlyingType;
	Vector<String> formerNames;
};

enum class ScriptAssemblyGroup
{
	Game,
	Editor
};

enum class ScriptTypeKind
{
	Behaviour,
	ScriptableObject
};

struct ScriptTypeDescriptor
{
	ResourceId sourceGuid;
	String scriptType;
	UInt32 version = 1;
	UInt64 localFileId = 0;
	String clrTypeName;
	VirtualPath sourcePath;
	ScriptAssemblyGroup group = ScriptAssemblyGroup::Game;
	Vector<ScriptFieldTypeDescriptor> fields;
	ScriptTypeKind kind = ScriptTypeKind::Behaviour;
};
} // namespace Ursa
