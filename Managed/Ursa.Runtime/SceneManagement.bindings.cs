using Ursa.Bindings;

namespace Ursa.SceneManagement;

[WireValue]
internal struct SceneData
{
	public ulong Session;
	public ulong Token;
	public string Guid;
	public ulong LocalFileId;
	public string Name;
	public string Path;
	public int BuildIndex;
}

[BindingModule("Runtime", Required = true)]
[NativeHeader("Scripting/Bindings/SceneServices.hpp")]
internal static partial class NativeSceneBindings
{
	[NativeMethod("Ursa::Scripting::Bindings::SceneServices::GetActiveScene")]
	internal static partial SceneData GetActiveScene();
	[NativeMethod("Ursa::Scripting::Bindings::SceneServices::IsSceneLoaded")]
	internal static partial bool IsSceneLoaded(ulong session, ulong token);
	[NativeMethod("Ursa::Scripting::Bindings::SceneServices::GetIsLoading")]
	internal static partial bool GetIsLoading();
	[NativeMethod("Ursa::Scripting::Bindings::SceneServices::GetLastLoadError")]
	internal static partial string GetLastLoadError();
	[NativeMethod("Ursa::Scripting::Bindings::SceneServices::LoadByName")]
	internal static partial void LoadByName(string nameOrPath, uint mode);
	[NativeMethod("Ursa::Scripting::Bindings::SceneServices::LoadByIndex")]
	internal static partial void LoadByIndex(int buildIndex, uint mode);
	[NativeMethod("Ursa::Scripting::Bindings::SceneServices::LoadByReference")]
	internal static partial void LoadByReference(string guid, ulong localFileId, uint mode);
}
