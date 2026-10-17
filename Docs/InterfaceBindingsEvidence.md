# 接口绑定阶段验证记录

日期：2026-10-16。当前记录对应已接入的模块装载、双向封送、21 个 ManagedHost 回调、104 个 Runtime 绑定、15 个 Rendering 绑定、27 个录制命令和 36 个 Editor 绑定；完整构建、项目发布及窗口验收仍单独追踪，不能以阶段测试代替最终验收。

## 构建与执行

| 验证 | 实际结果 |
| --- | --- |
| Release `UrsaDotNetHostTests`、`UrsaBindingsTests`、`UrsaBindingSnapshotTests` | 构建成功，托管编译无警告、无错误 |
| `UrsaBindingsTests` | 真实 C++/CoreCLR 双向调用通过；包含确定性、非法值、结果释放、模块依赖、作用域与回滚 |
| `UrsaBindingsGeneratorTests` | 不同目录/输入创建顺序、失败发布保留、删除输出清理、共享类型变化及只检查模式通过 |
| `UrsaBindingsSignatureTests` | 有效目标编译成功；缺失目标、返回类型错误和同宽度但不同参数类型编译失败并定位 C# 声明 |
| `UrsaBindingSnapshotTests` | 真实 Scene 上下文与生成入口通过；额外 primitive 测试使用内存 VFS 的 mesh/material 资源通过 |
| `UrsaDotNetHostSmoke` | 包含 C# 行为、多个会话、重载、四轮 Run/Stop、旧 token 拒绝、正常 collectible 卸载与故意泄漏后宿主故障；SRP 像素、资产及 Editor 的既有流程通过 |
| `UrsaManagedProtocolTests` | 现有托管包装与字段/资源/渲染协议夹具通过，GameObject 夹具已接生成的表类型 |
| `UrsaProjectStructureTests` | ON/OFF 工程源码登记通过，无 C++ 翻译单元重复编译 |
| clang-format 19 | 全仓检查通过（457 个 C++ 文件，排除生成快照） |

普通标量夹具预热后执行 1000 次调用，`GC.GetAllocatedBytesForCurrentThread()` 差值为 0。该结果只证明所测成功标量路径无托管分配，不代表字符串、返回集合或整体帧时；最终性能任务另行记录。

## 无 SDK 的干净原生构建

`Build/BindingsNativeOnly` 从空目录配置：`URSA_ENABLE_DOTNET=OFF`、`URSA_BUILD_EDITOR=OFF`、`URSA_BUILD_TESTS=ON`、空项目路径、Tracy OFF。启动 configure/build 的进程先从 PATH 移除 dotnet 和 WindowsApps，并确认 `Get-Command dotnet` 无结果。

`UrsaBindingSnapshotTests`、`UrsaManagedHandleTests` 构建成功；两项执行与 `UrsaProjectStructureTests` 共 3/3 通过。构建日志中无 SDK 检查、生成器或托管目标。ON 使用构建树生成头，OFF 使用源码快照；源快照头在 IDE 中登记，不作为第二份翻译单元编译。

Rendering/命令迁移后，再次在移除 dotnet/WindowsApps 的 PATH 下构建该 OFF 目录：增加生成协议的 `UrsaRenderCommandProtocolTests`，与 Snapshot/Handle/Structure 共 4/4 通过，仍无 SDK 或生成器调用。

## 具体行为证据

