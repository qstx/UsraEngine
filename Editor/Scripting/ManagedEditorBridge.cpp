#include <Scripting/ManagedEditorBridge.hpp>

#if defined(URSA_ENABLE_DOTNET)
#include <Scripting/Bindings/EditorServices.hpp>
#include <Editor.generated.hpp>

namespace Ursa::Editor
{
Result<bool> InvokeManagedEditorExtension(Scripting::DotNetHost& host, UInt64 sessionId, const String& extensionId,
                                          ManagedEditorContext& context)
{
	try
	{
		Bindings::EditorInvocation invocation(context, sessionId);
		const Scripting::Bindings::BindingModuleReference capability{Scripting::Bindings::Editor::moduleId,
		                                                             &Scripting::Bindings::Editor::NativeDescriptor(),
		                                                             invocation.Token()};
		auto invoked = host.InvokeEditorExtension(sessionId, extensionId, capability);
		if (!invoked || !invoked.value)
		{
			return Result<bool>::Failure(invoked ? "Managed editor extension was disabled after an exception"
			                                     : invoked.error);
		}
		return Result<bool>::Success(invocation.changed);
	}
	catch (const std::exception& error)
	{
		return Result<bool>::Failure(ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Editor
#endif
