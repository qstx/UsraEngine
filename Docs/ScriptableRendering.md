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

foreach (Ursa.Rendering.Camera camera in cameras)
{
    context.SetupCameraProperties(camera);
    CullingResults culling = context.Cull(camera);
    RendererList renderers = context.CreateRendererList(culling, drawing, filtering);
    commands.DrawRendererList(renderers, context);
    commands.Execute(context);
    context.Submit();
    commands.Clear();
}
```

托管协议测试覆盖两台相机的列表句柄、录制顺序、逐段提交和 Clear 后重录；原生上下文会拒绝跨会话或过期列表 token。每台相机设置前必须提交上一台相机的命令；设置相机时目标、全局参数、计算参数、Viewport 和 Scissor 恢复初始状态。

## 可编译的经典命令示例

完整源码见 [ClassicRendering.cs](Examples/ClassicRendering.cs)。`DrawCamera` 设置相机，用同一剔除结果创建不透明和透明列表，在临时颜色/深度目标绘制，然后复制颜色到相机输出。两段分别调用 `ExecuteCommandBuffer` 和 `Submit`；交付后立即 Clear 或 Dispose 缓冲不会改变已交付数据。复制要求输入与相机输出的尺寸、格式相同；需要缩放或材质效果时使用 `Blit`。

调用方创建上下文并在完成后 Dispose。回调失败时先调用 `Abort`，丢弃未提交段；已 Submit 的段仍安全完成。正常 Dispose 若发现尚未 Submit 的记录会报告错误并关闭作用域。Submit 不等待 GPU，也不呈现；宿主每个窗口上下文只获取一次交换链图像，收尾时消费对应同步并呈现一次。离屏上下文没有呈现。

该源码由 `Ursa.SessionFixtures` 直接引用，CMake 在测试暂存目录编译同一文件。`UrsaDotNetHostSmoke` 通过真实 .NET 宿主连续执行三帧，检查六次批次交付，以及绿色几何、蓝色清除背景的 GPU 像素。复跑：

```powershell
cmake --build Engine/Build --config Release --target UrsaDotNetHostTests -- /m:1
ctest --test-dir Engine/Build -C Release -R '^(UrsaDotNetHostSmoke|UrsaManagedProtocolTests)$' --output-on-failure
```

支持范围包括列表/显式子网格与 Pass 绘制、全局 float/int/vector/matrix/纹理/缓冲、Viewport/Scissor、颜色/深度清除、多附件、复制、全屏合成及同队列计算。资源描述限 2D、单采样的 RGBA8、RGBA16F 和 D32，最多八个颜色附件且受设备能力限制。临时 ID 只在当前上下文有效，结束前须 Release；资源/内核缺失、超出容量、读写反馈、过期列表与未提交段均返回诊断。

稳定 Shader 的反射、Pass 选择及结构化管线键缓存按版本失效；材质参数按 PerMaterial 版本更新，物体/骨骼数据按帧上传。资源统计与 Tracy 区域用于验证这些缓存和等待规则。

Vulkan RenderPass 缓存键包含附件格式及 Load/Store 操作；Framebuffer 键包含兼容 RenderPass、附件视图和目标尺寸，采样器按过滤、寻址和最大 LOD 描述复用。Framebuffer 缓存最多保留 64 项，并持有附件引用；容量回收只移除没有命令或 GPU 工作引用的项。尺寸、格式或附件变化会产生新键，旧 framebuffer 会在安全淘汰时释放，不会悬挂引用被销毁的附件。

`RenderFrameQueue` 使用两个槽位。`RenderFrameStatistics.slotReuses` 计数槽位循环使用，`slotWaits` 只计数复用时实际等待未完成 fence 的情况；`uploadedBytes`、`uploadBuffersCreated` 和 `uploadCapacity` 分别描述上传量、分配块数和保留容量。默认每槽上传块为 1 MiB、总预算为 64 MiB；达到预算时上传失败并返回诊断。命令列表统计另行报告 `resets`、`descriptorPoolsCreated`、`transientGroupsCreated` 和 `activeTransientGroups`，用于区分临时资源创建、复用和回收。

`Complete()` 结束并提交当前命令列表，返回对应 GPU fence；只有取得交换链图像的帧才呈现。`Begin()` 复用有未完成提交的槽位时才阻塞等待；CPU 等待通过 Tracy 的 `Rendering.FrameSlotWait` 区域观察，GPU 执行时长由 Vulkan GPU 区域采样，呈现和交换链获取各有独立区域。因而槽位等待、GPU 工作和资源存活计数是不同指标，不能把 CPU 等待时间当作 GPU 执行时间。

资源屏障的 before/after 状态会在单个命令列表内按 mip 追踪，并在 GPU 队列提交前与此前成功提交的状态核对；只有成功提交才更新队列状态。Texture 屏障可指定 mip 范围，计数为零表示从 base mip 到末级。Framebuffer 附件若同时绑定为同一 Pass 的采样纹理，会在记录命令前拒绝；读取回读和多段逻辑提交都按队列顺序转换状态。

## Shader 数据频率与 Compute 源

当前 Shader 资源封套为 v3、反射契约为 v2，编译/绑定策略为 `UrsaBindingMapV5UniformLayout`。uniform 尺寸规范化会改变布局身份，因此导入记录与编译缓存必须一起失效；旧 Library 产物会由项目导入重新生成，源 GUID 和材质引用保持原样。

普通 Rendering 调用由 `NativeRendering.bindings.cs` 生成，包含帧上下文、相机快照、Cull/RendererList、逻辑 Submit、资源创建/释放和原始字节读回。入口验证所属线程、会话、帧与代数；EnqueueBatch 的错误随调用返回，不再查询跨调用错误字符串。

`RenderCommands.bindings.cs` 是录制协议的唯一声明，17 个原命令族展开为 27 个具体命令，其中 ResetCameraState 为 native-only。生成器产出稳定 opcode、两侧编码/解码、参数布局及独立批次指纹；旧 v2 手写协议已删除，旧批次会被拒绝。基础批次框架为 v1，48 字节头携带 SHA-256 契约身份，记录按 8 字节对齐且填充必须为零，总量上限 64 MiB。managed 和 native codec 共同验证长度、类型、UTF-8 与预算；native 完整解码并解析所有资源后，执行器才统一预检和交付，末尾错误不会让前面的清屏或绘制生效。

`RenderTargetIdentifier.CameraTarget` 表示宿主交付的当前相机输出，非零整数表示当前上下文的临时 ID，`RenderTexture` 表示持久 GPU 资源。`SetRenderTargets` 支持最多八个颜色目标及独立深度目标，实际数量、格式和尺寸仍按设备能力校验。全屏材质的顶点程序使用 `SV_VertexID` 生成三角形；`Blit` 把来源绑定到 `_Source`、`_MainTex` 或 `_BlitTexture`，`DrawFullscreen` 使用命令录制的全局纹理。矩阵接口接受 `Matrix4x4`，按 `[row,column]` 索引、列序存储并乘列向量；上传 uniform 时根据反射转换，HLSL `row_major`/`column_major` 得到同一数学值。`DrawingSettings.CameraPosition` 为可选 `Vector3`，null 使用当前相机位置。

`Execute` 交付资源与参数快照，`context.Submit()` 关闭逻辑段，GPU 工作在上下文收尾时按顺序编码；清空命令缓冲或释放已交付资源的包装不会破坏这些快照。临时 ID 可以跨同一上下文的多个命令缓冲使用，结束前必须 Release；下一上下文不能沿用旧 ID。池最多保留 32 个目标、512 MiB，总量有界，完成 fence 前不复用 backing。每个持久目标/缓冲的创建预算为 256 MiB。`GetRawData` 是显式同步读回，会等待相应 GPU 工作，不用于逐绘制热路径。

`UrsaRenderCommandExecutorTests` 直接验证合成像素、全局 float/vector/int/matrix、HDR、双颜色/深度、纹理与缓冲复制、临时 ID、池复用、未提交段和反馈拒绝。`UrsaManagedSceneApiTests` 通过真实 ABI 交付多附件清除/复制，在源资源 Release 后提交并读回目标，验证托管桥接的资源保留。

Shader 的系统字段按数据频率分组：`_UrsaCameraPosition`、环境光和灯光数据属于 PerFrame；`_UrsaViewProjection` 属于 PerCamera；`_UrsaModel` 和 `_UrsaNormal` 属于 PerObject；其余反射到 MaterialData 的属性属于 PerMaterial。Uniform block 以反射偏移写入，描述符布局以合并后的 binding、类型、数量和 stage mask 建立。纯 PerMaterial 绑定组按材质实例、版本、Shader 和布局身份缓存；材质版本变化建立新 backing，旧 backing 保留到命令/GPU 引用释放。当前 Draw 内相同布局和字节内容的 Uniform backing 以及相同骨骼 palette 会合并复用，Sampler 按 filter/addressing 设置复用。

项目 Compute 源使用 `.compute` 扩展名、`.meta` GUID，以及具名内核与固定线程组声明：

```hlsl
#pragma kernel Fill
[[vk::image_format("rgba8")]] RWTexture2D<float4> _Output : register(u0);
[numthreads(8, 4, 1)]
void Fill(uint3 id : SV_DispatchThreadID)
{
    _Output[id.xy] = float4(1, 0, 0, 1);
}
```

导入为每个 `#pragma kernel` 编译 Compute SPIR-V，验证 `[numthreads]` 各维非零且总线程数不超过 1024，检查资源反射，并把 kernel 名称、线程组和绑定布局写入带稳定 GUID 的 Shader 资源；Pak 发布同一编译产物。include 内容、被引用 Shader、编译器版本和反射格式变化都会更新导入指纹。缺少 kernel、编译失败或不支持的 descriptor set 不会发布半成品。

