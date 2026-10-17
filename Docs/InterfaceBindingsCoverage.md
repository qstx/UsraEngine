# 接口绑定迁移覆盖矩阵

基线：2026-10-15，全量绑定改造前源码。此表是审阅记录，不是生成器输入；唯一人工维护的绑定契约是 C# `.bindings.cs`。行状态只有在生成声明、生产调用和验证全部落地后才改为完成。

## 边界与完整性

- NativeApiV6 的 39 个函数成员含日志、通用资源传输和命令批次入口，不能把传输入口本身当作其全部业务覆盖。
- ResourceRequest 的前缀判断仅用于校验/分组；下表列出实际操作。`binding.*` 的 shared/instance/first 组合及 `material.property.*` 的 Float/Range/Int/Color/Vector/Texture2D 变体必须共同覆盖。
- Editor 普通值、引用与上下文迁入强类型 API；脚本/资产/重载的 archive 数据保持专用序列化服务归属。
- RendererList 仍批量执行；ResetCameraState 是 native-only 执行细节，不开放项目录制。
- Initialize 仅允许保留固定 hostfxr 引导；其余业务反向入口纳入生成模块。
- 验证同时参考 `Tests/Managed/Ursa.ManagedHost.ProtocolTests`、native 桥接、真实宿主、Editor 交互和 GPU 像素测试。
- ComputeShader 元数据查询属于 Runtime 模块，因为公开包装在 Ursa.Runtime，不能反向依赖 Ursa.Rendering；GPU 分派仍属于 Rendering。

## 旧函数表（39 项）

来源：`Engine/Engine/Scripting/NativeApi.hpp`。验证组：ManagedSceneApiTests / ManagedAssetApiTests / DotNetHostTests。

| 原入口 | 目标归属 | 状态 |
| --- | --- | --- |
| `logUtf8` | Runtime | 完成：ReportHostMessage |
| `createGameObject` | Runtime | 完成：CreateGameObject |
| `getGameObjectName` | Runtime | 完成：GetName |
| `setGameObjectName` | Runtime | 完成：SetName |
| `getGameObjectActive` | Runtime | 完成：GetActive |
| `setGameObjectActive` | Runtime | 完成：SetActive |
| `getGameObjectParent` | Runtime | 完成：GetParent |
| `setGameObjectParent` | Runtime | 完成：SetParent |
| `getLocalTransform` | Runtime | 完成：LocalTransformValue |
| `setLocalTransform` | Runtime | 完成：LocalTransformValue |
| `getInputState` | Runtime | 完成：GetKey |
| `getTimeState` | Runtime | 完成：TimeState |
| `writeLog` | Runtime | 完成：WriteLog |
| `readFile` | Runtime | 完成：一次拥有 byte[] 结果 |
| `writeFile` | Runtime | 完成：ReadOnlySpan<byte> |
| `addComponent` | Runtime | 完成：AddComponent |
| `getComponentCount` | Runtime | 完成：GetComponents 快照长度 |
| `getComponentAt` | Runtime | 完成：GetComponents 快照索引 |
| `getComponentType` | Runtime | 完成 |
| `getComponentOwner` | Runtime | 完成 |
| `getComponentEnabled` | Runtime | 完成 |
| `setComponentEnabled` | Runtime | 完成 |
| `isComponentActive` | Runtime | 完成 |
| `removeComponent` | Runtime | 完成 |
| `loadTexture` | Runtime | 完成 |
| `loadMaterial` | Runtime | 完成 |
| `addBehaviour` | Runtime | 完成：保留创建时绑定与生命周期顺序 |
| `getGameObjectPersistentId` | Runtime | 完成：GetGameObjectPersistentId |
| `resolveGameObjectPersistentId` | Runtime | 完成：ResolveGameObjectPersistentId |
| `getComponentPersistentId` | Runtime | 完成 |
| `resolveComponentPersistentId` | Runtime | 完成 |
| `destroyGameObject` | Runtime | 完成：DestroyGameObject |
| `getWorldPosition` | Runtime | 完成：Vector3 |
| `setWorldPosition` | Runtime | 完成：Vector3 |
| `getCameraProjection` | Runtime | 完成：CameraProjection，保持弧度 |
| `setCameraProjection` | Runtime | 完成：CameraProjection，保持弧度 |
| `setWindowTitle` | Runtime | 完成 |
| `resourceRequest` | 按业务拆分，取消通用分发 | 完成：两侧表与通用分派均已删除 |
| `submitRenderBatch` | Rendering | 完成：EnqueueBatch，具体命令编码仍待第 8 组 |

