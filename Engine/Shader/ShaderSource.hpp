#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Core/Types.hpp>
#include <Serialization/ResourceId.hpp>

namespace Ursa::Shader
{
struct SourceLocation
{
	UInt32 line = 1;
	UInt32 column = 1;
};

struct TagDeclaration
{
	String name;
	String value;
};

enum class PropertyType
{
	Float,
	Range,
	Int,
	Color,
	Vector,
	Texture2D
};

struct PropertyDeclaration
{
	String identifier;
	String displayName;
	PropertyType type = PropertyType::Float;
	Float32 rangeMinimum = 0.0f;
	Float32 rangeMaximum = 1.0f;
	Float32 defaultFloat = 0.0f;
	Int32 defaultInt = 0;
	Array<Float32, 4> defaultVector{};
	String defaultTexture;
	String textureOptions;
	Vector<String> attributes;
	SourceLocation location;
};

enum class ProgramBlockKind
{
	Program,
	Include
};

enum class ProgramStage
{
	Vertex,
	Fragment,
	Compute
};

enum class ShaderKeywordDirectiveKind
{
	ShaderFeature,
	MultiCompile
};

struct ShaderKeywordDirective
{
	ShaderKeywordDirectiveKind kind = ShaderKeywordDirectiveKind::ShaderFeature;
	Vector<String> options;
	SourceLocation location;
};

struct ShaderKeywordVariant
{
	Vector<String> keywords;
	bool required = true;
};

struct ProgramEntryPoint
{
	ProgramStage stage = ProgramStage::Vertex;
	String name;
	SourceLocation location;
};

struct ProgramBlock
{
	ProgramBlockKind kind = ProgramBlockKind::Program;
	String source;
	Vector<ProgramEntryPoint> entryPoints;
	SourceLocation location;
	Vector<ShaderKeywordDirective> keywordDirectives;
};

struct RenderStateDeclaration
{
	String name;
	String value;
	SourceLocation location;
};

struct PassDeclaration
{
	String name;
	Vector<TagDeclaration> tags;
	Vector<RenderStateDeclaration> renderStates;
	Vector<ProgramBlock> programs;
	Vector<String> reusedPasses;
	SourceLocation location;
};

struct SubShaderPassOrderEntry
{
	Ursa::UInt32 localPassIndex = 0;
	String reusedPass;
	bool isReused = false;
};

struct SubShaderDeclaration
{
	Vector<TagDeclaration> tags;
	Vector<RenderStateDeclaration> renderStates;
	Vector<PassDeclaration> passes;
	Vector<String> reusedPasses;
	Vector<SubShaderPassOrderEntry> passOrder;
	Vector<ProgramBlock> includes;
	SourceLocation location;
};

struct ShaderSourceDocument
{
	String sourcePath;
	String name;
	// Resolved from the asset's .meta by ShaderSourceLibrary, never from source text.
	ResourceId id;
	UInt32 formatVersion = 1;
	String fallback;
	String customEditor;
	Vector<PropertyDeclaration> properties;
	Vector<ProgramBlock> includes;
	Vector<SubShaderDeclaration> subShaders;
};

Result<ShaderSourceDocument> ParseShaderSource(const String& sourcePath, const String& source);

Result<Vector<ShaderKeywordVariant>> ExpandShaderKeywordVariants(const Vector<ShaderKeywordDirective>& directives,
                                                                 const Vector<Vector<String>>& usedKeywordSets = {},
                                                                 UInt32 maxVariantCount = 256);
} // namespace Ursa::Shader