`ComputeShader.FindKernel` 和 `GetKernelThreadGroupSizes` 从已发布 Shader 读取具名内核与线程组信息。CommandBuffer 的 `SetComputeFloat/Int/Vector/Matrix/Texture/Buffer` 按 Shader、内核保存参数快照，`DispatchCompute` 使用该内核的反射创建绑定；缺失资源、错误资源种类、存储格式不匹配及超出设备限制的派发会拒绝执行。RGBA8 存储纹理声明需要上例的 `vk::image_format`，HDR RGBA16F 对应 `rgba16f`；普通 `RWTexture2D<float4>` 在当前 DXC 上可能产生 `rgba32f`，不能绑定到格式不同的目标。

计算与图形按同一队列顺序编码，执行器在 storage-write、sample 和 transfer 之间生成屏障。`UrsaRenderCommandExecutorTests` 验证连续帧的 compute→全屏采样及缓冲逐元素读回；`UrsaManagedSceneApiTests` 从导入产物经真实 ABI 解析内核、交付资源和数值参数并验证 GPU 输出。显式 `GetRawData` 在上下文收尾后调用，避免把尚未物理提交的逻辑段当作已经可读的结果。

## 项目 Graphics 配置存储

源配置位于项目 `ProjectSettings/Graphics.json`，编辑器通过独立 `settings:` VFS 挂载读写。它不属于 Assets，也不参与 Project 资产菜单。文件包含 `UrsaGraphicsSettings` 格式、版本 1 和 `renderPipeline`；默认值为 null，自定义选择包含资产的 `guid` 和非零 `localFileId`。选择不保存源路径，因此移动资产及其 `.meta` 后仍有效。

