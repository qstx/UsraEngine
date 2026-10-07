#include <Serialization/ComponentRegistry.hpp>
#include <limits>

namespace
{
struct CounterComponent final : Ursa::Component
{
	CounterComponent(int& total, int step) : total(total), step(step)
	{
	}

	Ursa::String SerializationType() const override
	{
		return "game.Counter";
	}

	Ursa::Result<void> Serialize(Ursa::ArchiveWriter& writer) const override
	{
		return writer.Write("step", Ursa::Int64(step));
	}

	Ursa::Result<void> Deserialize(const Ursa::ArchiveReader& reader, Ursa::SerializationContext&) override
	{
		auto value = reader.Integer("step");
		if (!value || value.value < (std::numeric_limits<int>::min)() ||
		    value.value > (std::numeric_limits<int>::max)())
		{
			return Ursa::Result<void>::Failure("Invalid Counter step");
		}
		step = int(value.value);
		return {};
	}

	int& total;
	int step;
};

Ursa::Result<void> RegisterCounter(Ursa::ComponentRegistry& registry)
{
	return registry.RegisterSerializable<CounterComponent>(
	    "game.Counter", 1,
	    [](const Ursa::GameObject::Ptr& owner, Ursa::ComponentLoadContext& context) -> Ursa::Result<CounterComponent*>
	    {
		    auto total = context.Service<int>();
		    if (!total)
		    {
			    return Ursa::Result<CounterComponent*>::Failure(total.error);
		    }
		    return owner->AddComponent<CounterComponent>(*total.value, 0);
	    });
}
} // namespace

int main()
{
	Ursa::ComponentRegistry registry;
	return RegisterCounter(registry) ? 0 : 1;
}
