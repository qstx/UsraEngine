# Mesh、Filter 与 Renderer

## 数据与寿命

`Mesh` 是可修改的 CPU 资源，与 Vulkan、导入器及 GPU 缓冲区无关。每次成功写入发布不可变快照并递增版本；失败保留原数据与版本。读取通道和 `GetData` 返回副本。完整解码数据最多 256 MiB，子网格最多 4096 个，只支持三角形和 32 位索引。

位置是必需通道。未提供法线、UV0、颜色或切线时，绘制使用向上法线、零 UV、白色和 `(1,0,0,1)`；需要真实表面法线时调用 `RecalculateNormals`。切线 W 必须为 +1 或 -1，控制法线贴图的副切线方向。空网格不产生绘制，也不会替换成立方体。

运行时身份单调递增，不以 C++ 地址识别资源。`Clone` 保留几何、蒙皮权重和骨骼资产引用，产生新的运行时及本地身份，不继承资产 GUID。`Destroy` 仅允许释放非持久资源；之后旧句柄、读写和绑定均失效。脚本 API 使用会话、资源种类和代数检查；结束会话会释放其持有的运行时资源。组件不自动销毁共享资源，使用者应显式释放不再需要的实例。

## 组件与共享

一个 GameObject 最多有一个 MeshFilter 和一个 Renderer（静态或蒙皮）。MeshFilter 是数据组件，没有 Enabled 开关；Renderer 可以启用或禁用。

`MeshFilter.SharedMesh` 返回当前绑定，不复制数据。首次访问 `Mesh` 会克隆已有绑定，或创建空网格，再将副本绑定到 SharedMesh；重复访问返回同一实例。重新设置 SharedMesh 会重新开始这项规则。Prefab 源对象禁止隐式实例化，可显式 Clone 后赋值。

Renderer 的 `SharedMaterial(s)` 操作当前槽，不自动克隆。`Material` 首次访问只克隆第一槽，`Materials` 克隆尚未实例化的各槽；空槽保持空值，数组 getter 返回数组副本。显式 `Material(s)` 赋值保留已经独立的运行时实例。缺少或空槽在绘制时使用默认材质，不改写保存的槽；额外材质按顺序重绘最后一个子网格。

SkinnedMeshRenderer 直接绑定 Mesh，不需要 MeshFilter。骨骼 GUID/local file ID 独立于 Mesh 自身身份，用来匹配 Animator；没有 Animator 时使用绑定姿态。克隆或另存 Mesh 保留该骨骼引用。

## 帧内更新与 GPU

在主线程的 Update/LateUpdate 或绘制收集之前修改 Mesh。场景收集保留当前快照；已收集或提交的绘制使用该快照，下一次收集才看到新版本。GPU 缓存按运行时身份和版本上传，并在提交完成栅栏之后回收旧版本及已释放对象。不要在命令提交过程中修改同一份快照数据。

## 文件与文档资源

`.ursamesh` 保存版本、GUID、local file ID 和 Mesh Archive 数据。独立资产的 local file ID 为 0；模型子资产保留原来的非零 ID。JSON 与二进制使用相同字段，二进制增加长度校验封套。资源读写经过 VFS。

Scene/Prefab 使用 `localResourceVersion: 1` 与 `localResources` 表保存无资产身份的 Mesh/Material。同一对象仅保存一次，引用为 Asset、Local 或空值；稳定本地 ID 支持重复保存和 Prefab 覆盖。加载先恢复资源，再绑定组件，任何缺失或错类型引用都拒绝交付对象图。Prefab 实例化会重映射本地身份并保持实例内共享关系。

Run 使用独立资源会话，复制可修改的资产 Mesh/Material，并恢复独立的文档本地资源；停止后释放运行资源，编辑对象与 Dirty 状态保留。

## C# 示例

```csharp
var cube = GameObject.CreatePrimitive(PrimitiveType.Cube);
var filter = cube.GetComponent<MeshFilterComponent>();
var renderer = cube.GetComponent<MeshRendererComponent>();
var mesh = filter!.Mesh; // 缓存一次，后续修改同一份实例
var material = renderer!.Material!;
material.Color = new Color(0.2f, 0.8f, 0.5f);
var positions = mesh.Vertices;
positions[0].Y += 0.2f;
mesh.Vertices = positions;
mesh.RecalculateNormals();
mesh.RecalculateBounds();
// 销毁对象后，在不再共享这些资源时显式调用 mesh.Destroy()/material.Destroy()。
```