`SaveGraphicsSettings` 对非空选择要求资产校验器，失败时保留现有文件。共享校验按 GUID 解析 `.ursaasset`，核对当前 Game 类型目录、RenderPipelineAsset 继承、资产 local file ID、脚本类型 GUID/local file ID 和版本；普通 ScriptableObject、材质、缺失类型或错误身份不能成为有效选择。

项目管线由 `RenderPipelineAsset` 派生类型和 `[CreateAssetMenu]` 菜单声明，脚本类型身份自动写入源 `.meta`，并覆写 `RenderPipelineAsset.CreatePipeline()`，示例见 [ProjectPipeline.cs](Examples/ProjectPipeline.cs)。通过项目 Graphics 设置选择已保存资产时，保存其 `.meta` GUID 与 local file ID；清除选择保存 `null` 并恢复默认 Forward。不要在 Behaviour 中以运行时 `CreateInstance` 代替项目选择，因为它没有可发布的资产身份。

内容导入在发布前重复校验选择及依赖闭包，将配置放入 `prj:__UrsaSettings/Graphics.json`。松散内容和 Pak 包含同一份配置；源设置的增加、删除或修改会使已发布内容失效。导入期间设置变化或无效选择不会替换上一份成功发布的 manifest。`UrsaGraphicsSettingsTests` 验证结构与 VFS 保存失败，`UrsaAssetImportPipelineTests` 验证重开、移动、清除、非法资产以及松散/Pak 一致性。

Application 的原生入口已改用同一批 RenderScene、Cull、RendererList 和命令执行服务。原生 Forward 在启动时读取已发布 Graphics 配置；若配置需要托管自定义管线而运行环境不支持，它会报告资产 GUID，并在场景更新前清理 swapchain、device、Scene 和应用回调。启用 .NET 的 Player 设置窗口输出后调用活动 C# 管线。FrameCapture 在物理提交和 Present 前读取窗口目标；MaterialShowcase 经多次缩放、最小化恢复和 60 帧 capture 回归通过。`UrsaApplicationSceneTests` 验证纯原生 Forward 和缺少托管自定义管线能力时的初始化清理。