- 基础 ABI 的实际 CLR 大小和字段偏移参与指纹，native 编译断言逐项核对。bool/uint 相同宽度的共享字段变更仍使模块和原生快照失效；检查模式保留原文件。
- 缺失模块/依赖、重复模块、同大小不兼容指纹、关闭 native 上下文及连接阶段故障均不替换有效视图。会话撤销后旧视图失败，后台撤销同样在下一入口生效。
- 临时模块只能在调用作用域安装；测试嵌套恢复、逆序释放、错误线程释放、切换会话后 Dispose 不恢复旧上下文。
- 字符串包含中文及嵌入零；结果独立于后续调用。native/managed 延迟使用均取得输入副本。畸形数组、非法 UTF-8、借用输入作为输出、解码失败和错误文本预算耗尽均有释放/错误断言。
- 非对称矩阵与平移矩阵的 C#/native 运算逐元素一致，点变换结果为 `(34,95,201)`。
- GameObject 的生成入口验证创建计数、名称、Layer、父子、活动状态、稳定身份和销毁；旧代数、错误种类、跨会话及后台线程失败后原对象不变。primitive 对合法种类创建一次，错误种类无部分创建。
- Component 生成入口验证数组快照、种类与归属、启用状态、持久身份和删除失效；跨会话删除被拒绝。真实宿主继续验证 Behaviour 挂载前的身份绑定、立即生命周期回调和重复运行会话中的隔离。
- Transform 通过非均匀父缩放/平移下的 world/local 换算；Camera 投影保持弧度，错误 near/far 与 viewport 失败后保留原状态。真实宿主的 Forward、自定义管线和相机区域像素测试继续通过。
- 平台绑定验证 Input 的三个阶段、超出 double 精确整数范围的 FrameCount、frame 结束后的 Time 拒绝、Unicode 日志/标题、空/非空原始文件和后续写入不覆盖已有返回值。Scene 仍活着但宿主已关闭时，租约拒绝 VFS 访问。宿主能力由不依赖 CoreCLR 的 native 接口提供。
- Mesh 生成入口验证 65536 个顶点及 UV/颜色/切线、负 baseVertex、三角形、克隆与持久资产装载共享；错误输入和修改返回数组均不改变版本，销毁源不破坏克隆。既有 MeshTests、MeshComponentTests、ManagedMeshApiTests 共 3/3 通过。
- class/struct 的同尺寸布局夹具均为 24 字节，但 X 偏移分别为 4/0；指纹包含 presence 与具体字段偏移，转换后模块身份失效，生成回归通过。
- Material 的生成入口覆盖 Float/Range/Int/Color/Vector/Texture2D 与关键字，验证 Range 失败无修改、Int 保持负数、Vector 不按 Color 限幅、错误 getter 种类拒绝。null 纹理移除覆盖；Renderer 的 null 槽位、实例克隆及跨会话数组失败不部分修改均通过。持久资产禁止 Destroy，实例 Destroy 后 getter/IsValid 失效。既有 MaterialTests/ShaderMaterialTests 通过，真实宿主继续覆盖已导入纹理引用及 ComputeShader 查询。
- ResourceReferenceArchive 的真实生成入口验证持久 Mesh/Material 恢复仍共享原句柄、GUID 保持、未知字段兼容、错误版本/句柄种类拒绝。原脚本字段与图服务未改写；ManagedProtocolTests 中的共享/循环图、未知字段及缺失引用诊断和 DotNetHostSmoke 中的资产转移、失败候选保留与重开通过。
- Rendering 生成入口验证两台相机共享不可变快照，旧会话/帧/index/generation、Abort 后旧 CullingResults 均失效。`UrsaRenderingBindingsTests` 用真正导入的 ComputeShader 分派并读回 64 个 uint 和 RGBA8 像素；覆盖 HDR 双颜色附件、独立 D32 深度、创建容量、帧内读回拒绝、提交前 Release 后 native/GPU backing 存活及拥有输出独立于资源释放。
- `RenderingBindingsProbe` 经真实 CoreCLR 调用 Matrix4x4、ComputeShader、GraphicsBuffer 和上下文生成绑定；非对称矩阵在两种 HLSL major 声明下均读回 `(27,71,115,159)`。ShaderReflectionTests 验证字段布局及跨 stage 冲突，错误写入不改 backing；RenderCommandExecutorTests 的 HDR/MRT、相机重置、窗口缩放/最小化恢复及每帧一次呈现继续通过，DotNetHostSmoke 的默认/自定义管线和相机区域像素继续通过。
- 录制命令夹具执行 C# 编码→native 解码/编码的逐字节对照，覆盖嵌套 DTO、UTF-8/嵌入零、集合、非对称矩阵和 UInt64.MaxValue；输入后改动不影响批次，编码失败撤销本条记录。两侧拒绝截断、错误身份、opcode/长度、非法 bool、溢出集合及非零填充；native-only 在项目路径拒绝，native 路径可用。解码 arena 预算耗尽后下一次有效批次仍成功。
- 生产 CommandBuffer 与 native 服务均接生成协议，旧 payload 偏移解析和 v2 validator 已删除。生成资源/GPU 测试在有效清屏命令后放置过期缓冲引用或错误批次身份，验证拒绝后接受计数和 GPU 纹理原样不变。Clear/Dispose、临时目标、释放后的 backing 保留、HDR/MRT/Compute、多段 Submit、默认/自定义管线均通过对应回归。
- 真实宿主分别绘制 1 和 64 个对象，每种情况 3 帧均为 6 次命令批次 ABI 提交（每帧两段）；两次均检查了相同的绿色几何/蓝色背景像素。该计数仅测批次提交，其他相机/剔除/列表绑定也按列表调用，不随列表内对象数增加；未把它作为总体帧耗时或零分配证据。

