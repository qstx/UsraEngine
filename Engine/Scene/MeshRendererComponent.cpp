#include <Scene/MeshRendererComponent.hpp>

namespace Ursa
{
String MeshRendererComponent::SerializationType() const
{
	return "ursa.MeshRenderer";
}

UInt32 MeshRendererComponent::SerializationVersion() const
{
	return 2;
}
} // namespace Ursa
