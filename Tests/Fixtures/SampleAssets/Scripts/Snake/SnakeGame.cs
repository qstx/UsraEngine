using System;
using System.Collections.Generic;

namespace Ursa.Demo;

public enum SnakeDirection
{
	Up,
	Down,
	Left,
	Right
}

public enum SnakeStatus
{
	Running,
	Lost,
	Won
}

public readonly struct SnakeCell : IEquatable<SnakeCell>
{
	public SnakeCell(int x, int z)
	{
		X = x;
		Z = z;
	}

	public int X { get; }

	public int Z { get; }

	public bool Equals(SnakeCell other) => X == other.X && Z == other.Z;

	public override bool Equals(object? value) => value is SnakeCell other && Equals(other);

	public override int GetHashCode() => HashCode.Combine(X, Z);

	public static bool operator ==(SnakeCell left, SnakeCell right) => left.Equals(right);

	public static bool operator !=(SnakeCell left, SnakeCell right) => !left.Equals(right);
}

public sealed class SnakeGame
{
	private readonly int width;
	private readonly int height;
	private readonly uint seed;
	private readonly SnakeCell[] initialBody;
	private readonly SnakeDirection initialDirection;
	private readonly SnakeCell? initialFood;
	private readonly List<SnakeCell> body = new();
	private uint randomState;
	private SnakeDirection direction;
	private SnakeDirection? pendingDirection;
	private SnakeCell? food;
	private int score;

	public SnakeGame(int width = 16, int height = 16, uint seed = 1,
					 SnakeCell[]? initialBody = null, SnakeDirection initialDirection = SnakeDirection.Right,
					 SnakeCell? initialFood = null)
	{
		if (width < 2 || height < 2)
		{
			throw new ArgumentOutOfRangeException(nameof(width), "The board must be at least 2 by 2.");
		}
		this.width = width;
		this.height = height;
		this.seed = seed;
		this.initialDirection = initialDirection;
		this.initialBody = initialBody ?? new[]
		{
			new SnakeCell(width / 2, height / 2),
			new SnakeCell(width / 2 - 1, height / 2),
			new SnakeCell(width / 2 - 2, height / 2)
		};
		this.initialFood = initialFood;
		ValidateInitialState();
		Reset();
	}

	public int Width => width;

	public int Height => height;

	public IReadOnlyList<SnakeCell> Body => body;

	public SnakeCell? Food => food;

	public SnakeDirection Direction => direction;

	public SnakeStatus Status { get; private set; }

	public int Score => score;

	public void Reset()
	{
		body.Clear();
		body.AddRange(initialBody);
		randomState = seed == 0 ? 0x9e3779b9u : seed;
		direction = initialDirection;
		pendingDirection = null;
		score = 0;
		Status = SnakeStatus.Running;
		food = initialFood;
		if (!food.HasValue)
		{
			SpawnFood();
		}
	}

	public void QueueDirection(SnakeDirection requested)
	{
		if (Status != SnakeStatus.Running || pendingDirection.HasValue || IsOpposite(requested, direction))
		{
			return;
		}
		pendingDirection = requested;
	}

	public void Step()
	{
		if (Status != SnakeStatus.Running)
		{
			return;
		}
		if (pendingDirection.HasValue)
		{
			direction = pendingDirection.Value;
			pendingDirection = null;
		}

		SnakeCell next = Move(body[0], direction);
		if (next.X < 0 || next.X >= width || next.Z < 0 || next.Z >= height)
		{
			Status = SnakeStatus.Lost;
			return;
		}
		bool eating = food.HasValue && next == food.Value;
		int collisionCount = eating ? body.Count : body.Count - 1;
		for (int index = 0; index < collisionCount; ++index)
		{
			if (body[index] == next)
			{
				Status = SnakeStatus.Lost;
				return;
			}
		}

		body.Insert(0, next);
		if (eating)
		{
			++score;
			SpawnFood();
		}
		else
		{
			body.RemoveAt(body.Count - 1);
		}
	}

	private void SpawnFood()
	{
		int freeCount = width * height - body.Count;
		if (freeCount <= 0)
		{
			food = null;
			Status = SnakeStatus.Won;
			return;
		}

		int selected = (int)(NextRandom() % (uint)freeCount);
		for (int z = 0; z < height; ++z)
		{
			for (int x = 0; x < width; ++x)
			{
				var cell = new SnakeCell(x, z);
				if (body.Contains(cell))
				{
					continue;
				}
				if (selected-- == 0)
				{
					food = cell;
					return;
				}
			}
		}
	}

	private uint NextRandom()
	{
		uint value = randomState;
		value ^= value << 13;
		value ^= value >> 17;
		value ^= value << 5;
		randomState = value;
		return value;
	}

	private void ValidateInitialState()
	{
		if (initialBody.Length == 0 || initialBody.Length > width * height)
		{
			throw new ArgumentException("The initial snake must occupy at least one board cell.", nameof(initialBody));
		}
		var occupied = new HashSet<SnakeCell>();
		foreach (SnakeCell cell in initialBody)
		{
			if (!IsOnBoard(cell) || !occupied.Add(cell))
			{
				throw new ArgumentException("The initial snake is outside the board or overlaps itself.", nameof(initialBody));
			}
		}
		if (initialFood.HasValue && (!IsOnBoard(initialFood.Value) || occupied.Contains(initialFood.Value)))
		{
			throw new ArgumentException("Initial food must occupy a free board cell.", nameof(initialFood));
		}
	}

	private bool IsOnBoard(SnakeCell cell) => cell.X >= 0 && cell.X < width && cell.Z >= 0 && cell.Z < height;

	private static bool IsOpposite(SnakeDirection left, SnakeDirection right) =>
		(left == SnakeDirection.Up && right == SnakeDirection.Down) ||
		(left == SnakeDirection.Down && right == SnakeDirection.Up) ||
		(left == SnakeDirection.Left && right == SnakeDirection.Right) ||
		(left == SnakeDirection.Right && right == SnakeDirection.Left);

	private static SnakeCell Move(SnakeCell cell, SnakeDirection value) => value switch
	{
		SnakeDirection.Up => new SnakeCell(cell.X, cell.Z - 1),
		SnakeDirection.Down => new SnakeCell(cell.X, cell.Z + 1),
		SnakeDirection.Left => new SnakeCell(cell.X - 1, cell.Z),
		_ => new SnakeCell(cell.X + 1, cell.Z)
	};
}

public sealed class SnakeFixedStepClock
{
	private readonly double interval;
	private double accumulated;

	public SnakeFixedStepClock(double intervalSeconds = 0.18)
	{
		if (intervalSeconds <= 0.0)
		{
			throw new ArgumentOutOfRangeException(nameof(intervalSeconds));
		}
		interval = intervalSeconds;
	}

	public int Advance(double deltaSeconds)
	{
		if (deltaSeconds < 0.0)
		{
			throw new ArgumentOutOfRangeException(nameof(deltaSeconds));
		}
		accumulated += Math.Min(deltaSeconds, 0.25);
		int steps = 0;
		while (accumulated + 0.000000001 >= interval)
		{
			accumulated -= interval;
			++steps;
		}
		return steps;
	}

	public void Reset() => accumulated = 0.0;
}
