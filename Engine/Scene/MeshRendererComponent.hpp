#pragma once

#include <Scene/RendererComponent.hpp>

namespace Ursa
{
class MeshRendererComponent final : public RendererComponent
{
public:
	String SerializationType() const override;

	UInt32 SerializationVersion() const override;
};
} // namespace Ursa