覆盖矩阵中的“完成”表示相应生产调用已切换并有行为验证。旧 Bootstrap 业务入口、NativeApi 表字段、通用资源分派、Editor exchange 和跨调用错误缓存已删除；测试改接生成入口，未增加 JSON fallback。

## Editor 强类型迁移

UrsaEditorBindingsTests 通过生成入口验证 64 位整数/枚举、数学值/空字符串描述、列表扩容/元素顺序、错误访问器、引用种类、跨会话、旧 revision/token、错误线程和目标删除。错误事务不写入目标，Update 丢弃编辑，冲突快照不能覆盖外部修改。内存 AssetDatabase 发布夹具验证 Mesh/Material/Texture/ScriptableObject 子资产的种类、GUID/local file ID 和选择描述；该夹具验证目录元数据，不代替资源加载回归。

UrsaDotNetHostSmoke 的真实 ImGui 帧覆盖 Vector2/3/4、旋转、颜色、对象/资产控件、菜单保存、C# 匹配访问器/Update 与布局异常清理。Scene 和独立 Prefab 均通过 EditorDocument 保存并重新打开，核对数值、未知字段和稳定对象/组件 ID；关闭并重建扩展会话不保留旧属性或委托。一次重复会话选择清空临时模块的问题和控件返回借用字符串的所有权问题均由该测试发现并修复。

## 构建与内容身份

ScriptBuildTests 在项目源码不变时修改隔离的 Ursa.Bindings 支持程序集输入，验证 SourceFingerprint/缓存失效、重新编译、新内容指纹及 RequiredBindings 发布；篡改模块指纹后 Run 就绪检查失败，重新构建修复清单。该程序集夹具添加 PE 尾部数据以避免改变业务 API；实际声明/共享布局改变模块身份由生成器夹具另行证明。DotNetHostSmoke 的四轮运行均拒绝缺少和错误模块身份后再加载有效内容；C# 元数据校验也覆盖缺失、错误名称/哈希以及 Game/Editor 要求隔离。

Demo 已完成 Release 的 Both 导入与 Game/Editor 编译：Game 清单为 Version 2 且只有 ManagedHost/Rendering/Runtime，Editor 清单额外包含 Editor。只复制 Assets 到 Build/TestProjects 的新目录、初始无 Library，完整导入得到 Editor imported=25/cacheHits=0，Player imported=0/cacheHits=22；生成项目和程序集均在 Library。后续最终 ABI 清理后的重建结果继续补记。

旧表清理后，真实宿主/托管协议 3/3 通过；ManagedSceneApi、ManagedAssetApi、ManagedMeshApi 已直接调用生成方法并通过：保留变换、父子、Camera、资源能力/生命周期、Scene/Prefab 资源引用、RendererList、MRT/copy、Compute 与 GPU 读回断言。固定引导空指针/截断/版本用基础 Bootstrap 夹具检查，不再验证已移除的旧表布局。

预热 10000 次后的本机 Release 记录：标量 10000 次 0 GC 字节、400.0 ns/次；Matrix4x4 10000 次 0 GC 字节、715.6 ns/次；8192 个 Point 往返 20 次共 3935200 GC 字节、0.548 ms/次，返回数组为独立副本，所有 native outcome/arena 已释放。计时包含当前封送、调用和校验，属于单机诊断数据；并发构建时曾测到标量 6273 ns，复测消除了该调度/预热干扰，未据此声称相对旧实现的速度提升。

## 最终项目与窗口验收

最终 21 方法 ManagedHost 契约下，Demo C#/.meta 输入哈希均未改变，Game BuildFingerprint 从 d17409e761fbf6cd 变为 efbcbf6f7635dbf8，RequiredBindings 的 Host FunctionCount 从 20 变为 21。Both 导入成功；再次从只复制 Assets、无 Library 的 BindingsFinal 项目导入得到 Editor imported=25/cacheHits=0，Player imported=0/cacheHits=22。

