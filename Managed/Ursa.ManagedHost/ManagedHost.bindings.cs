using Ursa.Bindings;

namespace Ursa.ManagedHost;

[BindingModule("ManagedHost", Required = true, Dependencies = new[] { "Runtime", "Rendering" })]
internal static partial class ManagedHostBindings
{
	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.InitializeFramework")]
	internal static partial void InitializeFramework();

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.CreateBehaviour")]
	internal static partial ulong CreateBehaviour(string typeName, Ursa.NativeHandle owner);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.BindBehaviour")]
	internal static partial void BindBehaviour(ulong token, Ursa.NativeHandle component);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.InvokeBehaviour")]
	internal static partial void InvokeBehaviour(ulong token, uint eventId);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.DestroyBehaviour")]
	internal static partial void DestroyBehaviour(ulong token);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.SerializeBehaviour")]
	internal static partial BehaviourFieldArchive SerializeBehaviour(ulong token);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.DeserializeBehaviour")]
	internal static partial void DeserializeBehaviour(ulong token, BehaviourFieldArchive archive);

	[ManagedCallback("Ursa.ManagedHost.ScriptSessions.LoadAssembly")]
	internal static partial void LoadScriptAssembly(byte[] image);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.CreateScriptSession")]
	internal static partial ulong CreateScriptSession(bool editor);

	[ManagedCallback("Ursa.ManagedHost.ScriptSessions.Select")]
	internal static partial void SelectScriptSession([BindingSession] ulong session);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.BindScriptSession")]
	internal static partial void BindScriptSession([BindingSession] ulong session);

	[ManagedCallback("Ursa.ManagedHost.ScriptSessions.Validate")]
	internal static partial void ValidateScriptSession([BindingSession] ulong session);

	[ManagedCallback("Ursa.ManagedHost.ScriptSessions.Close")]
	internal static partial bool CloseScriptSession([BindingSession] ulong session);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.LoadScriptContent")]
	internal static partial void LoadScriptContent([BindingSession] ulong session, ScriptContentArchive archive);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.LoadEditorApi")]
	internal static partial void LoadEditorApi(byte[] image);

	[ManagedCallback("Ursa.ManagedHost.ScriptSessions.TransferAssets")]
	internal static partial void TransferScriptableAssets([BindingSession] ulong source, ulong candidate);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.ConfigurePipeline")]
	internal static partial void ConfigureRenderPipeline([BindingSession] ulong session, PipelineAssetReference? asset);

	[ManagedCallback("Ursa.ManagedHost.ScriptSessions.RenderScene")]
	internal static partial void RenderScene([BindingSession] ulong session, float aspect);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.PrepareReload")]
	internal static partial ScriptReloadArchive PrepareScriptReload([BindingSession] ulong session, ScriptReloadArchive archive);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.PrepareEditorExtensions")]
	internal static partial EditorExtensionInfo[] PrepareEditorExtensions([BindingSession] ulong session);

	[ManagedCallback("Ursa.ManagedHost.HostCallbacks.InvokeEditorExtension")]
	internal static partial bool InvokeEditorExtension([BindingSession] ulong session, string extension, EditorBindingCapability capability);
}

[SerializedArchive]
internal struct BehaviourFieldArchive
{
	public uint Version;
	public string Json;
}

[SerializedArchive]
internal struct ScriptContentArchive
{
	public uint Version;
	public string Json;
}

[SerializedArchive]
internal struct ScriptReloadArchive
{
	public uint Version;
	public string Json;
}

[WireValue]
internal struct PipelineAssetReference
{
	public string Guid;
	public ulong LocalFileId;
}
