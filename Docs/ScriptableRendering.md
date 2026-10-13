# 可编程渲染接口与实施矩阵

本轮以 Unity 经典 SRP 的职责和公开接口为参考，项目 C# 控制相机及命令顺序，Ursa 原生层负责批量执行、资源和 GPU 同步。RenderGraph 在后续变更中引入。本文是首批交付契约，功能状态由 OpenSpec `scriptable-render-pipeline/tasks.md` 的验证记录跟踪。

| 参考接口/职责 | Ursa 首批接口 | 验证方式 |
| --- | --- | --- |
| ScriptableObject、CreateAssetMenu | 独立实例、持久资产、稳定类型与字段、资产创建菜单 | 创建/销毁、往返、缺失类型、引用图及菜单测试 |
| RenderPipelineAsset.CreatePipeline | 可由项目完整继承的资产工厂 | 项目程序集工厂、配置保存与失败候选测试 |
| RenderPipeline.Render / Dispose | 可复用相机 List、渲染回调、确定清理 | 多相机、回调异常、事件及程序集卸载测试 |
| ScriptableRenderContext | 相机设置、Cull、CreateRendererList、ExecuteCommandBuffer、Submit | 句柄有效期、批次顺序及离屏/GPU 测试 |
| Drawing/Filtering/SortingSettings | Pass 标签、Layer、队列、透明排序和替换材质 | 视锥、过滤和像素测试 |
| CommandBuffer | Clear/Dispose、目标/Viewport、列表/网格、复制/全屏、计算命令 | 已交付资源存活、状态同步与读回测试 |
| RenderTexture / GraphicsBuffer / ComputeShader | 有界资源、临时目标、已导入内核 | 目标能力、派发、复用及释放测试 |
| GraphicsSettings / RenderPipelineManager | 项目管线 GUID、帧边界切换、重载 | Editor/Player/Pak 配置与生命周期测试 |

参考文件位于工作区 `UnityCsReference/Runtime/Export/RenderPipeline`、`Runtime/Export/Graphics/RenderingCommandBuffer*` 和 `Runtime/Export/Scripting/ScriptableObject.bindings.cs`。参考其接口与行为，Ursa 使用自己的 C++ 实现、命名和 archive，公共 API 不出现 `Vk*`。

## 执行与资源边界

一个 RendererList 表达一批绘制；原生层展开列表，项目无需逐对象跨语言调用。CommandBuffer 按显式顺序执行，执行时复制所需参数并保留资源。Submit 交付有效逻辑段，窗口呈现由宿主统一完成。

上下文、剔除结果和 RendererList 只在所属帧/会话有效。CommandBuffer 清空或释放不会破坏已交付段；GPU 使用中的 backing 在完成前保留。两帧槽位分别持有上传区间、临时描述符与命令池，只在对应槽位复用时等待。

## 相机、场景快照与列表有效期

`CameraComponent.rendering` 保存相机类型（Game、SceneView、Preview）、深度顺序、清除标志、背景色、32 位剔除掩码、归一化视口和可选目标尺寸。视口必须位于 `[0,1]` 内且宽高为正；目标宽高必须同时为零（使用宿主目标）或同时非零。旧场景缺少这些数据时使用默认 Game 相机、深度 0、全 Layer、全视口和颜色加深度清除。GameObject Layer 范围为 0 到 31，旧场景默认 Layer 0；Scene 与 Prefab 都保存该值。

每帧先完成场景更新，再调用一次 `RenderScene::Capture(scene, aspect)` 建立不可变快照。快照收集当帧有效对象、Renderer/网格与子网格、材质版本和参数、世界变换、世界边界、骨骼矩阵、灯光及相机。相机按 `depth` 升序排列；深度相同时按持久身份稳定排序。不同相机应共享同一份快照，不能在相机之间重新采集，以免一帧内看到不同的场景状态。

`RenderSceneContext::Begin(sessionId, frameId, snapshot)` 开启帧上下文后，每台相机各自调用 `Cull(camera)`。CPU 剔除用相机视锥和 `cullingMask` 检查有效 Renderer；它不删除或改写场景对象。普通网格使用变换后的包围盒，蒙皮网格优先使用当前变形顶点边界，无法取得有效蒙皮数据时使用保守边界。对同一 `CullingResults` 可以创建多个列表，例如不透明与透明阶段；列表由队列范围、Layer 掩码、Shader 标签、排序规则和替换材质共同决定。

