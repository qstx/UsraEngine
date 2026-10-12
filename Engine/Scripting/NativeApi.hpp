#pragma once

#include <Scripting/ManagedHandleRegistry.hpp>
#include <cstddef>

namespace Ursa::Scripting
{
#if defined(_WIN32)
using ManagedLogFunction = void(__cdecl*)(void*, UInt32, const UInt8*, UInt64);
using ManagedCreateGameObjectFunction = Int32(__cdecl*)(void*, const UInt8*, UInt64, ManagedHandle*);
using ManagedGetGameObjectNameFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt8*, UInt64, UInt64*);
using ManagedSetGameObjectNameFunction = Int32(__cdecl*)(void*, ManagedHandle, const UInt8*, UInt64);
using ManagedGetGameObjectActiveFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt32*);
using ManagedSetGameObjectActiveFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt32);
using ManagedGetGameObjectParentFunction = Int32(__cdecl*)(void*, ManagedHandle, ManagedHandle*);
using ManagedSetGameObjectParentFunction = Int32(__cdecl*)(void*, ManagedHandle, ManagedHandle);
using ManagedGetLocalTransformFunction = Int32(__cdecl*)(void*, ManagedHandle, Float32*, UInt64);
using ManagedSetLocalTransformFunction = Int32(__cdecl*)(void*, ManagedHandle, const Float32*, UInt64);
using ManagedGetInputStateFunction = Int32(__cdecl*)(void*, UInt32, UInt32, UInt32*);
using ManagedGetTimeStateFunction = Int32(__cdecl*)(void*, Float32*, Float32*, Float64*, Float64*, UInt64*);
using ManagedWriteLogFunction = Int32(__cdecl*)(void*, UInt32, const UInt8*, UInt64);
using ManagedReadFileFunction = Int32(__cdecl*)(void*, const UInt8*, UInt64, UInt8*, UInt64, UInt64*);
using ManagedWriteFileFunction = Int32(__cdecl*)(void*, const UInt8*, UInt64, const UInt8*, UInt64);
using ManagedAddComponentFunction = Int32(__cdecl*)(void*, ManagedHandle, const UInt8*, UInt64, ManagedHandle*);
using ManagedGetComponentCountFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt32*);
using ManagedGetComponentAtFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt32, ManagedHandle*);
using ManagedGetComponentTypeFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt8*, UInt64, UInt64*);
using ManagedGetComponentOwnerFunction = Int32(__cdecl*)(void*, ManagedHandle, ManagedHandle*);
using ManagedGetComponentEnabledFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt32*);
using ManagedSetComponentEnabledFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt32);
using ManagedIsComponentActiveFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt32*);
using ManagedRemoveComponentFunction = Int32(__cdecl*)(void*, ManagedHandle);
using ManagedLoadAssetFunction = Int32(__cdecl*)(void*, const UInt8*, UInt64, UInt64, ManagedHandle*);
using ManagedAddBehaviourFunction = Int32(__cdecl*)(void*, ManagedHandle, const UInt8*, UInt64, UInt64, ManagedHandle*);
using ManagedGetGameObjectPersistentIdFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt8*, UInt64, UInt64*);
using ManagedResolvePersistentIdFunction = Int32(__cdecl*)(void*, const UInt8*, UInt64, ManagedHandle*);
using ManagedGetComponentPersistentIdFunction = Int32(__cdecl*)(void*, ManagedHandle, UInt8*, UInt64, UInt64*);
using ManagedSetWindowTitleFunction = Int32(__cdecl*)(void*, const UInt8*, UInt64);
using ManagedGetComponentValuesFunction = Int32(__cdecl*)(void*, ManagedHandle, Float32*, UInt64);
using ManagedSetComponentValuesFunction = Int32(__cdecl*)(void*, ManagedHandle, const Float32*, UInt64);
#else
using ManagedLogFunction = void (*)(void*, UInt32, const UInt8*, UInt64);
using ManagedCreateGameObjectFunction = Int32 (*)(void*, const UInt8*, UInt64, ManagedHandle*);
using ManagedGetGameObjectNameFunction = Int32 (*)(void*, ManagedHandle, UInt8*, UInt64, UInt64*);
using ManagedSetGameObjectNameFunction = Int32 (*)(void*, ManagedHandle, const UInt8*, UInt64);
using ManagedGetGameObjectActiveFunction = Int32 (*)(void*, ManagedHandle, UInt32*);
using ManagedSetGameObjectActiveFunction = Int32 (*)(void*, ManagedHandle, UInt32);
using ManagedGetGameObjectParentFunction = Int32 (*)(void*, ManagedHandle, ManagedHandle*);
using ManagedSetGameObjectParentFunction = Int32 (*)(void*, ManagedHandle, ManagedHandle);
using ManagedGetLocalTransformFunction = Int32 (*)(void*, ManagedHandle, Float32*, UInt64);
using ManagedSetLocalTransformFunction = Int32 (*)(void*, ManagedHandle, const Float32*, UInt64);
using ManagedGetInputStateFunction = Int32 (*)(void*, UInt32, UInt32, UInt32*);
using ManagedGetTimeStateFunction = Int32 (*)(void*, Float32*, Float32*, Float64*, Float64*, UInt64*);
using ManagedWriteLogFunction = Int32 (*)(void*, UInt32, const UInt8*, UInt64);
using ManagedReadFileFunction = Int32 (*)(void*, const UInt8*, UInt64, UInt8*, UInt64, UInt64*);
using ManagedWriteFileFunction = Int32 (*)(void*, const UInt8*, UInt64, const UInt8*, UInt64);
using ManagedAddComponentFunction = Int32 (*)(void*, ManagedHandle, const UInt8*, UInt64, ManagedHandle*);
using ManagedGetComponentCountFunction = Int32 (*)(void*, ManagedHandle, UInt32*);
using ManagedGetComponentAtFunction = Int32 (*)(void*, ManagedHandle, UInt32, ManagedHandle*);
using ManagedGetComponentTypeFunction = Int32 (*)(void*, ManagedHandle, UInt8*, UInt64, UInt64*);
using ManagedGetComponentOwnerFunction = Int32 (*)(void*, ManagedHandle, ManagedHandle*);
using ManagedGetComponentEnabledFunction = Int32 (*)(void*, ManagedHandle, UInt32*);
using ManagedSetComponentEnabledFunction = Int32 (*)(void*, ManagedHandle, UInt32);
using ManagedIsComponentActiveFunction = Int32 (*)(void*, ManagedHandle, UInt32*);
using ManagedRemoveComponentFunction = Int32 (*)(void*, ManagedHandle);
using ManagedLoadAssetFunction = Int32 (*)(void*, const UInt8*, UInt64, UInt64, ManagedHandle*);
using ManagedAddBehaviourFunction = Int32 (*)(void*, ManagedHandle, const UInt8*, UInt64, UInt64, ManagedHandle*);
using ManagedGetGameObjectPersistentIdFunction = Int32 (*)(void*, ManagedHandle, UInt8*, UInt64, UInt64*);
using ManagedResolvePersistentIdFunction = Int32 (*)(void*, const UInt8*, UInt64, ManagedHandle*);
using ManagedGetComponentPersistentIdFunction = Int32 (*)(void*, ManagedHandle, UInt8*, UInt64, UInt64*);
using ManagedSetWindowTitleFunction = Int32 (*)(void*, const UInt8*, UInt64);
using ManagedGetComponentValuesFunction = Int32 (*)(void*, ManagedHandle, Float32*, UInt64);
using ManagedSetComponentValuesFunction = Int32 (*)(void*, ManagedHandle, const Float32*, UInt64);
#endif

