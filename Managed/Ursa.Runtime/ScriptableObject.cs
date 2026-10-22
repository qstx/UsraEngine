using System;

namespace Ursa;

public abstract class ScriptableObject : IDisposable
{
	private ulong instanceId;
	private ulong sessionId;
	private int ownerThread;
	private bool alive;
	private bool disposing;
	private bool enabled;
	private bool editorProxy;
	private AssetReference? assetReference;
	internal ulong ValidationRevision { get; private set; }

	public bool IsValid => alive && ownerThread == Environment.CurrentManagedThreadId && ScriptableObjectRegistry.IsCurrentSession(sessionId);

	public ulong InstanceId
	{
		get { RequireValid(); return instanceId; }
	}

	public bool IsPersistent
	{
		get { RequireValid(); return assetReference.HasValue; }
	}

	public AssetReference? AssetReference
	{
		get { RequireValid(); return assetReference; }
	}

	public static T CreateInstance<T>() where T : ScriptableObject => (T)CreateInstance(typeof(T));

	public static T Load<T>(AssetReference reference) where T : ScriptableObject =>
		(T)ScriptableObjectRegistry.LoadAsset(reference, typeof(T));

	public static ScriptableObject CreateInstance(Type type)
	{
		ScriptableObject instance = CreateForLoad(type);
		instance.editorProxy = false;
		try
		{
			instance.Activate();
			return instance;
		}
		catch
		{
			try { instance.Release(); } catch { /* Preserve the construction callback failure. */ }
			throw;
		}
	}

	internal static ScriptableObject CreateForLoad(Type type)
	{
		ArgumentNullException.ThrowIfNull(type);
		ScriptableObjectRegistry.RequireThread();
		if (Ursa.Bindings.Generated.RuntimeModule.Context == 0)
			throw new InvalidOperationException("ScriptableObject creation requires an active host binding.");
		if (!typeof(ScriptableObject).IsAssignableFrom(type) || type.IsAbstract || type.ContainsGenericParameters)
			throw new ArgumentException($"'{type.FullName}' is not a concrete ScriptableObject type.", nameof(type));
		ScriptableObject instance = Activator.CreateInstance(type, nonPublic: true) as ScriptableObject ??
			throw new InvalidOperationException($"Cannot instantiate ScriptableObject '{type.FullName}'.");
		(instance.instanceId, instance.sessionId) = ScriptableObjectRegistry.Register(instance);
		instance.ownerThread = Environment.CurrentManagedThreadId;
		instance.alive = true;
		instance.editorProxy = ScriptableObjectRegistry.SnapshotOnly;
		return instance;
	}

	internal void Activate()
	{
		RequireValid();
		if (editorProxy) return;
		if (enabled) return;
		enabled = true;
		OnEnable();
	}

	// Inspector field proxies are snapshots, not loaded game instances. Their lifetime
	// must never invoke user game callbacks or occupy the persistent asset cache.
	internal static ScriptableObject CreateEditorProxy(Type type, AssetReference reference)
	{
		ScriptableObject instance = CreateForLoad(type);
		instance.editorProxy = true;
		instance.BindAsset(reference, register: false);
		return instance;
	}

	internal void ReleaseEditorProxy()
	{
		if (!alive) return;
		alive = false;
		enabled = false;
		ScriptableObjectRegistry.Unregister(sessionId, instanceId);
	}

	public void Dispose()
	{
		ScriptableObjectRegistry.RequireThread();
		if (!alive || disposing) return;
		RequireValid();
		ImportExecution.RequireWritableAsset(this);
		Release();
		GC.SuppressFinalize(this);
	}

	protected virtual void OnEnable() { }

	protected virtual void OnDisable() { }

	protected virtual void OnDestroy() { }

	protected virtual void OnValidate() { }

	internal void RequireValid()
	{
		ScriptableObjectRegistry.RequireThread();
		if (!IsValid) throw new ObjectDisposedException(GetType().FullName, "ScriptableObject belongs to a closed or different session.");
	}

	internal void BindAsset(AssetReference reference, bool register = true)
	{
		RequireValid();
		if (assetReference.HasValue && !assetReference.Value.Equals(reference))
			throw new InvalidOperationException("A persistent ScriptableObject cannot change its asset identity.");
		if (string.IsNullOrEmpty(reference.Guid) || reference.LocalFileId == 0)
			throw new ArgumentException("A persistent asset needs a GUID and nonzero local file ID.");
		if (register) ScriptableObjectRegistry.BindAsset(this, reference);
		assetReference = reference;
	}

	internal void ValidateFields()
	{
		RequireValid();
		OnValidate();
		ValidationRevision = checked(ValidationRevision + 1);
	}

	internal void Release()
	{
		if (!alive || disposing) return;
		if (editorProxy) { ReleaseEditorProxy(); return; }
		disposing = true;
		Exception? disableError = null;
		Exception? destroyError = null;
		try
		{
			try { if (enabled) OnDisable(); } catch (Exception error) { disableError = error; }
			try { OnDestroy(); } catch (Exception error) { destroyError = error; }
		}
		finally
		{
			alive = false;
			enabled = false;
			disposing = false;
			ScriptableObjectRegistry.Unregister(sessionId, instanceId);
		}
		if (disableError != null && destroyError != null) throw new AggregateException(disableError, destroyError);
		if (disableError != null) throw disableError;
		if (destroyError != null) throw destroyError;
	}
}