## 旧资源操作（74 项）

来源：`Engine/Engine/Scripting/ManagedMeshApi.cpp`。验证组：ManagedMeshApiTests / ManagedSceneApiTests / DotNetHostTests。

| 原入口 | 目标归属 | 状态 |
| --- | --- | --- |
| `binding.materials.get` | Runtime | 完成 |
| `binding.materials.set` | Runtime | 完成 |
| `binding.mesh.get` | Runtime | 完成 |
| `binding.mesh.set` | Runtime | 完成 |
| `camera.settings.get` | Runtime | 完成：CameraRenderingSettings |
| `camera.settings.set` | Runtime | 完成：CameraRenderingSettings |
| `computeShader.findKernel` | Runtime | 完成：强类型元数据查询 |
| `computeShader.threadGroup` | Runtime | 完成：ComputeThreadGroupSize |
| `gameObject.layer.get` | Runtime | 完成：GetLayer |
| `gameObject.layer.set` | Runtime | 完成：SetLayer |
| `gameObject.primitive` | Runtime | 完成：CreatePrimitive |
| `graphicsBuffer.create` | Rendering | 完成 |
| `graphicsBuffer.readback` | Rendering | 完成 |
| `graphicsBuffer.release` | Rendering | 完成 |
| `material.alphaCutoff` | Runtime | 完成 |
| `material.alphaMode` | Runtime | 完成 |
| `material.clone` | Runtime | 完成 |
| `material.color` | Runtime | 完成 |
| `material.create` | Runtime | 完成 |
| `material.destroy` | Runtime | 完成 |
| `material.emissive` | Runtime | 完成 |
| `material.get` | Runtime | 完成 |
| `material.keyword` | Runtime | 完成 |
| `material.keywords` | Runtime | 完成 |
| `material.load` | Runtime | 完成 |
| `material.metallic` | Runtime | 完成 |
| `material.normalScale` | Runtime | 完成 |
| `material.occlusionStrength` | Runtime | 完成 |
| `material.property.get` | Runtime | 完成 |
| `material.property.remove` | Runtime | 完成 |
| `material.property.set` | Runtime | 完成 |
| `material.roughness` | Runtime | 完成 |
| `material.sampler.get` | Runtime | 完成 |
| `material.sampler.set` | Runtime | 完成 |
| `material.shader.get` | Runtime | 完成 |
| `material.shader.set` | Runtime | 完成 |
| `material.texture.get` | Runtime | 完成 |
| `material.texture.set` | Runtime | 完成 |
| `mesh.clear` | Runtime | 完成 |
| `mesh.clone` | Runtime | 完成 |
| `mesh.colors` | Runtime | 完成：Color[] |
| `mesh.create` | Runtime | 完成 |
| `mesh.destroy` | Runtime | 完成 |
| `mesh.get` | Runtime | 完成：拥有 MeshData/通道快照 |
| `mesh.indices` | Runtime | 完成：uint[] |
| `mesh.load` | Runtime | 完成 |
| `mesh.normals` | Runtime | 完成：Vector3[] |
| `mesh.recalculateBounds` | Runtime | 完成 |
| `mesh.recalculateNormals` | Runtime | 完成 |
| `mesh.set` | Runtime | 完成：MeshData，保留未公开的 skin 元数据 |
| `mesh.subMesh` | Runtime | 完成：SubMesh |
| `mesh.subMeshCount` | Runtime | 完成 |
| `mesh.tangents` | Runtime | 完成：Vector4[] |
| `mesh.triangles.get` | Runtime | 完成：uint[] |
| `mesh.triangles.set` | Runtime | 完成：uint[] |
| `mesh.uv0` | Runtime | 完成：Vector2[] |
| `mesh.version` | Runtime | 完成：ulong |
| `mesh.vertices` | Runtime | 完成：Vector3[] |
| `render.camera.setup` | Rendering | 完成 |
| `render.cameras` | Rendering | 完成 |
| `render.context.abort` | Rendering | 完成 |
| `render.context.begin` | Rendering | 完成 |
| `render.context.end` | Rendering | 完成 |
| `render.context.submit` | Rendering | 完成 |
| `render.cull` | Rendering | 完成 |
| `render.error` | 调用局部错误，取消跨调用查询 | 完成：EnqueueBatch 随本次调用返回错误 |
| `render.rendererList.create` | Rendering | 完成 |
| `renderTexture.create` | Rendering | 完成 |
| `renderTexture.readback` | Rendering | 完成 |
| `renderTexture.release` | Rendering | 完成 |
| `resource.reference` | Runtime | 完成：分别 SnapshotMesh/SnapshotMaterial，ResourceReferenceArchive |
| `resource.restore` | Runtime | 完成：分别 RestoreMesh/RestoreMaterial，ResourceReferenceArchive |
| `resource.valid` | Runtime | 完成 |
| `scriptableasset.graph.load` | Runtime | 完成：ScriptableAssetGraphArchive |

