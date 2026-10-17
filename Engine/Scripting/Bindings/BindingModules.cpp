#include <Runtime.generated.hpp>
#include <Rendering.generated.hpp>
#include <ManagedHost.generated.hpp>

// Compile the supplied ABI in native-only builds as well as generated SDK builds.
static_assert(Ursa::Scripting::Bindings::Runtime::moduleId != Ursa::Scripting::Bindings::Rendering::moduleId);
