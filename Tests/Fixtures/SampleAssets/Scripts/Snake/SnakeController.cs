using System;

namespace Ursa.Demo;

public sealed class SnakeController : Behaviour
{
	public int width = 16;
	public int height = 16;
	public uint seed = 1;
	public bool smokeGameplay;

	public GameObject[] segmentObjects = Array.Empty<GameObject>();
	public GameObject? foodObject;
	private SnakeGame? game;
	private SnakeFixedStepClock clock = new();
	private bool paused;
	private ulong lastTitleFrame;
	private ulong smokeFrame;
	private int visibleSegmentCount;

	public override void Awake()
	{
		if (smokeGameplay)
		{
			width = 4;
			height = 4;
			game = new SnakeGame(width, height, seed,
				new[] { new SnakeCell(1, 1), new SnakeCell(0, 1) }, SnakeDirection.Right, new SnakeCell(2, 1));
		}
		else
		{
			game = new SnakeGame(width, height, seed);
		}
		if (segmentObjects.Length < width * height || foodObject == null)
		{
			throw new InvalidOperationException("Snake scene must contain a food object and one segment object per board cell.");
		}
		foreach (GameObject segment in segmentObjects)
		{
			segment.ActiveSelf = false;
		}
		SyncPresentation();
	}

	public override void FixedUpdate()
	{
		if (game == null || paused || smokeGameplay)
		{
			return;
		}
		int stepCount = clock.Advance(Time.FixedDeltaTime);
		for (int index = 0; index < stepCount; ++index)
		{
			game.Step();
		}
		SyncPresentation();
	}

	public override void Update()
	{
		if (game == null)
		{
			return;
		}
		if (smokeGameplay)
		{
			RunSmokeStep();
		}
		else
		{
			ReadDirectionKeys();
			if (Input.GetKeyDown(KeyCode.P))
			{
				paused = !paused;
			}
			if (Input.GetKeyDown(KeyCode.Space))
			{
				game.Reset();
				clock.Reset();
				paused = false;
			}
		}
		SyncPresentation();
		UpdateTitle();
	}

	private void ReadDirectionKeys()
	{
		if (Pressed(KeyCode.Up, KeyCode.W)) game!.QueueDirection(SnakeDirection.Up);
		else if (Pressed(KeyCode.Down, KeyCode.S)) game!.QueueDirection(SnakeDirection.Down);
		else if (Pressed(KeyCode.Left, KeyCode.A)) game!.QueueDirection(SnakeDirection.Left);
		else if (Pressed(KeyCode.Right, KeyCode.D)) game!.QueueDirection(SnakeDirection.Right);
	}

	private static bool Pressed(KeyCode primary, KeyCode secondary) =>
		Input.GetKeyDown(primary) || Input.GetKeyDown(secondary);

	private void SyncPresentation()
	{
		if (game == null)
		{
			return;
		}
		while (visibleSegmentCount < game.Body.Count)
		{
			segmentObjects[visibleSegmentCount++].ActiveSelf = true;
		}
		while (visibleSegmentCount > game.Body.Count)
		{
			segmentObjects[--visibleSegmentCount].ActiveSelf = false;
		}
		float xOffset = (game.Width - 1) * 0.5f;
		float zOffset = (game.Height - 1) * 0.5f;
		for (int index = 0; index < game.Body.Count; ++index)
		{
			SnakeCell cell = game.Body[index];
			GameObject segment = segmentObjects[index];
			segment.Transform.LocalPosition = new Vector3(cell.X - xOffset, 0.34f, cell.Z - zOffset);
		}
		foodObject!.ActiveSelf = game.Food.HasValue;
		if (game.Food.HasValue)
		{
			SnakeCell food = game.Food.Value;
			foodObject.Transform.LocalPosition = new Vector3(food.X - xOffset, 0.28f, food.Z - zOffset);
		}
	}

	private void UpdateTitle()
	{
		if (game == null || lastTitleFrame == Time.FrameCount)
		{
			return;
		}
		lastTitleFrame = Time.FrameCount;
		string title = $"Ursa Snake | Score: {game.Score}";
		if (paused) title += " | Paused";
		else if (game.Status == SnakeStatus.Lost) title += " | Game Over";
		else if (game.Status == SnakeStatus.Won) title += " | You Win";
		title += " | Arrows/WASD: turn | P: pause | Space: restart";
		Application.SetWindowTitle(title);
	}

	private void RunSmokeStep()
	{
		ulong frame = ++smokeFrame;
		if (frame == 1)
		{
			game!.Step();
			if (frame == 1 && (game.Score != 1 || game.Body.Count != 3))
			{
				throw new InvalidOperationException("Snake smoke run did not eat and grow on the first step.");
			}
		}
		else if (frame == 2)
		{
			paused = true;
			if (game!.Body[0] != new SnakeCell(2, 1) || game.Score != 1)
			{
				throw new InvalidOperationException("Snake smoke run did not remain paused.");
			}
		}
		else if (frame == 3)
		{
			paused = false;
			game!.Step();
		}
		else if (frame == 4)
		{
			game!.Step();
			if (game.Status != SnakeStatus.Lost)
			{
				throw new InvalidOperationException("Snake smoke run did not detect the board collision.");
			}
		}
		else if (frame == 5)
		{
			game!.Reset();
			if (game.Status != SnakeStatus.Running || game.Score != 0)
			{
				throw new InvalidOperationException("Snake smoke run did not restart the game.");
			}
		}
	}
}
