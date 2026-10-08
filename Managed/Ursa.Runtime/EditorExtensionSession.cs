using System;

namespace Ursa;

// Shared contract only. Project delegates and reflection data belong to one editing context.
internal interface IEditorExtensionSession : IDisposable
{
	string Describe();

	bool Invoke(string id, nint api, int size);
}