项目扩展注册/重载、玩法 Run/Stop 与 Scene/Prefab 往返连同准备步骤共 6/6 通过。ManagedEditorWorkflowTests 单独重跑 83 秒通过，检查编译失败恢复、保留未保存字段、重载和三轮 Run/Stop、最终保存；与并发构建同跑的一次超时不计通过。EditorSrpAcceptanceTests 已通过（203 秒，加准备共 218 秒），实际创建/保存管线资产、配置管线、窗口/渲染/重载各阶段截图在 Build/TestProjects/SrpAcceptance/Release/79b37127-a866-4a02-8bd1-22742364e479。首次失败定位到共享 Editor 模块初始化尚未运行，已在 LoadEditorApi 执行模块注册后重跑通过。

新增固定地址组件分配夹具，在 native 删除/重建组件后分别先访问旧句柄、先注册新句柄，验证旧入口拒绝、替换对象取得新代数；ResolveComponent 在确认当前 Scene 成员及 ComponentId 后才解引用，避免读取已释放指针。GameObject 注册也核对原弱引用以处理 native 地址复用。

归档顺序：本 change 以已完成但未归档的 scriptable-render-pipeline 为基线；应先将该 SRP change 的 delta specs 同步到主规格，再归档 SRP。当前仅实施绑定 change，不自动归档或改写 SRP 渲染功能规格。

最终 Release 核心回归 14/14 通过，包含生成器/签名/快照、Editor、旧测试迁移、矩阵/GPU 与源文件登记。当前最终程序再次执行 Snake/MaterialShowcase 的松散与 Pak 各 8 帧，正常退出，无新增 Vulkan 错误；两组 PNG SHA-256 分别完全一致：Snake 25fd9054c00b08509c1cb5e213beaea3e4a02567d1411a6ca3c77a97b682114f，MaterialShowcase f73531b7fd01849296ea8e7dc9c1e383eb3ad00c84af0f8126a34ce6f9d08e08。默认/自定义管线及 Scene/Game/Preview 的像素与资源保留同时由 DotNetHostSmoke、EditorSrpAcceptance 和 RenderingBindings/RenderCommandExecutor 断言。

构建矩阵：BindingsFinalOff 在 PATH 移除 dotnet/WindowsApps 并确认 dotnet 不可发现后完成 Debug/Release，两个配置各 6/6（Handle/Snapshot/Mesh/Asset/Command/Structure）通过。BindingsFinalVerify 从新目录完成 ON + Editor ON 的 Debug 构建，12/12 对应检查通过。系统的 MSVC PDB 管理器出现 C1902，固定工具集仍未解决；按现有 ManagedScripting 文档的等价嵌入符号方式，在验证目录使用 /Z7（/Od /RTC1 保持），没有修改仓库默认 Debug 配置。同目录 Release 原生/托管构建完成，13/13 对应检查通过，包含新增的原生属性归一化回归。

最终边界回归：所有 Editor GUI 入口先校验 EditorInvocation，活动 GUI 帧中使用 Runtime Scene token 也被拒绝；未编辑的 TextField 保留嵌入 NUL 的原字符串。原生属性 Apply 取得实际保存结果作为新快照，Camera 的 Double 输入转 Single 后不会使下一次合法 Apply 误报冲突；Root Release 的 SerializedObject/EditorBindings/Host 加准备共 4/4 通过，干净 Release 组也包含相同断言。

所有标量/数学测量都在已选定模块视图与预热后的成功调用中进行；不包含会话切换、模块重新验证或完整帧的分配。数组测量包含输入封送及独立返回数组的复制，不能解释为总分配为零。

最终收尾：干净 ON Release 13/13 通过（含 SerializedObject 新增断言）；Debug 对变更后的 Editor/SerializedObject/Host 重建并执行 4/4 通过，之前的 Debug 12/12 保持。clang-format 19 全仓 462 个 C++ 文件通过，生成快照 --check、确定性/签名夹具和 OpenSpec strict 校验均通过。Git 差异只含计划中的 Engine 源码、生成快照、测试/文档与 Demo 两个 Editor C# 文件；Demo .meta 无差异，根 TODO.md 为用户原改动，未纳入改造。
