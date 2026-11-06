using System;
using Ursa.SceneManagement;

namespace Ursa.SessionFixtures;

public sealed class SceneSwitchProbe : Behaviour
{
	private static int entries;
	private static GameObject? first;
	private static string trace = string.Empty;
	public int destination = 1;
	public Texture? image;
	public bool failCleanup;
	public int visits;
	public int updates;
	public bool staleRejected;
	public string lifecycle = string.Empty;
	public override void Awake()
	{
		visits = ++entries;
		trace += "A";
		if (first == null)
		{
			first = GameObject;
			SceneManager.SceneLoaded += (_, _) => trace += "L";
			SceneManager.SceneUnloaded += _ => trace += "U";
		}
		else
		{
			try { _ = first.Name; }
			catch (InvalidOperationException) { staleRejected = true; }
		}
	}
	public override void OnEnable() { trace += "E"; }
	public override void Start() { trace += "S"; lifecycle = trace; }
	public override void OnDisable() { trace += "D"; if (failCleanup) throw new InvalidOperationException("Expected scene cleanup failure."); }
	public override void OnDestroy() { trace += "X"; if (failCleanup) throw new InvalidOperationException("Expected scene cleanup failure."); }
	public override void Update() { updates++; }
	public override void OnGUI()
	{
		if (GUI.Button(new Rect(8, 8, 80, 24), "Navigate"))
		{
			SceneManager.LoadScene(destination);
			SceneManager.LoadScene(destination); // Coalesces without another activation.
			GUIUtility.ExitGUI();
		}
	}
}