旧 BoxRender、旧 Renderer 内嵌网格/单材质字段由本次迁移工具转换；正式注册表和托管 API 已移除旧类型；未迁移数据会返回类型/版本诊断。

## 迁移与编辑器操作

```powershell
Engine/Build/Debug/UrsaMeshMigrate.exe --project Demo --dry-run
Engine/Build/Debug/UrsaMeshMigrate.exe --project Demo
Engine/Build/Editor/Debug/UrsaAssetImport.exe --project Demo --configuration Debug
```

迁移工具先按当前导入器重建模型缓存，再验证所有候选 Scene/Prefab 图。`--dry-run` 不写场景/Prefab；模型导入元数据及 Library 仍会更新。实际迁移保留原组件 IDs，新 Filter/渲染子对象使用可重复生成的 IDs；原文件备份位于 `lib:MeshMigration/<id>/`。旧颜色合并到本地材质，Prefab 颜色/材质覆盖变为本地资源覆盖。脚本里的旧类型及属性由项目代码迁移，新资产 GUID 和既有 `.meta` 不应手工删除。

Inspector 中 MeshFilter 与 Renderer 分开显示。通过 Mesh picker、Project 拖放或 Model Import 中的网格子资产拖放分配几何；面板显示顶点、索引、子网格、版本和边界。材质槽支持 Size、排序、清空、资产选择以及显式 Make Local Copy。持久材质的参数通过材质资产编辑器修改，组件面板编辑独立副本；缺少/空槽和额外槽会给出提示。

Save Mesh As 写入新的项目 `.ursamesh` 及 `.meta`，可显式替换当前绑定。内置/导入网格保留源身份，另存对象取得新 GUID；只读路径、重名及保存失败不会替换绑定。Scene 中的 Prefab 实例提供 Apply/Revert：Apply 更新可写源文件并刷新实例，Revert 恢复当前源；共享几何修改、材质槽和本地资源覆盖均参与这项操作。

Run Inspector 修改运行图。重复 Run/Stop 使用新的会话，运行资源不泄漏到编辑文档。希望跨停止持久化的运行时网格应显式另存为资产；正常场景保存保留编辑图。

## 实施验收记录

2026-10-12，Windows x64、Visual Studio 2022，启用 Editor、.NET 和 Tracy：

| 验收项 | Debug | Release |
| --- | --- | --- |
| 全量 CMake 构建 | 通过，无编译警告 | 通过，无编译警告 |
| 全量 CTest | 124/124 通过 | 124/124 通过 |
| 实际 Demo 显式导入及 Snake、规则冒烟、材质 Player | 通过 | 通过 |
| 实际 Demo Snake/材质 Editor 启动 | 通过 | 通过 |
| 实际 Demo pak 中的 Snake/材质 Player | 通过 | 通过 |
| 实际 Demo 原生窗口网格编辑流程 | 通过 | 通过 |
| 内存工作负载 benchmark 启动及回收检查 | 通过 | 通过 |

原生窗口回归覆盖模型实例化及其导入面板、MeshFilter/Renderer Inspector 绘制、修改独立几何与材质槽、另存 Mesh、Scene/Prefab 保存重开、两次 Run/Stop、运行资源隔离与编辑 Dirty 恢复。测试创建的资产和目录在结束时清理，并重新准备项目内容。Player 回归包含窗口缩放、最小化、恢复和关闭。

CPU/GPU 回归同时覆盖动态 Mesh 版本及旧快照、GPU 缓存回收、额外材质槽、负 baseVertex、切线方向、蒙皮网格另存后的独立骨骼引用、动画播放、Prefab 本地资源 Apply/Revert、C# 资源句柄和生命周期前字段恢复。验收未发现 Vulkan 验证错误。

clang-format 19 检查通过；VS 工程结构检查核对 118 个第一方工程和 399 条 C++ 文件登记，目录与 filters 一致。第三方子模块提交保持不变。