## Editor 资产创建与 Graphics 设置

项目 C# 类型可以用 `[CreateAssetMenu]` 声明 Project 创建菜单项。菜单声明只对公开、具体、非泛型的 Game `ScriptableObject` 有效；重复菜单路径、无效文件名、非资产类型和不受支持字段会在脚本目录生成时报告类型及源文件，保留上一份有效目录。`MenuName` 支持分类路径，`Order` 决定同级顺序，`FileName` 是默认文件名（编辑器会追加 `.ursaasset`）。例如：

```csharp
using Ursa;
using Ursa.Rendering;

[UrsaScriptType("game.presentation-pipeline", Version = 1)]
[CreateAssetMenu(MenuName = "Rendering/Presentation Pipeline", FileName = "PresentationPipeline", Order = 20)]
public sealed class PresentationPipelineAsset : RenderPipelineAsset
{
    public Color clearColor = new(0.05f, 0.1f, 0.25f, 1);

    protected override RenderPipeline CreatePipeline() => new PresentationPipeline(clearColor);
}
```

成功编译后，从顶栏 `Assets > Create` 或 Project 文件夹的右键 `Create` 选择菜单项。编辑器会在当前 Project 文件夹创建 `.ursaasset` 和独立 `.meta` GUID，按序列化字段默认值写入 `Fields` 与版本化 `FieldTypes`。选择新资产后，Inspector 显示标量、字符串、枚举、向量、列表和资源引用控件；控件修改会显示 Dirty，只有点击 `Save ScriptableObject` 才通过 VFS 原子保存。引用选择器列出当前可解析的资源；场景对象和文档临时资源不能保存到持久资产。只读项目来源禁用保存。类型缺失或版本不兼容时，Inspector 显示稳定类型身份、脚本源 GUID/local file ID 和原始字段，保留数据等待类型恢复。

顶栏 `Window > Project Graphics Settings` 打开项目默认管线设置。选择器只列出当前 Game 类型目录中继承 `RenderPipelineAsset`、资产身份/版本与工厂均可验证的 `.ursaasset`；普通 ScriptableObject、材质、缺失类型和不兼容资产不会成为候选。`Apply Graphics Settings` 会先校验资产并在当前编辑会话创建管线候选，候选工厂成功后才原子保存 `ProjectSettings/Graphics.json`。工厂失败显示资产诊断并保留活动管线与旧设置；清除选择保存 `null` 并恢复默认 Forward。项目脚本重载与 Run 会话都从该设置恢复选择。

Scene、Game 与材质 Preview 共用活动项目管线，但使用不同场景、相机类型和 RenderTexture：Scene 使用编辑器轨道相机并标记为 `SceneView`，Game 只接收当前有效的 `Game` 相机，没有有效游戏相机时显示空状态，材质预览使用独立立方体场景及 `Preview` 相机。Project 中单击材质资产时，即使当前 Scene 文档仍打开，也会显示独立 Material Preview 窗口。每个目标包含独立颜色/深度附件，按目标物理尺寸设置相机比例；渲染回调逐目标绑定所属 Scene API，渲染后恢复稳定编辑/运行 API。视口缩放、DPI、裁剪、Gizmo 和 Game 分辨率偏好保持在 Editor 合成层，目标替换会等待 GPU 空闲后注销旧 ImGui 纹理。

建议按以下顺序复跑资产与活动管线测试；实际窗口交互仍按 OpenSpec 的 Editor 验收任务记录：

```powershell
cmake --build Engine/Build --config Release --target UrsaEditor UrsaScriptBuildTests UrsaScriptableAssetSourceTests UrsaGraphicsSettingsTests UrsaDotNetHostTests UrsaMaterialDraftTests -- /m:2 /nr:false /p:CL_MPCount=1
ctest --test-dir Engine/Build -C Release -R '^(UrsaScriptBuildTests|UrsaScriptableAssetSourceTests|UrsaGraphicsSettingsTests|UrsaMaterialDraftTests|UrsaDotNetHostSmoke)$' --output-on-failure
```

## 首批画面与范围

