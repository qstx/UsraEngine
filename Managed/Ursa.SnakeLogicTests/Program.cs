using System;
using Ursa.Demo;

internal static class Program
{
	private static void Expect(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException(message);
		}
	}

	private static int Main()
	{
		var clock = new SnakeFixedStepClock();
		Expect(clock.Advance(0.1) == 0 && clock.Advance(0.08) == 1,
			   "Fixed-step clock did not accumulate elapsed time.");
		clock.Reset();
		Expect(clock.Advance(1.0) == 1, "Fixed-step clock did not clamp a long frame.");

		var growth = new SnakeGame(5, 4, 7,
			new[] { new SnakeCell(1, 1), new SnakeCell(0, 1) }, SnakeDirection.Right, new SnakeCell(2, 1));
		growth.QueueDirection(SnakeDirection.Left);
		growth.Step();
		Expect(growth.Body.Count == 3 && growth.Body[0] == new SnakeCell(2, 1) && growth.Score == 1,
			   "Snake accepted a reverse turn or failed to grow after eating.");
		growth.Step();
		Expect(growth.Body.Count == 3 && growth.Body[0] == new SnakeCell(3, 1),
			   "Snake body did not move after a successful growth step.");

		var seededA = new SnakeGame(8, 8, 1234);
		var seededB = new SnakeGame(8, 8, 1234);
		Expect(seededA.Food == seededB.Food, "Seeded food placement was not deterministic.");

		var wall = new SnakeGame(3, 3, 1,
			new[] { new SnakeCell(2, 1), new SnakeCell(1, 1) }, SnakeDirection.Right);
		wall.Step();
		Expect(wall.Status == SnakeStatus.Lost, "Snake did not detect a wall collision.");

		var selfCollision = new SnakeGame(4, 4, 1,
			new[]
			{
				new SnakeCell(1, 1), new SnakeCell(2, 1), new SnakeCell(2, 0),
				new SnakeCell(1, 0), new SnakeCell(0, 0)
			}, SnakeDirection.Up);
		selfCollision.Step();
		Expect(selfCollision.Status == SnakeStatus.Lost, "Snake did not detect a self collision.");

		var fullBoard = new SnakeGame(2, 2, 1,
			new[] { new SnakeCell(0, 0), new SnakeCell(0, 1), new SnakeCell(1, 1) },
			SnakeDirection.Right, new SnakeCell(1, 0));
		fullBoard.Step();
		Expect(fullBoard.Status == SnakeStatus.Won && !fullBoard.Food.HasValue && fullBoard.Body.Count == 4,
			   "Snake did not win when its body filled the board.");

		Console.WriteLine("Snake managed logic tests passed.");
		return 0;
	}
}