`RenderContextToken`、`CullingResults` 和 `RendererList` 只能在创建它们的线程、会话和帧代数内访问。调用 `End()`、开始下一帧、切换会话或复用列表槽后，旧结果都失效；需要跨相机复用剔除结果时，应在同一有效上下文内创建列表并消费它们。原生 `Entries(list)` 可用于检查已经排序的列表内容。

同一帧上下文可以为两台相机分别设置相机属性、剔除并建立列表，再按录制顺序提交列表命令。清空缓冲后可录制下一台相机的列表；已提交批次与资源由上下文/native 层保留：

```csharp
using var context = new ScriptableRenderContext(aspect);
using var commands = new CommandBuffer("camera-lists");
var drawing = new DrawingSettings { ShaderTags = new[] { "SRPDefaultUnlit" } };
var filtering = new FilteringSettings();

foreach (CameraComponent camera in cameras)
{
    context.SetupCameraProperties(camera);
    CullingResults culling = context.Cull(camera);
    RendererList renderers = context.CreateRendererList(culling, drawing, filtering);
    commands.DrawRendererList(renderers, context);
    commands.Execute(context);
    commands.Clear();
}
```

托管协议测试覆盖两台相机的列表句柄、录制顺序、逐段提交和 Clear 后重录；原生上下文会拒绝跨会话或过期列表 token。最终 GPU 命令执行和同帧逻辑 Submit 合并由任务 6.6 接入。

稳定 Shader 的反射、Pass 选择及结构化管线键缓存按版本失效；材质参数按 PerMaterial 版本更新，物体/骨骼数据按帧上传。资源统计与 Tracy 区域用于验证这些缓存和等待规则。

Vulkan RenderPass 缓存键包含附件格式及 Load/Store 操作；Framebuffer 键包含兼容 RenderPass、附件视图和目标尺寸，采样器按过滤、寻址和最大 LOD 描述复用。Framebuffer 缓存最多保留 64 项，并持有附件引用；容量回收只移除没有命令或 GPU 工作引用的项。尺寸、格式或附件变化会产生新键，旧 framebuffer 会在安全淘汰时释放，不会悬挂引用被销毁的附件。

`RenderFrameQueue` 使用两个槽位。`RenderFrameStatistics.slotReuses` 计数槽位循环使用，`slotWaits` 只计数复用时实际等待未完成 fence 的情况；`uploadedBytes`、`uploadBuffersCreated` 和 `uploadCapacity` 分别描述上传量、分配块数和保留容量。默认每槽上传块为 1 MiB、总预算为 64 MiB；达到预算时上传失败并返回诊断。命令列表统计另行报告 `resets`、`descriptorPoolsCreated`、`transientGroupsCreated` 和 `activeTransientGroups`，用于区分临时资源创建、复用和回收。

`Complete()` 结束并提交当前命令列表，返回对应 GPU fence；只有取得交换链图像的帧才呈现。`Begin()` 复用有未完成提交的槽位时才阻塞等待；CPU 等待通过 Tracy 的 `Rendering.FrameSlotWait` 区域观察，GPU 执行时长由 Vulkan GPU 区域采样，呈现和交换链获取各有独立区域。因而槽位等待、GPU 工作和资源存活计数是不同指标，不能把 CPU 等待时间当作 GPU 执行时间。

资源屏障的 before/after 状态会在单个命令列表内按 mip 追踪，并在 GPU 队列提交前与此前成功提交的状态核对；只有成功提交才更新队列状态。Texture 屏障可指定 mip 范围，计数为零表示从 base mip 到末级。Framebuffer 附件若同时绑定为同一 Pass 的采样纹理，会在记录命令前拒绝；读取回读和多段逻辑提交都按队列顺序转换状态。

## Shader 数据频率与 Compute 源

Shader 的系统字段按数据频率分组：`_UrsaCameraPosition`、环境光和灯光数据属于 PerFrame；`_UrsaViewProjection` 属于 PerCamera；`_UrsaModel` 和 `_UrsaNormal` 属于 PerObject；其余反射到 MaterialData 的属性属于 PerMaterial。Uniform block 以反射偏移写入，描述符布局以合并后的 binding、类型、数量和 stage mask 建立。纯 PerMaterial 绑定组按材质实例、版本、Shader 和布局身份缓存；材质版本变化建立新 backing，旧 backing 保留到命令/GPU 引用释放。当前 Draw 内相同布局和字节内容的 Uniform backing 以及相同骨骼 palette 会合并复用，Sampler 按 filter/addressing 设置复用。