`RenderPipelineManager.SetPipelineAsset` 录入候选选择，`Render` 的下一次边界才调用工厂并替换实例。未变化的活动资产继续复用；活动资产成功 OnValidate 后更新验证代数，下一边界重建。非活动资产的验证不改变当前实例。工厂失败保留上一有效实例，诊断包含类型、持久身份（如有）、CreatePipeline 和原始异常。

Render 内请求切换只影响下一次边界。Manager 通过 `RenderInternal` 拒绝已 Dispose 的实例，在开始事件、Render 或结束事件失败时仍调用结束/Abort；一个错误保留其原始堆栈，多个错误用 AggregateException 一起交付。Reset 先解除资产和事件引用，再调用 Dispose，所以清理回调失败不会保留活动配置或旧事件。真实宿主的 GPU 回归先提交绿色清除，再录入未提交的红色清除并同时触发 Render/结束事件异常；输出仍为绿色，后续上下文可以继续执行。

管线实例、候选选择和四类渲染事件按脚本会话隔离，选择另一会话时恢复其自身状态。关闭会话先解除管线与事件引用并 Dispose，再关闭 ScriptableObject 图和卸载 ALC；即使项目没有显式注销事件，也不由共享 Rendering 程序集保留旧项目类型。会话创建、绑定和关闭必须发生在渲染边界。Player/Editor 关闭顺序保留原生 Scene API、Scene 和资源会话，直至托管管线与资产完成清理。

`UrsaManagedProtocolTests` 核对编辑/运行会话的实例、事件隔离、资产快照的未知字段/循环引用与关闭后的资产失效；`UrsaDotNetHostSmoke` 让项目管线拥有真实 RenderTexture 并故意保留事件注册，验证自动 Dispose、GPU 输出与 ALC 卸载。真实宿主还在独立编辑会话之间迁移未保存的管线字段和共享资产图，验证失败工厂/OnValidate 保留源会话、成功迁移输出新像素、旧 token 失效且未写回源产物。

Editor 在新会话恢复 Behaviour 字段后调用 `TransferScriptableAssets`，恢复资产图及管线选择并验证候选工厂；随后才替换活动会话。每个编辑会话拥有原生资源 API，其生命周期覆盖托管清理。兼容字段和未知数据保留，程序集/类型引用及运行缓存不会序列化。类型目录使用 CLR 标量身份，读取仍接受旧的 C# 标量别名；集合按元素类型验证。

`UrsaPlaySessionTests` 检查服务清理时 Scene 仍存在，项目 Run/Stop 与扩展回归由 `UrsaProjectPlaySessionTests` 和 `UrsaProjectEditorExtensionsTests` 覆盖。`UrsaManagedEditorWorkflowTests` 运行实际隐藏窗口，覆盖编译失败/恢复、未保存字段、Run 中源码延后、Stop 后自动重载和三次 Run/Stop；源码检查只有在允许编译时才更新已观察指纹，避免漏掉延后的重载。

复跑管线生命周期、定义和 GPU 示例：

```powershell
cmake --build Engine/Build --config Release --target UrsaManagedTestAssemblies UrsaDotNetHostTests -- /m:1
ctest --test-dir Engine/Build -C Release -R '^(UrsaManagedProtocolTests|UrsaDotNetHostSmoke|UrsaManagedEditorWorkflowTests)$' --output-on-failure
```

`UrsaDotNetHostSmoke` 会编译这两个 `Engine/Docs/Examples` 源文件；宿主测试执行经典离屏命令和 `PresentationPipelineAsset`，逐像素检查自定义管线输出。协议测试覆盖替换和销毁实例、回调配对、失败候选保留环境及恢复默认 Forward。管线重载测试覆盖跨 ALC 的未保存字段、循环资产引用、失败恢复和旧句柄失效。

默认 Forward 保留 Unlit、PBR、透明、静态/蒙皮网格。项目示例覆盖离屏渲染、多个相机、全屏合成及计算输出。SceneView、Game 和 Preview 相机通过同一管线入口渲染，编辑模式不执行 Behaviour 游戏更新。

管线回调和相机事件使用 `List<Ursa.Rendering.Camera>`。游戏组件可通过 `Camera.FromComponent` 转为复用包装；Setup 从当前快照取得类型、清除、背景、Layer 掩码、视口及实际输出尺寸。包装只属于创建它的脚本会话，Setup/Cull 会检查组件身份，避免同 GUID 的 Run 克隆接受旧编辑包装。SceneView/Preview 的包装由宿主提供，管线不依赖组件才能识别相机类型。

