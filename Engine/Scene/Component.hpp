#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Window/Window.hpp>
#include <Serialization/ResourceId.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

namespace Ursa
{
class Scene;

class GameObject;

struct ComponentId
{
	Ursa::UInt32 index = std::numeric_limits<Ursa::UInt32>::max();
	Ursa::UInt32 generation = 0;

	explicit operator bool() const
	{
		return index != std::numeric_limits<Ursa::UInt32>::max();
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
		return keys.at(static_cast<Ursa::Size>(key));
	}

	static FrameInput Capture(const Window& window)
	{
		FrameInput input;
		for (Ursa::Size i = 0; i < input.keys.size(); ++i)
		{
			input.keys[i] = window.KeyDown(static_cast<Key>(i));
		}
		return input;
	}
};

struct FrameContext
{
	Ursa::Float32 deltaSeconds = 0.0f;
	Ursa::Float32 fixedDeltaSeconds = 0.0f;
	Ursa::Float64 elapsedSeconds = 0.0;
	Ursa::UInt64 frameIndex = 0;
	Window* window = nullptr;
	FrameInput input;
};

class Component
{
public:
	virtual ~Component() = default;

	Scene& GetScene() const
	{
		if (!scene)
		{
			throw std::logic_error("Component is not attached to a Scene");
		}
		return *scene;
	}

	Scene* TryGetScene() const
	{
		return scene;
	}

	std::shared_ptr<GameObject> GetGameObject() const;

	ComponentId Id() const
	{
		return id;
	}

	ResourceId PersistentId() const
	{
		return persistentId;
	}

	Result<void> SetPersistentId(ResourceId value)
	{
		if (scene || !value)
		{
			return Result<void>::Failure("A component persistent ID can only be assigned while detached");
		}
		persistentId = value;
		return {};
	}

	void SetEnabled(bool value);

	bool IsEnabled() const
	{
		return enabled;
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
	friend class GameObject;
	friend class PrefabAsset;
	Scene* scene = nullptr;
	GameObject* gameObject = nullptr;
	ComponentId id;
	ResourceId persistentId = ResourceId::Generate();
	bool enabled = true;
};
} // namespace Ursa
