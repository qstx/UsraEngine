#include <Scripting/ManagedEditorBridge.hpp>
#include <Assets/AssetPreviewExtension.hpp>

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
		invocation.hostToken = host.Token();
		const Scripting::Bindings::BindingModuleReference capability{Scripting::Bindings::Editor::moduleId,
		                                                             &Scripting::Bindings::Editor::NativeDescriptor(),
		                                                             invocation.Token()};
		auto invoked = host.InvokeEditorExtension(sessionId, extensionId, capability);
		if (!invoked || !invoked.value)
		{
			return Result<bool>::Failure(invoked ? "Managed editor extension was disabled after an exception"
			                                     : invoked.error);
		}
		const auto synchronized = invocation.SynchronizeRuntimeTarget();
		if (!synchronized)
		{
			return Result<bool>::Failure(synchronized.error);
		}
		return Result<bool>::Success(invocation.changed);
	}
	catch (const std::exception& error)
	{
		return Result<bool>::Failure(ToUrsaString(error.what()));
	}
}

Result<Scripting::ManagedPreviewResult> InvokeManagedEditorPreview(Scripting::DotNetHost& host, UInt64 sessionId,
                                                                   const String& extensionId, UInt64 instance,
                                                                   Scripting::ManagedPreviewPhase phase,
                                                                   const ViewportRect& rect,
                                                                   ManagedEditorContext& context)
{
	try
	{
		Bindings::EditorInvocation invocation(context, sessionId);
		invocation.hostToken = host.Token();

		struct PreviewScope
		{
			AssetPreviewExtension* preview;

			~PreviewScope()
			{
				if (preview)
				{
					preview->SetInvocation(nullptr);
				}
			}
		} previewScope{context.preview};

		if (context.preview)
		{
			context.preview->SetInvocation(&invocation);
		}
		const Scripting::Bindings::BindingModuleReference capability{Scripting::Bindings::Editor::moduleId,
		                                                             &Scripting::Bindings::Editor::NativeDescriptor(),
		                                                             invocation.Token()};
		auto result = host.InvokeEditorPreview(sessionId, extensionId, instance, phase, rect.x, rect.y, rect.width,
		                                       rect.height, capability);
		if (result && phase == Scripting::ManagedPreviewPhase::Inspector)
		{
			const auto synchronized = invocation.SynchronizeRuntimeTarget();
			if (!synchronized)
			{
				return Result<Scripting::ManagedPreviewResult>::Failure(synchronized.error);
			}
		}
		if (context.preview && invocation.changed)
		{
			context.preview->RequestRepaint();
		}
		return result;
	}
	catch (const std::exception& error)
	{
		return Result<Scripting::ManagedPreviewResult>::Failure(ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Editor
#endif
