#include <Shader/ShaderSource.hpp>

#include <algorithm>
#include <Core/StringInterop.hpp>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace Ursa::Shader
{
namespace
{
enum class TokenKind
{
	Identifier,
	Number,
	String,
	Punctuation,
	Newline,
	Program,
	Include
};

struct Token
{
	TokenKind kind = TokenKind::Identifier;
	String value;
	SourceLocation location;
};

bool IsIdentifierStart(char character)
{
	return std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_';
}

bool IsNumberStart(const String& source, Ursa::Size offset)
{
	if (offset >= source.size())
	{
		return false;
	}
	const char character = source[offset];
	return std::isdigit(static_cast<unsigned char>(character)) != 0 ||
	       (character == '.' && offset + 1 < source.size() &&
	        std::isdigit(static_cast<unsigned char>(source[offset + 1])) != 0) ||
	       ((character == '-' || character == '+') && offset + 1 < source.size() &&
	        (std::isdigit(static_cast<unsigned char>(source[offset + 1])) != 0 || source[offset + 1] == '.'));
}

bool IsIdentifierPart(char character)
{
	return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

bool IsWhitespace(char character)
{
	return character == ' ' || character == '\t' || character == '\r' || character == '\f' || character == '\v';
}

String Slice(const String& source, Ursa::Size begin, Ursa::Size end)
{
	return String(source.data() + begin, end - begin);
}

bool Matches(const String& source, Ursa::Size offset, const char* text)
{
	Ursa::Size length = 0;
	while (text[length] != '\0')
	{
		++length;
	}
	return offset + length <= source.size() && source.compare(offset, length, text) == 0;
}

bool HasWordBoundary(const String& source, Ursa::Size offset, Ursa::Size length)
{
	return offset + length >= source.size() || !IsIdentifierPart(source[offset + length]);
}

class Lexer final
{
public:
	Lexer(const String& path, const String& text) : sourcePath(path), source(text)
	{
	}

	Result<Vector<Token>> Run()
	{
		while (offset < source.size())
		{
			const char character = source[offset];
			if (character == '\n')
			{
				Add(TokenKind::Newline, offset, offset + 1, location);
				Advance();
				continue;
			}
			if (IsWhitespace(character))
			{
				Advance();
				continue;
			}
			if (Matches(source, offset, "//"))
			{
				SkipLineComment();
				continue;
			}
			if (Matches(source, offset, "/*"))
			{
				auto skipped = SkipBlockComment();
				if (!skipped)
				{
					return Result<Vector<Token>>::Failure(skipped.error);
				}
				continue;
			}
			if (character == '"')
			{
				auto parsed = ReadString();
				if (!parsed)
				{
					return Result<Vector<Token>>::Failure(parsed.error);
				}
				continue;
			}
			if (IsIdentifierStart(character))
			{
				ReadIdentifier();
				continue;
			}
			if (IsNumberStart(source, offset))
			{
				ReadNumber();
				continue;
			}
			if (character == '{' || character == '}' || character == '(' || character == ')' || character == '[' ||
			    character == ']' || character == '=' || character == ',' || character == ';')
			{
				Add(TokenKind::Punctuation, offset, offset + 1, location);
				Advance();
				continue;
			}
			return Result<Vector<Token>>::Failure(Error(location, "Unexpected character in shader source"));
		}
		if (!tokenError.empty())
		{
			return Result<Vector<Token>>::Failure(tokenError);
		}
		tokens.push_back({TokenKind::Newline, {}, location});
		return Result<Vector<Token>>::Success(std::move(tokens));
	}

private:
	String Error(SourceLocation position, const String& message) const
	{
		return sourcePath + ":" + Number(position.line) + ":" + Number(position.column) + ": " + message;
	}

	static String Number(UInt32 value)
	{
		String text;
		if (value == 0)
		{
			return "0";
		}
		while (value > 0)
		{
			text.insert(text.begin(), static_cast<char>('0' + (value % 10)));
			value /= 10;
		}
		return text;
	}

	void Advance()
	{
		if (source[offset] == '\n')
		{
			++location.line;
			location.column = 1;
		}
		else
		{
			++location.column;
		}
		++offset;
	}

	void Add(TokenKind kind, Ursa::Size begin, Ursa::Size end, SourceLocation position)
	{
		tokens.push_back({kind, Slice(source, begin, end), position});
	}

	void SkipLineComment()
	{
		while (offset < source.size() && source[offset] != '\n')
		{
			Advance();
		}
	}

	Result<void> SkipBlockComment()
	{
		const SourceLocation start = location;
		Advance();
		Advance();
		while (offset < source.size() && !Matches(source, offset, "*/"))
		{
			Advance();
		}
		if (offset == source.size())
		{
			return Result<void>::Failure(Error(start, "Unterminated block comment"));
		}
		Advance();
		Advance();
		return {};
	}

	Result<void> ReadString()
	{
		const SourceLocation start = location;
		Advance();
		String value;
		while (offset < source.size() && source[offset] != '"')
		{
			if (source[offset] == '\n')
			{
				return Result<void>::Failure(Error(start, "String literal cannot contain a raw newline"));
			}
			if (source[offset] == '\\' && offset + 1 < source.size() &&
			    (source[offset + 1] == '\\' || source[offset + 1] == '"'))
			{
				Advance();
				value.push_back(source[offset]);
				Advance();
				continue;
			}
			value.push_back(source[offset]);
			Advance();
		}
		if (offset == source.size())
		{
			return Result<void>::Failure(Error(start, "Unterminated string literal"));
		}
		Advance();
		tokens.push_back({TokenKind::String, std::move(value), start});
		return {};
	}

	void ReadIdentifier()
	{
		const Ursa::Size begin = offset;
		const SourceLocation start = location;
		while (offset < source.size() && IsIdentifierPart(source[offset]))
		{
			Advance();
		}
		const String identifier = Slice(source, begin, offset);
		if (identifier == "HLSLPROGRAM" || identifier == "HLSLINCLUDE")
		{
			const TokenKind kind = identifier == "HLSLPROGRAM" ? TokenKind::Program : TokenKind::Include;
			const char* terminator = "ENDHLSL";
			Ursa::Size end = offset;
			while (end < source.size() && (!Matches(source, end, terminator) || !HasWordBoundary(source, end, 7) ||
			                               (end > 0 && IsIdentifierPart(source[end - 1]))))
			{
				++end;
			}
			if (end == source.size())
			{
				tokenError = Error(start, "Missing ENDHLSL for " + identifier);
				offset = source.size();
				return;
			}
			Ursa::Size blockStart = offset;
			while (blockStart < end && (source[blockStart] == '\r' || source[blockStart] == '\n'))
			{
				++blockStart;
			}
			String block = Slice(source, blockStart, end);
			SourceLocation blockLocation = location;
			for (Ursa::Size position = offset; position < blockStart; ++position)
			{
				if (source[position] == '\n')
				{
					++blockLocation.line;
					blockLocation.column = 1;
				}
				else
				{
					++blockLocation.column;
				}
			}
			while (offset < end)
			{
				Advance();
			}
			for (Ursa::Size index = 0; index < 7; ++index)
			{
				Advance();
			}
			tokens.push_back({kind, std::move(block), blockLocation});
			return;
		}
		tokens.push_back({TokenKind::Identifier, identifier, start});
	}

	void ReadNumber()
	{
		const Ursa::Size begin = offset;
		const SourceLocation start = location;
		if (source[offset] == '-' || source[offset] == '+')
		{
			Advance();
		}
		while (offset < source.size() &&
		       (std::isdigit(static_cast<unsigned char>(source[offset])) != 0 || source[offset] == '.' ||
		        source[offset] == 'e' || source[offset] == 'E' || source[offset] == '+' || source[offset] == '-'))
		{
			if ((source[offset] == '+' || source[offset] == '-') && offset > begin && source[offset - 1] != 'e' &&
			    source[offset - 1] != 'E')
			{
				break;
			}
			Advance();
		}
		if (offset < source.size() && std::isalpha(static_cast<unsigned char>(source[offset])) != 0)
		{
			while (offset < source.size() && IsIdentifierPart(source[offset]))
			{
				Advance();
			}
			tokens.push_back({TokenKind::Identifier, Slice(source, begin, offset), start});
			return;
		}
		Add(TokenKind::Number, begin, offset, start);
	}

	const String& sourcePath;
	const String& source;
	Vector<Token> tokens;
	String tokenError;
	Ursa::Size offset = 0;
	SourceLocation location;
};

class Parser final
{
public:
	Parser(const String& path, Vector<Token> sourceTokens) : sourcePath(path), tokens(std::move(sourceTokens))
	{
	}

	Result<ShaderSourceDocument> Run()
	{
		SkipNewlines();
		if (!ConsumeIdentifier("Shader"))
		{
			return FailDocument(Current(), "Expected Shader declaration");
		}
		auto name = Expect(TokenKind::String, "Expected quoted Shader name");
		if (!name)
		{
			return Result<ShaderSourceDocument>::Failure(name.error);
		}
		document.sourcePath = sourcePath;
		document.name = name.value;
		auto opened = ExpectPunctuation("{");
		if (!opened)
		{
			return Result<ShaderSourceDocument>::Failure(opened.error);
		}
		while (!AtEnd() && !IsPunctuation(Current(), "}"))
		{
			SkipNewlines();
			if (AtEnd() || IsPunctuation(Current(), "}"))
			{
				break;
			}
			if (ConsumeIdentifier("Properties"))
			{
				auto parsed = ParseProperties();
				if (!parsed)
				{
					return Result<ShaderSourceDocument>::Failure(parsed.error);
				}
			}
			else if (ConsumeIdentifier("Version"))
			{
				if (versionDeclared)
				{
					return FailDocument(tokens[index - 1], "Duplicate shader source format version");
				}
				auto version = Expect(TokenKind::Number, "Expected integer shader format version");
				if (!version)
				{
					return Result<ShaderSourceDocument>::Failure(version.error);
				}
				if (version.value != "1")
				{
					return FailDocument(tokens[index - 1], "Unsupported shader source format version " + version.value);
				}
				versionDeclared = true;
			}
			else if (ConsumeIdentifier("ID"))
			{
				if (idDeclared)
				{
					return FailDocument(tokens[index - 1], "Duplicate Shader ID");
				}
				auto text = Expect(TokenKind::String, "Expected quoted Shader resource ID");
				if (!text)
				{
					return Result<ShaderSourceDocument>::Failure(text.error);
				}
				auto id = ResourceId::Parse(text.value);
				if (!id)
				{
					return Result<ShaderSourceDocument>::Failure(Error(tokens[index - 1], id.error));
				}
				// Legacy source IDs are accepted only for migration; asset metadata owns identity.
				idDeclared = true;
			}
			else if (ConsumeIdentifier("SubShader"))
			{
				auto parsed = ParseSubShader();
				if (!parsed)
				{
					return Result<ShaderSourceDocument>::Failure(parsed.error);
				}
				document.subShaders.push_back(std::move(parsed.value));
			}
			else if (ConsumeIdentifier("Fallback"))
			{
				auto value = Expect(TokenKind::String, "Expected quoted fallback shader name");
				if (!value)
				{
					return Result<ShaderSourceDocument>::Failure(value.error);
				}
				document.fallback = std::move(value.value);
			}
			else if (ConsumeIdentifier("CustomEditor"))
			{
				auto value = Expect(TokenKind::String, "Expected quoted CustomEditor name");
				if (!value)
				{
					return Result<ShaderSourceDocument>::Failure(value.error);
				}
				document.customEditor = std::move(value.value);
			}
			else if (Current().kind == TokenKind::Include)
			{
				document.includes.push_back({ProgramBlockKind::Include, Current().value, {}, Current().location});
				++index;
			}
			else
			{
				return FailDocument(Current(), "Unexpected declaration in Shader block");
			}
		}
		auto closed = ExpectPunctuation("}");
		if (!closed)
		{
			return Result<ShaderSourceDocument>::Failure(closed.error);
		}
		SkipNewlines();
		if (!AtEnd())
		{
			return FailDocument(Current(), "Unexpected content after Shader block");
		}
		if (document.subShaders.empty())
		{
			return FailDocument(tokens.front(), "Shader must declare at least one SubShader");
		}
		return Result<ShaderSourceDocument>::Success(std::move(document));
	}

private:
	String Error(const Token& token, const String& message) const
	{
		return sourcePath + ":" + Number(token.location.line) + ":" + Number(token.location.column) + ": " + message;
	}

	static String Number(UInt32 value)
	{
		String text;
		if (value == 0)
		{
			return "0";
		}
		while (value > 0)
		{
			text.insert(text.begin(), static_cast<char>('0' + (value % 10)));
			value /= 10;
		}
		return text;
	}

	bool AtEnd() const
	{
		return index >= tokens.size();
	}

	const Token& Current() const
	{
		static const Token emptyEnd{TokenKind::Newline, {}, {}};
		return AtEnd() ? (tokens.empty() ? emptyEnd : tokens.back()) : tokens[index];
	}

	bool ConsumeIdentifier(const char* value)
	{
		if (!AtEnd() && Current().kind == TokenKind::Identifier && Current().value == value)
		{
			++index;
			return true;
		}
		return false;
	}

	void SkipNewlines()
	{
		while (!AtEnd() && Current().kind == TokenKind::Newline)
		{
			++index;
		}
	}

	bool IsPunctuation(const Token& token, const char* value) const
	{
		return token.kind == TokenKind::Punctuation && token.value == value;
	}

	Result<String> Expect(TokenKind kind, const String& diagnostic)
	{
		SkipNewlines();
		if (AtEnd() || Current().kind != kind)
		{
			return Result<String>::Failure(Error(Current(), diagnostic));
		}
		String value = Current().value;
		++index;
		return Result<String>::Success(std::move(value));
	}

	Result<void> ExpectPunctuation(const char* value)
	{
		SkipNewlines();
		if (AtEnd() || !IsPunctuation(Current(), value))
		{
			return Result<void>::Failure(Error(Current(), String("Expected '") + value + "'"));
		}
		++index;
		return {};
	}

	Result<ProgramBlock> ParseProgramBlock()
	{
		ProgramBlock block;
		block.kind = ProgramBlockKind::Program;
		block.source = Current().value;
		block.location = Current().location;
		const Token blockToken = Current();
		++index;
		Ursa::Size lineStart = 0;
		UInt32 lineOffset = 0;
		while (lineStart <= block.source.size())
		{
			const Ursa::Size newline = block.source.find('\n', lineStart);
			const Ursa::Size lineEnd = newline == String::npos ? block.source.size() : newline;
			Ursa::Size cursor = lineStart;
			while (cursor < lineEnd &&
			       (block.source[cursor] == ' ' || block.source[cursor] == '\t' || block.source[cursor] == '\r'))
			{
				++cursor;
			}
			if (cursor < lineEnd && block.source.compare(cursor, 7, "#pragma") == 0)
			{
				cursor += 7;
				while (cursor < lineEnd && (block.source[cursor] == ' ' || block.source[cursor] == '\t'))
				{
					++cursor;
				}
				const Ursa::Size directiveStart = cursor;
				while (cursor < lineEnd && IsIdentifierPart(block.source[cursor]))
				{
					++cursor;
				}
				const String directive = block.source.substr(directiveStart, cursor - directiveStart);
				ProgramStage stage = ProgramStage::Vertex;
				bool isStage = true;
				if (directive == "vertex")
				{
					stage = ProgramStage::Vertex;
				}
				else if (directive == "fragment")
				{
					stage = ProgramStage::Fragment;
				}
				else if (directive == "compute")
				{
					stage = ProgramStage::Compute;
				}
				else
				{
					isStage = false;
				}
				if (isStage)
				{
					while (cursor < lineEnd && (block.source[cursor] == ' ' || block.source[cursor] == '\t'))
					{
						++cursor;
					}
					const Ursa::Size entryStart = cursor;
					while (cursor < lineEnd && IsIdentifierPart(block.source[cursor]))
					{
						++cursor;
					}
					if (cursor == entryStart)
					{
						Token location = blockToken;
						location.location.line += lineOffset;
						return Result<ProgramBlock>::Failure(
						    Error(location, "Stage pragma is missing its entry point"));
					}
					for (const ProgramEntryPoint& existing : block.entryPoints)
					{
						if (existing.stage == stage)
						{
							Token location = blockToken;
							location.location.line += lineOffset;
							return Result<ProgramBlock>::Failure(Error(location, "Duplicate stage pragma"));
						}
					}
					SourceLocation location = blockToken.location;
					location.line += lineOffset;
					block.entryPoints.push_back(
					    {stage, block.source.substr(entryStart, cursor - entryStart), location});
				}
				else if (directive == "shader_feature" || directive == "shader_feature_local" ||
				         directive == "multi_compile" || directive == "multi_compile_local")
				{
					ShaderKeywordDirective keywordDirective;
					keywordDirective.kind = directive.find("shader_feature") == 0
					                            ? ShaderKeywordDirectiveKind::ShaderFeature
					                            : ShaderKeywordDirectiveKind::MultiCompile;
					keywordDirective.location = blockToken.location;
					keywordDirective.location.line += lineOffset;
					Set<String> uniqueOptions;
					while (cursor < lineEnd)
					{
						while (cursor < lineEnd && IsWhitespace(block.source[cursor]))
						{
							++cursor;
						}
						if (cursor >= lineEnd || block.source[cursor] == '/')
						{
							break;
						}
						const Ursa::Size optionStart = cursor;
						while (cursor < lineEnd && IsIdentifierPart(block.source[cursor]))
						{
							++cursor;
						}
						if (optionStart == cursor)
						{
							return Result<ProgramBlock>::Failure(
							    Error(blockToken, "Keyword pragma options must be identifiers"));
						}
						String option = block.source.substr(optionStart, cursor - optionStart);
						if (option == "_")
						{
							option.clear();
						}
						if (!uniqueOptions.insert(option).second)
						{
							return Result<ProgramBlock>::Failure(
							    Error(blockToken, "Keyword pragma contains a duplicate option"));
						}
						keywordDirective.options.push_back(std::move(option));
					}
					if (keywordDirective.options.empty())
					{
						return Result<ProgramBlock>::Failure(
						    Error(blockToken, "Keyword pragma requires at least one option"));
					}
					if (keywordDirective.kind == ShaderKeywordDirectiveKind::ShaderFeature &&
					    std::find(keywordDirective.options.begin(), keywordDirective.options.end(), String{}) ==
					        keywordDirective.options.end())
					{
						keywordDirective.options.insert(keywordDirective.options.begin(), {});
					}
					block.keywordDirectives.push_back(std::move(keywordDirective));
				}
			}
			if (newline == String::npos)
			{
				break;
			}
			lineStart = newline + 1;
			++lineOffset;
		}
		if (block.entryPoints.empty())
		{
			return Result<ProgramBlock>::Failure(
			    Error(blockToken, "HLSLPROGRAM must declare a vertex, fragment, or compute stage"));
		}
		return Result<ProgramBlock>::Success(std::move(block));
	}

	Result<ShaderSourceDocument> FailDocument(const Token& token, const String& message) const
	{
		return Result<ShaderSourceDocument>::Failure(Error(token, message));
	}

	Result<void> ParseProperties()
	{
		auto opened = ExpectPunctuation("{");
		if (!opened)
		{
			return opened;
		}
		while (!AtEnd() && !IsPunctuation(Current(), "}"))
		{
			SkipNewlines();
			if (AtEnd() || IsPunctuation(Current(), "}"))
			{
				break;
			}
			PropertyDeclaration property;
			property.location = Current().location;
			while (IsPunctuation(Current(), "["))
			{
				++index;
				auto attribute = Expect(TokenKind::Identifier, "Expected property attribute name");
				if (!attribute)
				{
					return Result<void>::Failure(attribute.error);
				}
				if (!IsSupportedAttribute(attribute.value))
				{
					return Result<void>::Failure(Error(Current(), "Unsupported property attribute " + attribute.value));
				}
				for (const String& existing : property.attributes)
				{
					if (existing == attribute.value)
					{
						return Result<void>::Failure(
						    Error(Current(), "Duplicate property attribute " + attribute.value));
					}
				}
				property.attributes.push_back(std::move(attribute.value));
				auto closedAttribute = ExpectPunctuation("]");
				if (!closedAttribute)
				{
					return closedAttribute;
				}
			}
			auto identifier = Expect(TokenKind::Identifier, "Expected property identifier");
			if (!identifier)
			{
				return Result<void>::Failure(identifier.error);
			}
			property.identifier = std::move(identifier.value);
			auto openDisplay = ExpectPunctuation("(");
			if (!openDisplay)
			{
				return openDisplay;
			}
			auto display = Expect(TokenKind::String, "Expected quoted property display name");
			if (!display)
			{
				return Result<void>::Failure(display.error);
			}
			property.displayName = std::move(display.value);
			auto separator = ExpectPunctuation(",");
			if (!separator)
			{
				return separator;
			}
			auto type = Expect(TokenKind::Identifier, "Expected property type");
			if (!type)
			{
				return Result<void>::Failure(type.error);
			}
			if (type.value == "Float")
			{
				property.type = PropertyType::Float;
			}
			else if (type.value == "Range")
			{
				property.type = PropertyType::Range;
				auto openRange = ExpectPunctuation("(");
				if (!openRange)
				{
					return openRange;
				}
				auto minimum = ReadFloat("Expected numeric Range minimum", property.location);
				if (!minimum)
				{
					return Result<void>::Failure(minimum.error);
				}
				property.rangeMinimum = minimum.value;
				auto rangeSeparator = ExpectPunctuation(",");
				if (!rangeSeparator)
				{
					return rangeSeparator;
				}
				auto maximum = ReadFloat("Expected numeric Range maximum", property.location);
				if (!maximum)
				{
					return Result<void>::Failure(maximum.error);
				}
				property.rangeMaximum = maximum.value;
				auto closeRange = ExpectPunctuation(")");
				if (!closeRange)
				{
					return closeRange;
				}
			}
			else if (type.value == "Int")
			{
				property.type = PropertyType::Int;
			}
			else if (type.value == "Color")
			{
				property.type = PropertyType::Color;
			}
			else if (type.value == "Vector")
			{
				property.type = PropertyType::Vector;
			}
			else if (type.value == "2D" || type.value == "Texture2D")
			{
				property.type = PropertyType::Texture2D;
			}
			else
			{
				return Result<void>::Failure(Error(tokens[index - 1], "Unsupported property type " + type.value));
			}
			auto closeDisplay = ExpectPunctuation(")");
			if (!closeDisplay)
			{
				return closeDisplay;
			}
			auto equals = ExpectPunctuation("=");
			if (!equals)
			{
				return equals;
			}
			if (property.type == PropertyType::Float || property.type == PropertyType::Range)
			{
				auto value = ReadFloat("Expected numeric property default", property.location);
				if (!value)
				{
					return Result<void>::Failure(value.error);
				}
				property.defaultFloat = value.value;
				if (property.type == PropertyType::Range &&
				    (property.rangeMinimum > property.rangeMaximum || property.defaultFloat < property.rangeMinimum ||
				     property.defaultFloat > property.rangeMaximum))
				{
					return Result<void>::Failure(Error(tokens[index - 1], "Range bounds or default value are invalid"));
				}
			}
			else if (property.type == PropertyType::Int)
			{
				auto value = ReadInteger("Expected integer property default", property.location);
				if (!value)
				{
					return Result<void>::Failure(value.error);
				}
				property.defaultInt = value.value;
			}
			else if (property.type == PropertyType::Color || property.type == PropertyType::Vector)
			{
				auto value = ReadVector(property.location);
				if (!value)
				{
					return Result<void>::Failure(value.error);
				}
				property.defaultVector = value.value;
				if (property.type == PropertyType::Color && !HasAttribute(property, "HDR"))
				{
					for (Ursa::Size component = 0; component < property.defaultVector.size(); ++component)
					{
						if (property.defaultVector[component] < 0.0f || property.defaultVector[component] > 1.0f)
						{
							return Result<void>::Failure(
							    Error(tokens[index - 1], "Color defaults without HDR must be in [0, 1]"));
						}
					}
				}
			}
			else
			{
				auto texture = Expect(TokenKind::String, "Expected quoted texture default");
				if (!texture)
				{
					return Result<void>::Failure(texture.error);
				}
				property.defaultTexture = std::move(texture.value);
				if (property.defaultTexture != "" && property.defaultTexture != "white" &&
				    property.defaultTexture != "black" && property.defaultTexture != "gray" &&
				    property.defaultTexture != "bump" && property.defaultTexture != "red")
				{
					return Result<void>::Failure(Error(tokens[index - 1], "Unsupported built-in texture default"));
				}
				auto openOptions = ExpectPunctuation("{");
				if (!openOptions)
				{
					return openOptions;
				}
				Ursa::Int32 nesting = 1;
				while (!AtEnd() && nesting > 0)
				{
					if (IsPunctuation(Current(), "{"))
					{
						++nesting;
					}
					else if (IsPunctuation(Current(), "}"))
					{
						--nesting;
					}
					if (nesting > 0)
					{
						property.textureOptions += Current().value;
					}
					++index;
				}
				if (nesting != 0)
				{
					return Result<void>::Failure(Error(tokens[index - 1], "Unterminated texture property options"));
				}
			}
			if (!ValidateAttributeTypes(property))
			{
				return Result<void>::Failure(
				    Error(tokens[index - 1], "Property attribute is incompatible with its type"));
			}
			for (const PropertyDeclaration& existing : document.properties)
			{
				if (existing.identifier == property.identifier)
				{
					return Result<void>::Failure(
					    Error(Token{TokenKind::Identifier, property.identifier, property.location},
					          "Duplicate property identifier " + property.identifier));
				}
			}
			document.properties.push_back(std::move(property));
		}
		return ExpectPunctuation("}");
	}

	static bool IsSupportedAttribute(const String& attribute)
	{
		return attribute == "HideInInspector" || attribute == "HDR" || attribute == "Normal" ||
		       attribute == "NoScaleOffset" || attribute == "Gamma" || attribute == "MainColor" ||
		       attribute == "MainTexture";
	}

	static bool HasAttribute(const PropertyDeclaration& property, const String& attribute)
	{
		for (const String& current : property.attributes)
		{
			if (current == attribute)
			{
				return true;
			}
		}
		return false;
	}

	static bool ValidateAttributeTypes(const PropertyDeclaration& property)
	{
		const bool texture = property.type == PropertyType::Texture2D;
		const bool color = property.type == PropertyType::Color;
		for (const String& attribute : property.attributes)
		{
			if ((attribute == "HDR" || attribute == "MainColor") && !color)
			{
				return false;
			}
			if ((attribute == "Normal" || attribute == "NoScaleOffset" || attribute == "MainTexture") && !texture)
			{
				return false;
			}
		}
		return true;
	}

	Result<Float32> ReadFloat(const String& diagnostic, SourceLocation position)
	{
		auto token = Expect(TokenKind::Number, diagnostic);
		if (!token)
		{
			return Result<Float32>::Failure(token.error);
		}
		const std::string text(token.value.begin(), token.value.end());
		char* end = nullptr;
		const float parsed = std::strtof(text.c_str(), &end);
		if (end != text.c_str() + text.size() || !std::isfinite(parsed))
		{
			return Result<Float32>::Failure(
			    Error(Token{TokenKind::Number, token.value, position}, "Property numeric value must be finite"));
		}
		return Result<Float32>::Success(parsed);
	}

	Result<Int32> ReadInteger(const String& diagnostic, SourceLocation position)
	{
		auto token = Expect(TokenKind::Number, diagnostic);
		if (!token)
		{
			return Result<Int32>::Failure(token.error);
		}
		const std::string text(token.value.begin(), token.value.end());
		char* end = nullptr;
		const long parsed = std::strtol(text.c_str(), &end, 10);
		if (end != text.c_str() + text.size() || parsed < std::numeric_limits<Int32>::min() ||
		    parsed > std::numeric_limits<Int32>::max())
		{
			return Result<Int32>::Failure(Error(Token{TokenKind::Number, token.value, position},
			                                    "Integer property default is invalid or out of range"));
		}
		return Result<Int32>::Success(static_cast<Int32>(parsed));
	}

	Result<Array<Float32, 4>> ReadVector(SourceLocation position)
	{
		auto opened = ExpectPunctuation("(");
		if (!opened)
		{
			return Result<Array<Float32, 4>>::Failure(opened.error);
		}
		Array<Float32, 4> values{};
		for (Ursa::Size component = 0; component < values.size(); ++component)
		{
			auto value = ReadFloat("Expected numeric vector component", position);
			if (!value)
			{
				return Result<Array<Float32, 4>>::Failure(value.error);
			}
			values[component] = value.value;
			if (component + 1 < values.size())
			{
				auto separator = ExpectPunctuation(",");
				if (!separator)
				{
					return Result<Array<Float32, 4>>::Failure(separator.error);
				}
			}
		}
		auto closed = ExpectPunctuation(")");
		return closed ? Result<Array<Float32, 4>>::Success(values) : Result<Array<Float32, 4>>::Failure(closed.error);
	}

	Result<Vector<TagDeclaration>> ParseTags()
	{
		Vector<TagDeclaration> tags;
		auto opened = ExpectPunctuation("{");
		if (!opened)
		{
			return Result<Vector<TagDeclaration>>::Failure(opened.error);
		}
		while (!AtEnd() && !IsPunctuation(Current(), "}"))
		{
			SkipNewlines();
			if (AtEnd() || IsPunctuation(Current(), "}"))
			{
				break;
			}
			auto name = Expect(Current().kind, "Expected Tag name");
			if (!name)
			{
				return Result<Vector<TagDeclaration>>::Failure(name.error);
			}
			auto equals = ExpectPunctuation("=");
			if (!equals)
			{
				return Result<Vector<TagDeclaration>>::Failure(equals.error);
			}
			if (AtEnd() || (Current().kind != TokenKind::String && Current().kind != TokenKind::Identifier &&
			                Current().kind != TokenKind::Number))
			{
				return Result<Vector<TagDeclaration>>::Failure(Error(Current(), "Expected Tag value"));
			}
			tags.push_back({std::move(name.value), Current().value});
			++index;
		}
		auto closed = ExpectPunctuation("}");
		return closed ? Result<Vector<TagDeclaration>>::Success(std::move(tags))
		              : Result<Vector<TagDeclaration>>::Failure(closed.error);
	}

	Result<Vector<RenderStateDeclaration>> ParseStateLine()
	{
		Vector<RenderStateDeclaration> states;
		if (AtEnd() || Current().kind != TokenKind::Identifier)
		{
			return Result<Vector<RenderStateDeclaration>>::Failure(Error(Current(), "Expected render state"));
		}
		RenderStateDeclaration state;
		state.name = Current().value;
		state.location = Current().location;
		++index;
		while (!AtEnd() && Current().kind != TokenKind::Newline && !IsPunctuation(Current(), "}"))
		{
			if (!state.value.empty() && Current().kind != TokenKind::Punctuation && state.value.back() != ',')
			{
				state.value += " ";
			}
			state.value += Current().value;
			++index;
		}
		states.push_back(std::move(state));
		return Result<Vector<RenderStateDeclaration>>::Success(std::move(states));
	}

	Result<SubShaderDeclaration> ParseSubShader()
	{
		SubShaderDeclaration subShader;
		subShader.location = Current().location;
		auto opened = ExpectPunctuation("{");
		if (!opened)
		{
			return Result<SubShaderDeclaration>::Failure(opened.error);
		}
		while (!AtEnd() && !IsPunctuation(Current(), "}"))
		{
			SkipNewlines();
			if (AtEnd() || IsPunctuation(Current(), "}"))
			{
				break;
			}
			if (ConsumeIdentifier("Tags"))
			{
				auto tags = ParseTags();
				if (!tags)
				{
					return Result<SubShaderDeclaration>::Failure(tags.error);
				}
				subShader.tags = std::move(tags.value);
			}
			else if (ConsumeIdentifier("Stencil"))
			{
				auto states = ParseStencilBlock();
				if (!states)
				{
					return Result<SubShaderDeclaration>::Failure(states.error);
				}
				subShader.renderStates.insert(subShader.renderStates.end(), states.value.begin(), states.value.end());
			}
			else if (ConsumeIdentifier("Pass"))
			{
				auto pass = ParsePass();
				if (!pass)
				{
					return Result<SubShaderDeclaration>::Failure(pass.error);
				}
				subShader.passOrder.push_back({static_cast<UInt32>(subShader.passes.size()), {}, false});
				subShader.passes.push_back(std::move(pass.value));
			}
			else if (ConsumeIdentifier("UsePass"))
			{
				auto reused = Expect(TokenKind::String, "Expected quoted UsePass reference");
				if (!reused)
				{
					return Result<SubShaderDeclaration>::Failure(reused.error);
				}
				subShader.passOrder.push_back({0, reused.value, true});
				subShader.reusedPasses.push_back(std::move(reused.value));
			}
			else if (Current().kind == TokenKind::Include)
			{
				subShader.includes.push_back({ProgramBlockKind::Include, Current().value, {}, Current().location});
				++index;
			}
			else
			{
				auto states = ParseStateLine();
				if (!states)
				{
					return Result<SubShaderDeclaration>::Failure(states.error);
				}
				subShader.renderStates.insert(subShader.renderStates.end(), states.value.begin(), states.value.end());
			}
		}
		auto closed = ExpectPunctuation("}");
		return closed ? Result<SubShaderDeclaration>::Success(std::move(subShader))
		              : Result<SubShaderDeclaration>::Failure(closed.error);
	}

	Result<PassDeclaration> ParsePass()
	{
		PassDeclaration pass;
		pass.location = Current().location;
		auto opened = ExpectPunctuation("{");
		if (!opened)
		{
			return Result<PassDeclaration>::Failure(opened.error);
		}
		while (!AtEnd() && !IsPunctuation(Current(), "}"))
		{
			SkipNewlines();
			if (AtEnd() || IsPunctuation(Current(), "}"))
			{
				break;
			}
			if (ConsumeIdentifier("Name"))
			{
				auto name = Expect(TokenKind::String, "Expected quoted Pass name");
				if (!name)
				{
					return Result<PassDeclaration>::Failure(name.error);
				}
				pass.name = std::move(name.value);
			}
			else if (ConsumeIdentifier("Tags"))
			{
				auto tags = ParseTags();
				if (!tags)
				{
					return Result<PassDeclaration>::Failure(tags.error);
				}
				pass.tags = std::move(tags.value);
			}
			else if (ConsumeIdentifier("Stencil"))
			{
				auto states = ParseStencilBlock();
				if (!states)
				{
					return Result<PassDeclaration>::Failure(states.error);
				}
				pass.renderStates.insert(pass.renderStates.end(), states.value.begin(), states.value.end());
			}
			else if (Current().kind == TokenKind::Program || Current().kind == TokenKind::Include)
			{
				if (Current().kind == TokenKind::Program)
				{
					auto program = ParseProgramBlock();
					if (!program)
					{
						return Result<PassDeclaration>::Failure(program.error);
					}
					pass.programs.push_back(std::move(program.value));
				}
				else
				{
					pass.programs.push_back({ProgramBlockKind::Include, Current().value, {}, Current().location});
					++index;
				}
			}
			else
			{
				auto states = ParseStateLine();
				if (!states)
				{
					return Result<PassDeclaration>::Failure(states.error);
				}
				pass.renderStates.insert(pass.renderStates.end(), states.value.begin(), states.value.end());
			}
		}
		auto closed = ExpectPunctuation("}");
		return closed ? Result<PassDeclaration>::Success(std::move(pass))
		              : Result<PassDeclaration>::Failure(closed.error);
	}

	Result<Vector<RenderStateDeclaration>> ParseStencilBlock()
	{
		auto opened = ExpectPunctuation("{");
		if (!opened)
		{
			return Result<Vector<RenderStateDeclaration>>::Failure(opened.error);
		}
		Vector<RenderStateDeclaration> states;
		while (!AtEnd() && !IsPunctuation(Current(), "}"))
		{
			SkipNewlines();
			if (AtEnd() || IsPunctuation(Current(), "}"))
			{
				break;
			}
			String prefix = "Stencil.";
			if (ConsumeIdentifier("Front"))
			{
				prefix = "Stencil.Front.";
				auto group = ExpectPunctuation("{");
				if (!group)
				{
					return Result<Vector<RenderStateDeclaration>>::Failure(group.error);
				}
				auto parsed = ParseStencilContents(states, prefix);
				if (!parsed)
				{
					return Result<Vector<RenderStateDeclaration>>::Failure(parsed.error);
				}
			}
			else if (ConsumeIdentifier("Back"))
			{
				prefix = "Stencil.Back.";
				auto group = ExpectPunctuation("{");
				if (!group)
				{
					return Result<Vector<RenderStateDeclaration>>::Failure(group.error);
				}
				auto parsed = ParseStencilContents(states, prefix);
				if (!parsed)
				{
					return Result<Vector<RenderStateDeclaration>>::Failure(parsed.error);
				}
			}
			else
			{
				auto line = ParseStateLine();
				if (!line)
				{
					return Result<Vector<RenderStateDeclaration>>::Failure(line.error);
				}
				for (RenderStateDeclaration& state : line.value)
				{
					state.name = prefix + state.name;
					states.push_back(std::move(state));
				}
			}
		}
		auto closed = ExpectPunctuation("}");
		return closed ? Result<Vector<RenderStateDeclaration>>::Success(std::move(states))
		              : Result<Vector<RenderStateDeclaration>>::Failure(closed.error);
	}

	Result<void> ParseStencilContents(Vector<RenderStateDeclaration>& states, const String& prefix)
	{
		while (!AtEnd() && !IsPunctuation(Current(), "}"))
		{
			SkipNewlines();
			if (AtEnd() || IsPunctuation(Current(), "}"))
			{
				break;
			}
			auto line = ParseStateLine();
			if (!line)
			{
				return Result<void>::Failure(line.error);
			}
			for (RenderStateDeclaration& state : line.value)
			{
				state.name = prefix + state.name;
				states.push_back(std::move(state));
			}
		}
		return ExpectPunctuation("}");
	}

	const String& sourcePath;
	Vector<Token> tokens;
	ShaderSourceDocument document;
	bool versionDeclared = false;
	bool idDeclared = false;
	Ursa::Size index = 0;
};
} // namespace

Result<ShaderSourceDocument> ParseShaderSource(const String& sourcePath, const String& source)
{
	Lexer lexer(sourcePath, source);
	auto tokens = lexer.Run();
	if (!tokens)
	{
		return Result<ShaderSourceDocument>::Failure(tokens.error);
	}
	Parser parser(sourcePath, std::move(tokens.value));
	return parser.Run();
}

Result<ShaderSourceDocument> ParseComputeSource(const String& sourcePath, const String& source)
{
	struct Kernel
	{
		String name;
		SourceLocation location;
		Array<UInt32, 3> threadGroup{1, 1, 1};
		bool hasThreadGroup = false;
	};

	Vector<Kernel> kernels;
	String compiledSource;
	UInt32 lineNumber = 1;
	for (Ursa::Size lineStart = 0; lineStart < source.size();)
	{
		const Ursa::Size newline = source.find('\n', lineStart);
		const Ursa::Size lineEnd = newline == String::npos ? source.size() : newline;
		String line = source.substr(lineStart, lineEnd - lineStart);
		Ursa::Size cursor = 0;
		while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t' || line[cursor] == '\r'))
		{
			++cursor;
		}
		bool kernelDirective = false;
		constexpr char directive[] = "#pragma kernel";
		if (line.compare(cursor, sizeof(directive) - 1, directive) == 0)
		{
			cursor += sizeof(directive) - 1;
			if (cursor >= line.size() || (line[cursor] != ' ' && line[cursor] != '\t'))
			{
				return Result<ShaderSourceDocument>::Failure(sourcePath + ":" +
				                                             ToUrsaString(std::to_string(lineNumber)) +
				                                             ": malformed #pragma kernel directive");
			}
			while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
			{
				++cursor;
			}
			const Ursa::Size nameStart = cursor;
			if (cursor >= line.size() || !IsIdentifierStart(line[cursor]))
			{
				return Result<ShaderSourceDocument>::Failure(
				    sourcePath + ":" + ToUrsaString(std::to_string(lineNumber)) + ": kernel name is missing");
			}
			while (cursor < line.size() && IsIdentifierPart(line[cursor]))
			{
				++cursor;
			}
			Kernel kernel;
			kernel.name = line.substr(nameStart, cursor - nameStart);
			kernel.location = {lineNumber, static_cast<UInt32>(nameStart + 1)};
			if (std::find_if(kernels.begin(), kernels.end(),
			                 [&](const Kernel& existing) { return existing.name == kernel.name; }) != kernels.end())
			{
				return Result<ShaderSourceDocument>::Failure(sourcePath + ": duplicate compute kernel " + kernel.name);
			}
			kernels.push_back(std::move(kernel));
			kernelDirective = true;
		}
		if (!kernelDirective)
		{
			compiledSource.append(line.data(), line.size());
		}
		if (newline == String::npos)
		{
			break;
		}
		compiledSource.push_back('\n');
		lineStart = newline + 1;
		++lineNumber;
	}
	if (kernels.empty())
	{
		return Result<ShaderSourceDocument>::Failure(sourcePath + ": no #pragma kernel declarations found");
	}
	const auto parseDimension = [&](Ursa::Size begin, Ursa::Size end) -> Result<UInt32>
	{
		while (begin < end && (source[begin] == ' ' || source[begin] == '\t'))
		{
			++begin;
		}
		while (end > begin && (source[end - 1] == ' ' || source[end - 1] == '\t'))
		{
			--end;
		}
		UInt32 value = 0;
		if (begin == end)
		{
			return Result<UInt32>::Failure("Empty numthreads dimension");
		}
		for (Ursa::Size index = begin; index < end; ++index)
		{
			if (source[index] < '0' || source[index] > '9' ||
			    value > ((std::numeric_limits<UInt32>::max)() - UInt32(source[index] - '0')) / 10)
			{
				return Result<UInt32>::Failure("Invalid numthreads dimension");
			}
			value = value * 10 + UInt32(source[index] - '0');
		}
		return Result<UInt32>::Success(value);
	};
	for (Ursa::Size cursor = 0; (cursor = source.find("[numthreads", cursor)) != String::npos;)
	{
		const Ursa::Size open = source.find('(', cursor);
		const Ursa::Size close = open == String::npos ? String::npos : source.find(')', open + 1);
		const Ursa::Size bracket = close == String::npos ? String::npos : source.find(']', close + 1);
		if (open == String::npos || close == String::npos || bracket == String::npos)
		{
			return Result<ShaderSourceDocument>::Failure(sourcePath + ": malformed [numthreads] attribute");
		}
		Array<UInt32, 3> dimensions{};
		Ursa::Size valueStart = open + 1;
		bool valid = true;
		for (Ursa::Size dimension = 0; dimension < dimensions.size(); ++dimension)
		{
			const Ursa::Size comma = dimension + 1 == dimensions.size() ? close : source.find(',', valueStart);
			if (comma == String::npos || comma > close)
			{
				valid = false;
				break;
			}
			auto parsed = parseDimension(valueStart, comma);
			if (!parsed || parsed.value == 0 || parsed.value > 1024)
			{
				valid = false;
				break;
			}
			dimensions[dimension] = parsed.value;
			valueStart = comma + 1;
		}
		if (!valid || Ursa::UInt64(dimensions[0]) * dimensions[1] * dimensions[2] > 1024)
		{
			return Result<ShaderSourceDocument>::Failure(sourcePath + ": invalid [numthreads] dimensions");
		}
		Ursa::Size function = bracket + 1;
		while (function < source.size() && std::isspace(static_cast<unsigned char>(source[function])) != 0)
		{
			++function;
		}
		if (source.compare(function, 4, "void") != 0)
		{
			return Result<ShaderSourceDocument>::Failure(sourcePath + ": [numthreads] must decorate a void kernel");
		}
		function += 4;
		while (function < source.size() && std::isspace(static_cast<unsigned char>(source[function])) != 0)
		{
			++function;
		}
		const Ursa::Size functionStart = function;
		while (function < source.size() && IsIdentifierPart(source[function]))
		{
			++function;
		}
		const String functionName = source.substr(functionStart, function - functionStart);
		auto kernel = std::find_if(kernels.begin(), kernels.end(),
		                           [&](const Kernel& current) { return current.name == functionName; });
		if (kernel != kernels.end())
		{
			if (kernel->hasThreadGroup)
			{
				return Result<ShaderSourceDocument>::Failure(sourcePath + ": duplicate [numthreads] for kernel " +
				                                             kernel->name);
			}
			kernel->threadGroup = dimensions;
			kernel->hasThreadGroup = true;
		}
		cursor = bracket + 1;
	}
	ShaderSourceDocument document;
	document.sourcePath = sourcePath;
	String sourceName = sourcePath.substr(sourcePath.rfind('/') == String::npos ? 0 : sourcePath.rfind('/') + 1);
	const Ursa::Size extension = sourceName.rfind('.');
	if (extension != String::npos)
	{
		sourceName = sourceName.substr(0, extension);
	}
	document.name = "Compute/" + sourceName;
	SubShaderDeclaration subShader;
	for (Kernel& kernel : kernels)
	{
		if (!kernel.hasThreadGroup)
		{
			return Result<ShaderSourceDocument>::Failure(sourcePath + ": missing [numthreads] for kernel " +
			                                             kernel.name);
		}
		PassDeclaration pass;
		pass.name = kernel.name;
		pass.computeKernel = true;
		pass.threadGroupSize = kernel.threadGroup;
		pass.location = kernel.location;
		ProgramBlock program;
		program.kind = ProgramBlockKind::Program;
		program.source = compiledSource;
		program.location = kernel.location;
		program.entryPoints.push_back({ProgramStage::Compute, kernel.name, kernel.location});
		pass.programs.push_back(std::move(program));
		subShader.passOrder.push_back({static_cast<UInt32>(subShader.passes.size()), {}, false});
		subShader.passes.push_back(std::move(pass));
	}
	document.subShaders.push_back(std::move(subShader));
	return Result<ShaderSourceDocument>::Success(std::move(document));
}

Result<Vector<ShaderKeywordVariant>> ExpandShaderKeywordVariants(const Vector<ShaderKeywordDirective>& directives,
                                                                 const Vector<Vector<String>>& usedKeywordSets,
                                                                 UInt32 maxVariantCount)
{
	if (maxVariantCount == 0)
	{
		return Result<Vector<ShaderKeywordVariant>>::Failure("Shader variant limit must be greater than zero");
	}
	Vector<ShaderKeywordDirective> multiCompile;
	Vector<ShaderKeywordDirective> shaderFeatures;
	Set<String> declaredKeywords;
	Ursa::UInt64 potentialVariantCount = 1;
	for (const ShaderKeywordDirective& directive : directives)
	{
		if (directive.options.empty())
		{
			return Result<Vector<ShaderKeywordVariant>>::Failure("Shader keyword directive has no options");
		}
		Set<String> uniqueOptions;
		for (const String& option : directive.options)
		{
			if (!option.empty())
			{
				if (!IsIdentifierStart(option.front()) ||
				    !std::all_of(option.begin() + 1, option.end(), IsIdentifierPart))
				{
					return Result<Vector<ShaderKeywordVariant>>::Failure("Invalid Shader keyword option " + option);
				}
				if (!declaredKeywords.insert(option).second)
				{
					return Result<Vector<ShaderKeywordVariant>>::Failure(
					    "Shader keyword is declared in more than one option: " + option);
				}
			}
			if (!uniqueOptions.insert(option).second)
			{
				return Result<Vector<ShaderKeywordVariant>>::Failure(
				    "Shader keyword directive contains duplicate options");
			}
		}
		const Ursa::UInt64 optionCount = directive.options.size();
		if (potentialVariantCount > maxVariantCount / optionCount)
		{
			return Result<Vector<ShaderKeywordVariant>>::Failure(
			    "Shader keyword combinations exceed the configured variant limit");
		}
		potentialVariantCount *= optionCount;
		if (directive.kind == ShaderKeywordDirectiveKind::MultiCompile)
		{
			multiCompile.push_back(directive);
		}
		else
		{
			shaderFeatures.push_back(directive);
		}
	}
	for (const Vector<String>& usedSet : usedKeywordSets)
	{
		for (const String& keyword : usedSet)
		{
			if (declaredKeywords.find(keyword) == declaredKeywords.end())
			{
				return Result<Vector<ShaderKeywordVariant>>::Failure("Material uses undeclared Shader keyword " +
				                                                     keyword);
			}
		}
	}
	const auto expand = [](const Vector<ShaderKeywordDirective>& axes)
	{
		Vector<Vector<String>> combinations(1);
		for (const ShaderKeywordDirective& axis : axes)
		{
			Vector<Vector<String>> next;
			for (const Vector<String>& combination : combinations)
			{
				for (const String& option : axis.options)
				{
					Vector<String> expanded = combination;
					if (!option.empty())
					{
						expanded.push_back(option);
					}
					next.push_back(std::move(expanded));
				}
			}
			combinations = std::move(next);
		}
		return combinations;
	};
	const auto appendVariant = [](Vector<String> keywords)
	{
		std::sort(keywords.begin(), keywords.end());
		keywords.erase(std::unique(keywords.begin(), keywords.end()), keywords.end());
		return keywords;
	};
	Vector<Vector<String>> requiredCombinations = expand(multiCompile);
	Vector<ShaderKeywordDirective> defaultFeatures;
	for (const ShaderKeywordDirective& feature : shaderFeatures)
	{
		ShaderKeywordDirective defaultAxis = feature;
		defaultAxis.options = {String{}};
		defaultFeatures.push_back(std::move(defaultAxis));
	}
	const Vector<Vector<String>> defaultFeatureCombinations = expand(defaultFeatures);
	Vector<ShaderKeywordVariant> result;
	Map<String, Ursa::Size> resultIndices;
	const auto appendCombination = [&](const Vector<String>& multi, const Vector<String>& feature, bool required)
	{
		Vector<String> keywords = multi;
		keywords.insert(keywords.end(), feature.begin(), feature.end());
		keywords = appendVariant(std::move(keywords));
		String key;
		for (const String& keyword : keywords)
		{
			key += keyword;
			key += ';';
		}
		auto found = resultIndices.find(key);
		if (found != resultIndices.end())
		{
			result[found->second].required = result[found->second].required || required;
			return true;
		}
		if (result.size() >= maxVariantCount)
		{
			return false;
		}
		const auto inserted = resultIndices.emplace(std::move(key), result.size());
		if (!inserted.second)
		{
			return true;
		}
		result.push_back({std::move(keywords), required});
		return true;
	};
	for (const Vector<String>& multi : requiredCombinations)
	{
		for (const Vector<String>& feature : defaultFeatureCombinations)
		{
			if (!appendCombination(multi, feature, true))
			{
				return Result<Vector<ShaderKeywordVariant>>::Failure(
				    "Shader variants exceed the configured variant limit");
			}
		}
	}
	for (const Vector<String>& usedSet : usedKeywordSets)
	{
		Vector<String> selectedFeatureKeywords;
		for (const ShaderKeywordDirective& feature : shaderFeatures)
		{
			String selected;
			for (const String& option : feature.options)
			{
				if (!option.empty() && std::find(usedSet.begin(), usedSet.end(), option) != usedSet.end())
				{
					if (!selected.empty())
					{
						return Result<Vector<ShaderKeywordVariant>>::Failure(
						    "Material enables mutually exclusive Shader feature keywords");
					}
					selected = option;
				}
			}
			if (!selected.empty())
			{
				selectedFeatureKeywords.push_back(std::move(selected));
			}
		}
		if (selectedFeatureKeywords.empty())
		{
			continue;
		}
		for (const Vector<String>& multi : requiredCombinations)
		{
			if (!appendCombination(multi, selectedFeatureKeywords, false))
			{
				return Result<Vector<ShaderKeywordVariant>>::Failure(
				    "Shader variants exceed the configured variant limit");
			}
		}
	}
	return Result<Vector<ShaderKeywordVariant>>::Success(std::move(result));
}
} // namespace Ursa::Shader
