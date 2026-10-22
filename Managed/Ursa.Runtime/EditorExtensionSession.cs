using System;

namespace Ursa;

// Shared contract only. Project delegates and reflection data belong to one editing context.
internal interface IEditorExtensionSession : IDisposable
{
	EditorExtensionInfo[] Describe();

	bool Invoke(string id);
	EditorPreviewResult Preview(string id, ulong instance, EditorPreviewPhase phase, Rect rect);
}
