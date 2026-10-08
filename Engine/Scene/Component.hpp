#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Window/Window.hpp>
#include <Serialization/ResourceId.hpp>
#include <Serialization/Serializable.hpp>
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
	static constexpr Ursa::Size keyCount = static_cast<Ursa::Size>(Key::Digit9) + 1;
	Array<bool, keyCount> keys{};

	bool KeyDown(Key key) const
	{
		const Ursa::Size index = static_cast<Ursa::Size>(key);
		return index < keys.size() && keys[index];
	}

	bool KeyPressed(Key key) const
	{
		const Ursa::Size index = static_cast<Ursa::Size>(key);
		return index < keyPressed.size() && keyPressed[index];
	}

	bool KeyReleased(Key key) const
	{
		const Ursa::Size index = static_cast<Ursa::Size>(key);
		return index < keyReleased.size() && keyReleased[index];
	}

	static FrameInput Capture(const Window& window)
	{
		FrameInput input;
		for (Ursa::Size i = 0; i < input.keys.size(); ++i)
		{
			if (static_cast<Key>(i) != Key::Unknown)
			{
				input.keys[i] = window.KeyDown(static_cast<Key>(i));
			}
		}
		return input;
	}

	Array<bool, keyCount> keyPressed{};
	Array<bool, keyCount> keyReleased{};
};

struct FrameContext
{
	Ursa::Float32 deltaSeconds = 0.0f;
	Ursa::Float32 fixedDeltaSeconds = 0.0f;
	Ursa::Float64 elapsedSeconds = 0.0;
	Ursa::Float64 fixedElapsedSeconds = 0.0;
	Ursa::UInt64 frameIndex = 0;
	Window* window = nullptr;
	FrameInput input;
};

class Component : public Serializable
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

	// Internal attachment binding completes before gameplay callbacks can see this component.
	virtual Result<void> PrepareRuntimeBinding()
	{
		return {};
	}

	virtual Result<void> Awake(const FrameContext&)
	{
		return {};
	}

	virtual void OnEnable()
	{
	}

	virtual Result<void> Start(const FrameContext&)
	{
		return {};
	}

	virtual Result<void> FixedUpdate(const FrameContext&)
	{
		return {};
	}

	virtual Result<void> Update(const FrameContext&)
	{
		return {};
	}

	virtual Result<void> LateUpdate(const FrameContext&)
	{
		return {};
	}

	virtual void OnDisable()
	{
	}

	virtual void OnDestroy()
	{
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
