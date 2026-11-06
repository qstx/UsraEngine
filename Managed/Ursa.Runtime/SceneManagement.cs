using System;
using System.Collections.Generic;

namespace Ursa.SceneManagement;

public enum LoadSceneMode : uint { Single, Additive }

public readonly struct Scene : IEquatable<Scene>
{
	private readonly SceneData data;
	internal Scene(SceneData value) { data = value; }
	public string Name => data.Name ?? string.Empty;
	public string Path => data.Path ?? string.Empty;
	public int BuildIndex => data.Token == 0 ? -1 : data.BuildIndex;
	public AssetReference AssetReference => data.Token == 0 ? default : new(data.Guid, data.LocalFileId);
	public bool IsLoaded => data.Token != 0 && NativeSceneBindings.IsSceneLoaded(data.Session, data.Token);
	public bool Equals(Scene other) => data.Session == other.data.Session && data.Token == other.data.Token;
	public override bool Equals(object? other) => other is Scene scene && Equals(scene);
	public override int GetHashCode() => HashCode.Combine(data.Session, data.Token);
	public static bool operator ==(Scene left, Scene right) => left.Equals(right);
	public static bool operator !=(Scene left, Scene right) => !left.Equals(right);
}

public static class SceneManager
{
	private sealed class Notifications
	{
		internal Action<Scene, LoadSceneMode>? loaded;
		internal Action<Scene>? unloaded;
	}
	[ThreadStatic] private static Dictionary<ulong, Notifications>? ownerNotifications;
	private static Dictionary<ulong, Notifications> notifications => ownerNotifications ??= new();
	private static Notifications Current
	{
		get
		{
			_ = NativeSceneBindings.GetActiveScene();
			ulong session = BehaviourRegistry.currentSession;
			if (!notifications.TryGetValue(session, out Notifications? value)) notifications.Add(session, value = new());
			return value;
		}
	}
	public static event Action<Scene, LoadSceneMode> SceneLoaded { add => Current.loaded += value; remove => Current.loaded -= value; }
	public static event Action<Scene> SceneUnloaded { add => Current.unloaded += value; remove => Current.unloaded -= value; }
	public static Scene GetActiveScene() => new(NativeSceneBindings.GetActiveScene());
	public static bool IsLoading => NativeSceneBindings.GetIsLoading();
	public static string LastLoadError => NativeSceneBindings.GetLastLoadError();
	public static void LoadScene(string nameOrPath, LoadSceneMode mode = LoadSceneMode.Single) => NativeSceneBindings.LoadByName(nameOrPath, (uint)mode);
	public static void LoadScene(int buildIndex, LoadSceneMode mode = LoadSceneMode.Single) => NativeSceneBindings.LoadByIndex(buildIndex, (uint)mode);
	public static void LoadScene(AssetReference scene, LoadSceneMode mode = LoadSceneMode.Single) => NativeSceneBindings.LoadByReference(scene.Guid, scene.LocalFileId, (uint)mode);

	internal static void Notify(SceneData data, bool loaded)
	{
		if (!notifications.TryGetValue(BehaviourRegistry.currentSession, out Notifications? state)) return;
		var scene = new Scene(data);
		Delegate[] callbacks = loaded ? state.loaded?.GetInvocationList() ?? Array.Empty<Delegate>() : state.unloaded?.GetInvocationList() ?? Array.Empty<Delegate>();
		foreach (Delegate callback in callbacks)
		{
			try
			{
				if (loaded) ((Action<Scene, LoadSceneMode>)callback)(scene, LoadSceneMode.Single);
				else ((Action<Scene>)callback)(scene);
			}
			catch (Exception error) { Debug.LogError($"Scene {(loaded ? "loaded" : "unloaded")} callback failed: {error}"); }
		}
	}

	internal static void ReleaseSession(ulong session) => notifications.Remove(session);
}