## 旧 Editor 操作（25 项）

原入口来自旧 `ManagedEditorBridge.cpp`。现由 `Ursa.Editor/NativeEditor.bindings.cs` 的 36 个具体方法提供，公开 API 为类型化上下文、控件与属性。验证组：EditorBindingsTests / SerializedObjectTests / DotNetHostSmoke；最终窗口验收另见 Evidence。

| 原入口 | 目标归属 | 状态 |
| --- | --- | --- |
| `ApplyProperties` | Editor | 完成 |
| `AssetField` | Editor | 完成 |
| `Assets` | Editor | 完成 |
| `BeginGroup` | Editor | 完成 |
| `Button` | Editor | 完成 |
| `ColorField` | Editor | 完成 |
| `Context` | Editor | 完成 |
| `DrawDefaultInspector` | Editor | 完成 |
| `EndGroup` | Editor | 完成 |
| `FloatField` | Editor | 完成 |
| `IntField` | Editor | 完成 |
| `MarkDirty` | Editor | 完成 |
| `ObjectField` | Editor | 完成 |
| `OpenAsset` | Editor | 完成 |
| `Popup` | Editor | 完成 |
| `PropertyField` | Editor | 完成 |
| `ReadProperty` | Editor | 完成 |
| `SameLine` | Editor | 完成 |
| `Save` | Editor | 完成 |
| `Separator` | Editor | 完成 |
| `Text` | Editor | 完成 |
| `TextField` | Editor | 完成 |
| `Toggle` | Editor | 完成 |
| `VectorField` | Editor | 完成 |
| `WriteProperty` | Editor | 完成 |

## 旧 Bootstrap 反向入口（8 项）

基线入口来自已删除的 `Bootstrap.cs`，当前声明在 `ManagedHost.bindings.cs`。验证组：DotNetHostTests / ManagedProtocolTests。

| 原入口 | 目标归属 | 状态 |
| --- | --- | --- |
| `Initialize` | 固定引导 + ManagedHost | 完成：仅 InitializeBindings 为固定引导，InitializeFramework 为生成回调 |
| `CreateBehaviour` | ManagedHost | 完成 |
| `BindBehaviour` | ManagedHost | 完成 |
| `InvokeBehaviour` | ManagedHost | 完成 |
| `DestroyBehaviour` | ManagedHost | 完成 |
| `SerializeBehaviour` | ManagedHost | 完成：BehaviourFieldArchive |
| `DeserializeBehaviour` | ManagedHost | 完成：BehaviourFieldArchive |
| `LoadScriptAssembly` | ManagedHost | 完成 |

