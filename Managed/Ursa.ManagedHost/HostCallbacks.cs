using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Runtime.CompilerServices;
using Ursa;
using Ursa.Bindings;

namespace Ursa.ManagedHost;

internal static class HostCallbacks
{
	internal static void InitializeFramework()
	{
		NativeRuntimeBindings.ReportHostMessage(1, "Ursa 托管运行时已初始化");
		NativeRuntimeBindings.ReportHostMessage(2, string.Empty);
	}

	internal static EditorExtensionInfo[] PrepareEditorExtensions(ulong session)
	{
		ScriptSessions.PrepareExtensions(session);
		return ScriptSessions.DescribeExtensions(session);
	}

	internal static bool InvokeEditorExtension(ulong session, string extension, EditorBindingCapability capability)
	{
		ScriptSessions.Select(session);
		if (extension.Length == 0 || extension.Length > 4096 || capability.ModuleId == 0 || capability.DescriptorAddress == 0 || capability.ContextToken == 0)
			throw new ArgumentException("Invalid Editor callback capability.");
		using var scope = BindingRegistry.EnterCallScope(new BindingModuleReference
		{
			Id = capability.ModuleId, Descriptor = checked((nint)capability.DescriptorAddress), Context = checked((nint)capability.ContextToken)
		});
		return ScriptSessions.InvokeExtension(session, extension);
	}

	internal static ulong CreateBehaviour(string typeName, NativeHandle owner)
	{
		if (typeName.Length == 0 || typeName.Length > 4096 || owner.kind != 1 || owner.reserved != 0)
			throw new ArgumentException("Behaviour creation requires a type name and GameObject handle.");
		return BehaviourRegistry.Create(typeName, GameObject.FromHandle(owner));
	}

	internal static void BindBehaviour(ulong token, NativeHandle component) => BehaviourRegistry.BindNative(token, component);
	internal static void InvokeBehaviour(ulong token, uint eventId) => BehaviourRegistry.Invoke(token, eventId);
	internal static void DestroyBehaviour(ulong token) => BehaviourRegistry.Destroy(token);
	internal static BehaviourFieldArchive SerializeBehaviour(ulong token) => new() { Version = 1, Json = BehaviourRegistry.Serialize(token) };
	internal static void DeserializeBehaviour(ulong token, BehaviourFieldArchive archive)
	{
		if (archive.Version != 1 || archive.Json.Length == 0 || archive.Json.Length > 64 * 1024 * 1024)
			throw new ArgumentException("Unsupported Behaviour field archive version or size.");
		BehaviourRegistry.Deserialize(token, archive.Json);
	}

	internal static ulong CreateScriptSession(bool editor) => ScriptSessions.Create(editor);
	internal static void BindScriptSession(ulong session) => ScriptSessions.Bind(session);
	internal static void LoadScriptContent(ulong session, ScriptContentArchive archive)
	{
		if (archive.Version != 1 || archive.Json.Length == 0 || archive.Json.Length > 16 * 1024 * 1024)
			throw new ArgumentException("Unsupported script content archive version or size.");
		ScriptSessions.LoadContent(session, archive.Json);
	}

	internal static void LoadEditorApi(byte[] image)
	{
		ScriptSessions.RequireThread();
		if (ScriptSessions.sharedEditorAssembly != null) return;
		if (image.Length == 0 || image.Length > 256 * 1024 * 1024) throw new ArgumentException("Invalid shared Editor API image size.");
		using var stream = new MemoryStream(image, writable: false);
		AssemblyLoadContext context = AssemblyLoadContext.GetLoadContext(typeof(Behaviour).Assembly)!;
		Assembly assembly = context.LoadFromStream(stream);
		if (assembly.GetName().Name != "Ursa.Editor") throw new InvalidOperationException("Shared editor API identity is invalid.");
		RuntimeHelpers.RunModuleConstructor(assembly.ManifestModule.ModuleHandle);
		ScriptSessions.sharedEditorAssembly = assembly;
	}

	internal static void ConfigurePipeline(ulong session, PipelineAssetReference? asset) => ScriptSessions.ConfigurePipeline(session,
		asset.HasValue ? new AssetReference(asset.Value.Guid, asset.Value.LocalFileId) : null);

	internal static ScriptReloadArchive PrepareReload(ulong session, ScriptReloadArchive archive)
	{
		if (archive.Version != 1 || archive.Json.Length == 0 || archive.Json.Length > 64 * 1024 * 1024)
			throw new ArgumentException("Unsupported reload archive version or size.");
		ScriptSessions.PrepareReload(session, archive.Json);
		return new() { Version = 1, Json = ScriptSessions.PreparedReload(session) };
	}
}
