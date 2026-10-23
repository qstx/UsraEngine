#include <Core/Types.hpp>
#include <Vfs/VirtualPath.hpp>

namespace Ursa
{
namespace
{
bool SchemeStart(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

bool SchemeBody(char character)
{
	return SchemeStart(character) || (character >= '0' && character <= '9') || character == '_';
}
} // namespace

Result<VirtualPath> VirtualPath::Parse(const String& text)
{
	const auto colon = text.find(':');
	if (colon == String::npos)
	{
		return Result<VirtualPath>::Failure("Resource path needs a scheme, such as ursa:Shaders/Examples/Cube.vert: " +
		                                    text);
	}
	if (colon == 0)
	{
		return Result<VirtualPath>::Failure("Resource path has an empty scheme: " + text);
	}

	String parsedScheme = text.substr(0, colon);
	if (parsedScheme.size() == 1)
	{
		return Result<VirtualPath>::Failure("Resource path scheme looks like a host drive letter: " + text);
	}
	if (!SchemeStart(parsedScheme.front()))
	{
		return Result<VirtualPath>::Failure("Resource path scheme must start with a letter: " + text);
	}
	for (Ursa::Size index = 0; index < parsedScheme.size(); ++index)
	{
		const char character = parsedScheme[index];
		if (!SchemeBody(character))
		{
			return Result<VirtualPath>::Failure("Resource path scheme has an invalid character: " + text);
		}
		if (character >= 'A' && character <= 'Z')
		{
			parsedScheme[index] = static_cast<char>(character - 'A' + 'a');
		}
	}

	String rest = text.substr(colon + 1);
	for (char& character : rest)
	{
		if (character == '\\')
		{
			character = '/';
		}
	}
	if (rest.empty())
	{
		return Result<VirtualPath>::Failure("Resource path needs a relative path after the scheme: " + text);
	}
	if (rest.front() == '/')
	{
		return Result<VirtualPath>::Failure("Resource path must be relative to its scheme root: " + text);
	}

	String normalized;
	Ursa::Size cursor = 0;
	while (cursor < rest.size())
	{
		const auto slash = rest.find('/', cursor);
		const Ursa::Size end = slash == String::npos ? rest.size() : slash;
		const String segment = rest.substr(cursor, end - cursor);
		if (!segment.empty() && segment != ".")
		{
			if (segment == "..")
			{
				return Result<VirtualPath>::Failure("Resource path escapes its scheme root: " + text);
			}
			if (segment.find(':') != String::npos)
			{
				return Result<VirtualPath>::Failure("Resource path segment has an invalid character: " + text);
			}
			if (!normalized.empty())
			{
				normalized.push_back('/');
			}
			normalized += segment;
		}
		if (slash == String::npos)
		{
			break;
		}
		cursor = slash + 1;
	}
	if (normalized.empty())
	{
		return Result<VirtualPath>::Failure("Resource path needs a relative path after the scheme: " + text);
	}

	VirtualPath path;
	path.scheme = std::move(parsedScheme);
	path.relative = std::move(normalized);
	return Result<VirtualPath>::Success(std::move(path));
}

String VirtualPath::ToString() const
{
	return scheme + ":" + relative;
}

String VirtualPath::FileName() const
{
	const auto slash = relative.rfind('/');
	return slash == String::npos ? relative : relative.substr(slash + 1);
}

bool operator==(const VirtualPath& left, const VirtualPath& right)
{
	return left.scheme == right.scheme && left.relative == right.relative;
}

bool operator!=(const VirtualPath& left, const VirtualPath& right)
{
	return !(left == right);
}
} // namespace Ursa
