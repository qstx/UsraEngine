using System;
using Ursa;

namespace Ursa.SessionFixtures;

public sealed class GuiProbe : Behaviour
{
	public int callbacks;
	public int layouts;
	public int repaints;
	public int clicks;
	public int updates;
	public int staleRejected;
	public int repaintObservedUpdate;
	public int consumedW;
	public bool failInGui;
	public ImportProbeAsset? settings;
	public ImportProbeAsset?[] settingsList = Array.Empty<ImportProbeAsset?>();
	public string text = "中文";
	private Event? previous;

	public override void Update()
	{
		updates++;
		if (Input.GetKey(KeyCode.W) || Input.GetKeyDown(KeyCode.W))
			throw new InvalidOperationException("GUI-consumed W reached managed Update.");
	}

	public override void OnGUI()
	{
		if (failInGui)
		{
			GUI.BeginGroup(new Rect(0, 0, 100, 100));
			GUI.PushId(99);
			throw new InvalidOperationException("Expected GUI component failure inside nested scope.");
		}
		if (previous != null)
		{
			bool rejected = false;
			try { _ = previous.Type; }
			catch (InvalidOperationException) { rejected = true; }
			if (!rejected) throw new InvalidOperationException("Old Event wrapper was accepted.");
			staleRejected++;
		}
		Event e = Event.Current;
		previous = e;
		callbacks++;
		if (e.Type == EventType.Layout) layouts++;
		if (e.Type == EventType.Repaint) { repaints++; repaintObservedUpdate = updates; }
		if (e.Type == EventType.KeyDown && e.KeyCode == KeyCode.W) { consumedW++; e.Use(); }
		GUI.Label(new Rect(8, 40, 160, 24), "托管 OnGUI");
		if (GUI.Button(new Rect(8, 8, 80, 24), "Action"))
		{
			clicks++;
			GUIUtility.ExitGUI();
		}
		text = GUI.TextField(new Rect(8, 72, 160, 24), text);
	}
}

public sealed class GuiSwallowedExitProbe : Behaviour
{
	public int callbacks;
	public override void OnGUI()
	{
		callbacks++;
		try { GUIUtility.ExitGUI(); }
		catch (Exception) { }
		// Even a caller swallowing ExitGUI cannot resume the pass or disable this behaviour.
		GUI.Label(new Rect(0, 0, 20, 20), "Unreachable");
	}
}