using ManagedResourceRequestFunction = Int32 (*)(void*, const UInt8*, UInt64, UInt8*, UInt64, UInt64*);

struct NativeApiV5
{
	UInt32 version = 5;
	UInt32 size = 0;
	void* context = nullptr;
	ManagedLogFunction logUtf8 = nullptr;
	void* sceneContext = nullptr;
	ManagedCreateGameObjectFunction createGameObject = nullptr;
	ManagedGetGameObjectNameFunction getGameObjectName = nullptr;
	ManagedSetGameObjectNameFunction setGameObjectName = nullptr;
	ManagedGetGameObjectActiveFunction getGameObjectActive = nullptr;
	ManagedSetGameObjectActiveFunction setGameObjectActive = nullptr;
	ManagedGetGameObjectParentFunction getGameObjectParent = nullptr;
	ManagedSetGameObjectParentFunction setGameObjectParent = nullptr;
	ManagedGetLocalTransformFunction getLocalTransform = nullptr;
	ManagedSetLocalTransformFunction setLocalTransform = nullptr;
	ManagedGetInputStateFunction getInputState = nullptr;
	ManagedGetTimeStateFunction getTimeState = nullptr;
	ManagedWriteLogFunction writeLog = nullptr;
	ManagedReadFileFunction readFile = nullptr;
	ManagedWriteFileFunction writeFile = nullptr;
	ManagedAddComponentFunction addComponent = nullptr;
	ManagedGetComponentCountFunction getComponentCount = nullptr;
	ManagedGetComponentAtFunction getComponentAt = nullptr;
	ManagedGetComponentTypeFunction getComponentType = nullptr;
	ManagedGetComponentOwnerFunction getComponentOwner = nullptr;
	ManagedGetComponentEnabledFunction getComponentEnabled = nullptr;
	ManagedSetComponentEnabledFunction setComponentEnabled = nullptr;
	ManagedIsComponentActiveFunction isComponentActive = nullptr;
	ManagedRemoveComponentFunction removeComponent = nullptr;
	ManagedLoadAssetFunction loadTexture = nullptr;
	ManagedLoadAssetFunction loadMaterial = nullptr;
	ManagedAddBehaviourFunction addBehaviour = nullptr;
	ManagedGetGameObjectPersistentIdFunction getGameObjectPersistentId = nullptr;
	ManagedResolvePersistentIdFunction resolveGameObjectPersistentId = nullptr;
	ManagedGetComponentPersistentIdFunction getComponentPersistentId = nullptr;
	ManagedResolvePersistentIdFunction resolveComponentPersistentId = nullptr;
	ManagedRemoveComponentFunction destroyGameObject = nullptr;
	ManagedGetLocalTransformFunction getWorldPosition = nullptr;
	ManagedSetLocalTransformFunction setWorldPosition = nullptr;
	ManagedGetComponentValuesFunction getCameraProjection = nullptr;
	ManagedSetComponentValuesFunction setCameraProjection = nullptr;
	ManagedSetWindowTitleFunction setWindowTitle = nullptr;
	ManagedResourceRequestFunction resourceRequest = nullptr;
};

static_assert(sizeof(void*) != 8 || sizeof(NativeApiV5) == 328, "NativeApiV5 layout changed; increment its version");
static_assert(offsetof(NativeApiV5, sceneContext) == 24, "NativeApiV5 scene context offset changed");
} // namespace Ursa::Scripting
