#pragma once

#include <Scripting/ScriptProjectModel.hpp>

namespace Ursa::Editor
{
struct ScriptDiagnostic
{
	VirtualPath source;
	UInt32 line = 0;
	UInt32 column = 0;
	String severity;
	String code;
	String message;
	String stage;
};

Result<Vector<ScriptDiagnostic>> ReadScriptDiagnostics(VirtualFileSystem& filesystem, const VirtualPath& path,
                                                       const Vector<ScriptProjectSource>& sources, const String& stage);
} // namespace Ursa::Editor