默认 Forward 复用绘制/过滤设置和 CommandBuffer，对每台相机设置属性、创建不透明/透明列表、设置像素视口与 Scissor、按 ColorAndDepth/Depth/Nothing 清除并 Submit。清除使用 Scissor 对应的 RenderPass render area，保留其他相机区域；缺省背景取相机配置。`UrsaDotNetHostSmoke` 经真实 C# 默认管线连续三帧绘制 Unlit、PBR、纹理、透明、带 baseVertex 的子网格与运动蒙皮，并与既有原生路径逐像素对照；另验证两个相机的分区清除与 Depth/Nothing 保留颜色。

RenderGraph、阴影体系、Deferred/Forward+、IBL、TAA、GPU 驱动剔除和异步计算队列留到后续。完整 Unity 文件/API 兼容不属于本轮交付，未支持的资源与命令描述会返回明确诊断。

## ABI 与产物升级规则

- 托管/原生边界使用 Windows x64、小端序、固定宽度标量与顺序布局；禁止通过 ABI 传 STL/EASTL 容器、CLR 对象、原生 bool 或隐式字符串布局。
- 渲染上下文 token 为 24 字节：sessionId/frameId 为 64 位，index/generation 为 32 位。剔除/列表结果在 token 后携带结果 index/generation，总计 32 字节；不同上下文、会话、帧或代数不能互换。
- 命令批次当前版本为 2，头部 16 字节，包含 version/headerSize/commandCount/byteSize。记录头部为 opcode/byteSize（8 字节），每条记录按 8 字节对齐，整个批次最多 64 MiB。先校验完整批次的版本、范围、记录个数与 opcode，再校验各命令参数；未认领尾部和未知 opcode 都报错。
- ABI 结构、调用约定或字段语义发生不兼容变化时，必须提升对应 API/命令协议版本，同时更新两端结构尺寸和偏移测试。新增接口不能以相同 ABI 版本覆盖旧函数表；宿主须在任何调用前检查版本及大小。
- ScriptableAsset 统一 archive 类型为 ursa.ScriptableAsset，描述版本初始为 1。托管字段 payload 的 FormatVersion 与脚本类型 Version 是独立概念：前者决定字段封套格式，后者决定项目类型数据契约。未知 archive/payload 版本必须给出诊断，不能猜测解码。
- .ursaasset 源文件的自身 GUID 由 .meta 提供；导入描述包含解析后的 sourceGuid/localFileId、稳定 typeId/typeVersion 和字段 payload。程序集、框架或类型目录变化参与导入指纹，缺失类型仍保留源字段用于编辑恢复。
- Shader 外层资源封套版本、反射/布局产物版本、源语法版本独立演进。当前 `.ursashaderasset` 封套为 v3、Shader archive 为 v4、SPIR-V 反射契约为 v2、绑定策略为 `UrsaBindingMapV5UniformLayout`；Pass、规范化反射和合并后的绑定布局都有稳定身份。导入校验入口、descriptor、uniform block 和顶点输入后才发布资产；源指纹包含源文件、UsePass/fallback 依赖 Shader 内容、include 内容、编译器和格式版本。反射布局或参数频率约定改变时提升对应版本并使旧 Library 产物失效；源 GUID、属性名和有效材质引用保持不变。Player 拒绝不兼容产物，Editor/导入工具从源重建，普通 C++ 构建不准备项目内容。

协议的双端尺寸/偏移和恶意批次测试位于 Tests/Engine/Rendering 与 Tests/Managed/Ursa.ManagedHost.ProtocolTests；资产描述的双格式往返与版本拒绝测试位于 Tests/Engine/ScriptableAssets。
# GPU 调试域

`CommandBuffer.Name` 会作为该次执行的外层 GPU 域；项目可在其内部用 `BeginSample(name)` / `EndSample(name)` 创建自定义嵌套阶段，或用 `ProfilingScope` 在 `using` 结束时配对。样本名称必须非空、有效 UTF-8、无 NUL 且最多 1024 字节。开始/结束必须同名并遵循栈顺序，且都在同一缓冲的一次 Execute 内完成。Clear 会丢弃尚未交付的样本；先前交付的缓冲与名称快照不受改名、Clear 或 Dispose 影响。

`ProfilingSampler` 可以缓存并复用于多个缓冲。首版接口提供 GPU 调试标记，不提供时间统计或 CPU-only 采样。Vulkan 通过可选的 `VK_EXT_debug_utils` 向 RenderDoc 输出标记，独立于 Tracy 与 validation。
