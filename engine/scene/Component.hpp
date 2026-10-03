#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <window/Window.hpp>
#include <array>
#include <cstdint>
#include <limits>

namespace Ursa
{
class Scene;

struct NodeId;

struct ComponentId
{
	uint32_t index = std::numeric_limits<uint32_t>::max();
	uint32_t generation = 0;

	explicit operator bool() const
	{
		return index != std::numeric_limits<uint32_t>::max();
	}
};

inline bool operator==(ComponentId a, ComponentId b)
{
	return a.index == b.index && a.generation == b.generation;
}

inline bool operator!=(ComponentId a, ComponentId b)
{
	return !(a == b);
}

struct FrameInput
{
	Array<bool, 10> keys{};

	bool KeyDown(Key key) const
	{
		return keys.at(static_cast<size_t>(key));
	}

	static FrameInput Capture(const Window& window)
	{
		FrameInput input;
		for (size_t i = 0; i < input.keys.size(); ++i)
		{
			input.keys[i] = window.KeyDown(static_cast<Key>(i));
		}
		return input;
	}
};

struct FrameContext
{
	float deltaSeconds = 0.0f;
	float fixedDeltaSeconds = 0.0f;
	double elapsedSeconds = 0.0;
	uint64_t frameIndex = 0;
	Window* window = nullptr;
	FrameInput input;
};

class Component
{
public:
	virtual ~Component() = default;

	Scene& GetScene() const
	{
		return *scene;
	}

	NodeId Node() const;

	ComponentId Id() const
	{
		return id;
	}

	virtual Result<void> OnStart(const FrameContext&)
	{
		return {};
	}

	virtual Result<void> OnUpdate(const FrameContext&)
	{
		return {};
	}

	virtual Result<void> OnFixedUpdate(const FrameContext&)
	{
		return {};
	}

	virtual Result<void> OnLateUpdate(const FrameContext&)
	{
		return {};
	}

	virtual void OnRemove()
	{
	}

private:
	friend class Scene;
	Scene* scene = nullptr;
	uint32_t nodeIndex = 0;
	uint32_t nodeGeneration = 0;
	ComponentId id;
};
} // namespace Ursa