项目 Compute 源使用 `.compute` 扩展名、`.meta` GUID，以及具名内核与固定线程组声明：

```hlsl
#pragma kernel Fill
[numthreads(8, 4, 1)]
void Fill(uint3 id : SV_DispatchThreadID)
{
    // 写入导入反射声明的资源
}
```

导入为每个 `#pragma kernel` 编译 Compute SPIR-V，验证 `[numthreads]` 各维非零且总线程数不超过 1024，检查资源反射，并把 kernel 名称、线程组和绑定布局写入带稳定 GUID 的 Shader 资源；Pak 发布同一编译产物。include 内容、被引用 Shader、编译器版本和反射格式变化都会更新导入指纹。缺少 kernel、编译失败或不支持的 descriptor set 不会发布半成品。托管 `ComputeShader` 的查找/绑定/Dispatch API 在 6.5 接入。

## 首批画面与范围

默认 Forward 保留 Unlit、PBR、透明、静态/蒙皮网格。项目示例覆盖离屏渲染、多个相机、全屏合成及计算输出。SceneView、Game 和 Preview 相机通过同一管线入口渲染，编辑模式不执行 Behaviour 游戏更新。

RenderGraph、阴影体系、Deferred/Forward+、IBL、TAA、GPU 驱动剔除和异步计算队列留到后续。完整 Unity 文件/API 兼容不属于本轮交付，未支持的资源与命令描述会返回明确诊断。

## ABI 与产物升级规则

- 托管/原生边界使用 Windows x64、小端序、固定宽度标量与顺序布局；禁止通过 ABI 传 STL/EASTL 容器、CLR 对象、原生 bool 或隐式字符串布局。
- 渲染上下文 token 为 24 字节：sessionId/frameId 为 64 位，index/generation 为 32 位。剔除/列表结果在 token 后携带结果 index/generation，总计 32 字节；不同上下文、会话、帧或代数不能互换。
- 命令批次初始版本为 1，头部 16 字节，包含 version/headerSize/commandCount/byteSize。记录头部为 opcode/byteSize（8 字节），每条记录按 8 字节对齐，整个批次最多 64 MiB。先校验完整批次的版本、范围、记录个数与 opcode，再校验各命令参数；未认领尾部和未知 opcode 都报错。
- ABI 结构、调用约定或字段语义发生不兼容变化时，必须提升对应 API/命令协议版本，同时更新两端结构尺寸和偏移测试。新增接口不能以相同 ABI 版本覆盖旧函数表；宿主须在任何调用前检查版本及大小。
- ScriptableAsset 统一 archive 类型为 ursa.ScriptableAsset，描述版本初始为 1。托管字段 payload 的 FormatVersion 与脚本类型 Version 是独立概念：前者决定字段封套格式，后者决定项目类型数据契约。未知 archive/payload 版本必须给出诊断，不能猜测解码。
- .ursaasset 源文件的自身 GUID 由 .meta 提供；导入描述包含解析后的 sourceGuid/localFileId、稳定 typeId/typeVersion 和字段 payload。程序集、框架或类型目录变化参与导入指纹，缺失类型仍保留源字段用于编辑恢复。
- Shader 外层资源封套版本、反射/布局产物版本、源语法版本独立演进。当前 `.ursashaderasset` 封套为 v2、Shader archive 为 v4、SPIR-V 反射 schema 为 v1、编译缓存键为 V2；Pass、规范化反射和合并后的绑定布局都有稳定身份。导入校验入口、descriptor、uniform block 和顶点输入后才发布资产；源指纹包含源文件、UsePass/fallback 依赖 Shader 内容、include 内容、编译器和格式版本。反射布局或参数频率约定改变时提升对应版本并使旧 Library 产物失效；源 GUID、属性名和有效材质引用保持不变。Player 拒绝不兼容产物，Editor/导入工具从源重建，普通 C++ 构建不准备项目内容。

协议的双端尺寸/偏移和恶意批次测试位于 Tests/Engine/Rendering 与 Tests/Managed/Ursa.ManagedHost.ProtocolTests；资产描述的双格式往返与版本拒绝测试位于 Tests/Engine/ScriptableAssets。
