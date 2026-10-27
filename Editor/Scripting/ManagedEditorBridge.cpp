#include <Scripting/ManagedEditorBridge.hpp>
#include <Assets/AssetPreviewExtension.hpp>

#if defined(URSA_ENABLE_DOTNET)
#include <Scripting/Bindings/EditorServices.hpp>
#include <Editor.generated.hpp>
#include <imgui.h>
#include <imgui_internal.h>

namespace Ursa::Editor
{
String ManagedInspectorSelectionKey(const ManagedEditorContext& context)
{
	if (context.componentTargets.size() <= 1 && context.gameObjectTargets.size() <= 1)
	{
		return {};
	}
	String key = "managed-inspector-selection:";
	if (context.document)
	{
		key += context.document->Id().ToString();
	}
	key += ":components:" + ToUrsaString(std::to_string(context.componentPrimaryIndex));
	for (const auto* component : context.componentTargets)
	{
		key += ":";
		if (component)
		{
			key += component->PersistentId().ToString();
		}
	}
	key += ":objects:" + ToUrsaString(std::to_string(context.gameObjectPrimaryIndex));
	for (const auto& object : context.gameObjectTargets)
	{
		key += ":";
		if (object)
		{
			key += object->Id().ToString();
		}
	}
	return key;
}

namespace
{
struct InspectorSelectionIdScope
{
	bool pushed = false;

	InspectorSelectionIdScope(const String& extensionId, const ManagedEditorContext& context)
	{
		const String key = ManagedInspectorSelectionKey(context);
		if (extensionId.rfind("inspector", 0) == 0 && !key.empty() && ImGui::GetCurrentContext() &&
		    ImGui::GetCurrentContext()->CurrentWindow)
		{
			ImGui::PushID(key.c_str());
			pushed = true;
		}
	}

	~InspectorSelectionIdScope()
	{
		if (pushed)
		{
			ImGui::PopID();
		}
	}
};
} // namespace

Result<bool> InvokeManagedEditorExtension(Scripting::DotNetHost& host, UInt64 sessionId, const String& extensionId,
                                          ManagedEditorContext& context)
{
	try
	{
		Bindings::EditorInvocation invocation(context, sessionId);
		InspectorSelectionIdScope selectionId(extensionId, context);
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