## 旧会话反向入口（15 项）

来源：`Engine/Managed/Ursa.ManagedHost/ScriptSessions.cs`。验证组：ProjectPlaySessionTests / DotNetHostTests。

| 原入口 | 目标归属 | 状态 |
| --- | --- | --- |
| `ConfigureRenderPipeline` | ManagedHost | 完成：可选 PipelineAssetReference |
| `RenderScene` | ManagedHost | 完成 |
| `TransferScriptableAssets` | ManagedHost | 完成 |
| `PrepareEditorExtensions` | ManagedHost | 完成：EditorExtensionInfo[] |
| `DescribeEditorExtensions` | ManagedHost | 完成：合入 PrepareEditorExtensions 单次拥有结果 |
| `InvokeEditorExtension` | ManagedHost | 完成：框架 EditorBindingCapability 和临时模块作用域 |
| `PrepareScriptReload` | ManagedHost | 完成：ScriptReloadArchive |
| `ReadPreparedScriptReload` | ManagedHost | 完成：合入 PrepareScriptReload 的单次拥有结果 |
| `LoadEditorApi` | ManagedHost | 完成 |
| `LoadScriptContent` | ManagedHost | 完成：ScriptContentArchive |
| `CreateScriptSession` | ManagedHost | 完成 |
| `SelectScriptSession` | ManagedHost | 完成 |
| `BindScriptSession` | ManagedHost | 完成 |
| `ValidateScriptSession` | ManagedHost | 完成 |
| `CloseScriptSession` | ManagedHost | 完成 |

## 旧录制命令与 native-only 操作（17 项）

基线来源：`Engine/Engine/Rendering/Commands/RenderCommandProtocol.hpp`。现由 `Managed/Ursa.Rendering/RenderCommands.bindings.cs` 声明 27 个具体命令；全局和计算参数各有 Float/Int/Vector/Matrix/Texture/Buffer 变体。验证组：BindingsTests / RenderCommandProtocolTests / RenderingBindingsTests / RenderCommandExecutorTests / DotNetHostSmoke。

| 原入口 | 目标归属 | 状态 |
| --- | --- | --- |
| `SetRenderTarget` | RecordedCommand | 完成 |
| `ClearTarget` | RecordedCommand | 完成 |
| `SetViewport` | RecordedCommand | 完成 |
| `DrawRendererList` | RecordedCommand | 完成 |
| `DrawMesh` | RecordedCommand | 完成 |
| `Blit` | RecordedCommand | 完成 |
| `CopyBuffer` | RecordedCommand | 完成 |
| `DispatchCompute` | RecordedCommand | 完成 |
| `SetGlobalParameters` | RecordedCommand | 完成 |
| `GetTemporaryTarget` | RecordedCommand | 完成 |
| `ReleaseTemporaryTarget` | RecordedCommand | 完成 |
| `SetScissor` | RecordedCommand | 完成 |
| `CopyTexture` | RecordedCommand | 完成 |
| `DrawFullscreen` | RecordedCommand | 完成 |
| `SetRenderTargets` | RecordedCommand | 完成 |
| `SetComputeParameters` | RecordedCommand | 完成 |
| `ResetCameraState` | native-only | 完成 |

## 必须核对的公共调用集合

- Demo：`Assets/Scripts` 的玩法、Rendering 管线及 Editor 扩展。
- Engine：`Docs/Examples`、`Managed/Ursa.*` 包装和 `Tests/Managed` 夹具。
- 本表完成时必须附生成清单/声明位置和测试证据；未迁移项不能通过保留 JSON fallback 或删除原能力消除。
