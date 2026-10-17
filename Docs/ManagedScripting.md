# C# 托管宿主

本文说明当前可选的 C# 宿主如何配置、构建，以及宿主启动失败时如何定位问题。

## 支持范围

- 当前宿主仅支持 Windows x64。
- 需要安装 .NET 10 SDK 和 `Microsoft.NETCore.App 10` 运行时。关闭脚本时，引擎不要求安装 .NET。
- 原生宿主通过 .NET host pack 中的 `nethost` 定位 `hostfxr`，再由 `hostfxr` 初始化 CoreCLR。`URSA_ENABLE_DOTNET` 默认关闭。
- 当前版本在一个进程内只允许初始化一次 CoreCLR；CLR 不支持在进程内完整卸载，因此关闭或重建游戏脚本会话不应再次初始化运行时。

## 配置与构建

从 `Engine` 目录配置：

```powershell
cmake -S . -B Build -DURSA_ENABLE_DOTNET=ON
cmake --build Build --parallel --config Debug
ctest --test-dir Build -C Debug --output-on-failure
```

发布构建将 `Debug` 替换为 `Release`。若要验证不依赖 .NET 的原生构建，配置时使用 `-DURSA_ENABLE_DOTNET=OFF`。

配置会通过 `dotnet --list-sdks` 和 `dotnet --list-runtimes` 检查 SDK 与运行时，并根据已安装的 .NET 10 运行时定位 Windows x64 host pack。若机器上存在多个 `dotnet.exe`，可显式指定：

```powershell
cmake -S . -B Build -DURSA_ENABLE_DOTNET=ON -DURSA_DOTNET_EXECUTABLE="C:/Program Files/dotnet/dotnet.exe"
```

切换 `dotnet.exe` 后若 CMake 仍使用旧路径，请在重新配置时增加 `-UURSA_DOTNET_EXECUTABLE`，清除缓存的程序路径。

启用后，CMake 会验证 host pack 中的 `nethost.h`、`hostfxr.h`、`coreclr_delegates.h`、`nethost.lib` 和 `nethost.dll`。托管工程构建在 `Engine/Build/runtime/Scripting/Managed/Source` 的构建暂存目录中进行；程序集输出到 `Engine/Build/Managed/<配置>`，不会把 `bin/` 或 `obj/` 写入源码目录。原生宿主目标链接 `nethost.lib`，可执行程序旁会复制对应的 `nethost.dll`。

## VFS 与原生宿主边界

托管引导程序集、runtimeconfig、deps 清单及依赖均使用 VFS 虚拟路径读取。内容可以来自松散目录或 pak。启动前，宿主按文件名和内容计算稳定指纹，并把文件写入可写的 `user:ManagedBootstrap/` 缓存。相同内容复用同一缓存目录；源路径仍由 VFS 解析，宿主不会把 `ursa:` 或 `prj:` 路径直接交给操作系统。

只有 `hostfxr` 初始化 runtimeconfig 和加载入口程序集时才需要原生磁盘路径。此时 VFS 只允许从实际可写的 `user:` 目录挂载中解析路径，并再次检查路径仍在挂载根目录内。只读缓存、pak 提供者或逃逸挂载根目录的路径都会返回错误。

目前的宿主引导文件总大小上限为 256 MiB，文件名必须唯一；重复路径、重名依赖、缺失文件和缓存写入失败都会使初始化失败。不要手动清理运行中进程正在使用的引导缓存；进程退出后可以删除 `user:ManagedBootstrap/` 下不再需要的内容。

托管代码通过 `AssetFileSystem.ReadAllBytes("ursa:...")` 读取 VFS 内容，通过 `AssetFileSystem.WriteAllBytes("user:...")` 写入可写挂载。写入权限由 VFS 挂载访问模式决定，写入只读 pak 或只读挂载会返回失败；发行版的持久数据应写入 `user:`。`Ursa.Debug` 提供 Trace 至 Error 级别日志，日志消息以 UTF-8 传递。`Input` 与 `Time` 读取当前帧快照，生命周期集成应在每次托管回调前设置对应的 `FrameContext`。

## 项目脚本与编译

IDE 工程与正式脚本构建的输入模型、生成目录和输出隔离见 [C# 工程与外部编辑器](CSharpIdeIntegration.md)。IDE 的结构指纹与包含源码内容的正式构建指纹分别管理；打开 IDE 工程不要求脚本语义校验成功，IDE 构建不会替代正式内容发布。

启用 `URSA_ENABLE_DOTNET` 后，项目 `Assets` 中的 `.cs` 文件由 AssetDatabase 自动发现并维护 `.meta` GUID。路径中包含独立目录段 `Editor` 的脚本归入 Editor 程序集；其余脚本归入 Game 程序集。Game 程序集引用 `Ursa.Runtime`/`Ursa.Rendering`，Editor 额外引用 Game 与 `Ursa.Editor`；`Ursa.Bindings` 为引擎共享支持依赖，Player 不加载 Editor 脚本或 Editor 模块。

Editor 会在项目的 `Library/ScriptBuild` 下生成 MSBuild 工程、脚本类型目录、编译暂存区和输出，不需要手工维护 `.csproj`。在编辑器中使用 `Scripts > Compile Scripts` 编译 Game 与 Editor 程序集；编译在后台运行，诊断显示在 Script Build 面板。Run 会检查脚本输入指纹；脚本尚未编译或编译后输入有变化时，Run 会后台编译最新输入，成功后才启动运行场景。清空项目 `Library` 后，下一次编译会重新发现脚本、生成目录并构建程序集。

每个公开、非抽象、非泛型的 `Behaviour` 派生类型都会加入 ScriptType 目录。推荐使用显式稳定标识，类型改名时保留原标识：

```csharp
using Ursa;

[UrsaScriptType("sample.movement", Version = 1)]
public sealed class MoveWithInputBehaviour : Behaviour
{
    public float step = 1.0f;
}
```

未标注特性的类型会使用源 GUID 和 CLR 类型名生成默认标识；重命名这类类型会改变标识，因此用于场景或 Prefab 的脚本应显式声明 `UrsaScriptType`。编译缓存指纹包含源码、分组、运行时 API、.NET SDK 和引用程序集；编译期间输入发生变化时，暂存产物不会发布。编译错误不会覆盖最近一次成功构建的程序集。

## 脚本字段、Scene 与 Prefab

公开实例字段默认保存；私有字段可使用 `[SerializeField]` 加入保存。静态、常量、只读、`[NonSerialized]` 字段和属性不会保存。当前支持数字、布尔值、字符串、枚举、`Vector3`、`Quaternion`、`Color`、`AssetReference`、`GameObject`/`Component` 引用，以及这些类型的一维数组和 `List<T>`。要求保存但类型不受支持时，保存会报告脚本类型和字段名，不会悄悄写入默认值。

Scene 和 Prefab 会保存脚本的稳定 `UrsaScriptType` 标识、类型版本和字段值。对象、组件和资产引用通过持久 ID 保存；加载 Scene、Prefab 或 Run 副本时，引用会映射到新对象图，完成后才调用 Awake。Prefab 实例覆盖单独记录字段值，因此修改一个实例不会改写 Prefab 源。

在 Hierarchy 选择 GameObject 后，从 Inspector 的 **Add Component > Scripts** 挂载脚本。Inspector 根据字段类型显示数字、布尔值、字符串、枚举、向量/颜色、引用和数组/列表控件。修改后保存 Scene 或 Prefab；重新打开时字段值仍会保留。选中 Prefab 实例修改脚本字段并保存 Scene 时，Editor 记录实例覆盖；Prefab 源文件保持不变。

字段改名时保留脚本类型标识和版本，并在新字段上声明旧字段名：

```csharp
using Ursa;

[UrsaScriptType("sample.movement", Version = 2)]
public sealed class MoveWithInputBehaviour : Behaviour
{
    [FormerlySerializedAs("moveStep")]
    public float step = 1.0f;

    [SerializeField]
    private string label = "Mover";
}
```

加载时新字段优先；如果新字段没有保存值，系统会从 `moveStep` 迁移。再次保存会写入 `step`，并把无关修改中尚未识别的旧字段原样保留。Prefab 中对应的字段覆盖路径也会从旧名称迁移到新名称。

如果脚本类型暂时不存在，Scene/Prefab 仍会保留其类型名、版本和字段 payload，保存不会删除这些数据。重新编译后，兼容的类型和字段可以恢复；运行时找不到或版本不兼容的脚本会阻止进入 Run，并显示启动诊断。类型版本用于拒绝不兼容的数据结构，不要复用旧版本号表示不兼容变更。

## 编辑重载和 Run/Stop

编译成功后，Editor 创建候选可回收上下文，按稳定脚本身份恢复当前 Scene 或独立 Prefab 的字段。已保存字段覆盖构造默认值，新字段使用默认值，未知字段继续保留。候选字段恢复或版本校验失败时，旧编辑环境和未保存文档保持可用；修复源码后再次编译即可重试。

每次 Run 使用独立的游戏加载上下文，仅加载 Game 内容及其依赖，项目静态字段从初始状态开始。运行中修改源码不会替换当前游戏程序集；Stop 后再编译重载。运行副本包含未保存的编辑字段与引用，运行修改不写回编辑文档。

Stop 先停止更新并清理运行场景，再失效对象句柄、释放行为 token 和会话注册，最后卸载上下文。宿主最多执行 8 轮 GC／终结器检查。若用户代码把项目实例或委托挂到进程级事件、后台任务等外部强引用中，检查可能失败；Editor 会保留编辑文档，并禁止新 Run 或重载。保存文档、移除泄漏来源并重启 Editor 后恢复。普通编译错误不需要重启。

程序集、PDB 和脚本类型目录由 VFS 内容清单加载，并校验各文件指纹。编译产物先写入不可变内容目录，再发布清单。清单路径使用虚拟路径；Player 会话拒绝 Editor 组内容。引导 API 的物理缓存仅用于 CoreCLR 平台接口。

## 游戏 API、线程与句柄

游戏行为继承 `Ursa.Behaviour`。`GameObject` 提供对象名称、活动状态、父子关系、Transform 和组件添加/查询；纹理与材质通过 `AssetReference` 的 GUID 和 local file ID 加载。原生对象始终由 Engine 所有，C# 包装只携带带会话 ID、类型和 generation 的不透明句柄。对象或组件销毁后，旧包装访问会报失效句柄，不会重新指向复用的槽位。

场景、组件、资源和 Editor API 仅能从主线程访问。后台任务可以做纯 C# 计算；提交场景修改前应回到主线程。生命周期顺序与原生组件一致：Awake、OnEnable、Start、FixedUpdate、Update、LateUpdate、OnDisable、OnDestroy。运行中添加组件会在安全边界纳入场景；移除或销毁会先派发生命周期清理，再释放实例 token。

下面的行为响应右方向键并移动一个本地单位：

```csharp
using Ursa;

[UrsaScriptType("sample.movement", Version = 1)]
public sealed class MoveWithInputBehaviour : Behaviour
{
    public float step = 1.0f;

    public override void Update()
    {
        if (!Input.GetKeyDown(KeyCode.Right))
        {
            return;
        }

        Vector3 position = Transform.LocalPosition;
        position.X += step;
        Transform.LocalPosition = position;
    }
}
```

将文件保存到项目 `Assets/Scripts/MoveWithInputBehaviour.cs`，在 `GameObject` 所属的运行场景中挂载该行为，然后通过 `Scripts > Compile Scripts` 编译。Update 从当前帧输入快照读取按下沿，并把 Transform 修改写回原生场景。`Ursa.ManagedHost.ProtocolTests` 内包含相同移动行为的混合运行冒烟用例；用例从 pak 读取托管程序集，并验证输入确实改变了原生 Transform。公共 C# API 不包含 RHI 或 Vulkan 类型。

## 常见故障

- **提示需要 .NET 10 SDK/runtime**：确认 `dotnet --list-sdks` 中有 10.x SDK，且 `dotnet --list-runtimes` 中有 `Microsoft.NETCore.App 10.x`。仅安装 SDK 或仅安装运行时都不满足构建要求。
- **找不到 host pack 文件**：安装 Windows x64 的 .NET 10 SDK/runtime，确认 CMake 选择的 `dotnet.exe` 属于该安装目录，然后重新配置。不要从其他机器复制不同版本或架构的 host pack。
- **找不到 `hostfxr` 或无法初始化 CoreCLR**：确认目标机器安装了 .NET 10 Desktop/普通运行时中的 `Microsoft.NETCore.App`，并检查传入 runtimeconfig 是否与打包的运行时版本匹配。宿主不会自动下载运行时。
- **托管引导文件读取/缓存失败**：检查 `ManagedHostFiles` 中的 VFS 路径确实存在，并确保 `user:` 指向可写目录且剩余空间足够。pak 中的 `ursa:` / `prj:` 内容只作为读取来源。
- **报告进程已初始化运行时**：同一进程不能第二次启动 CoreCLR。重试前退出当前进程并重新启动应用；不能通过销毁 `DotNetHost` 对象来卸载 CLR。

## 验证

启用脚本的 Debug 构建会运行 `UrsaDotNetHostSmoke`、`UrsaManagedProtocolTests` 和 `UrsaManagedHandleTests`。宿主冒烟测试从 pak 读取引导内容、写入 `user:` 缓存，再通过 `hostfxr` 启动并验证托管到原生的 UTF-8 日志回调。

## 枚举持久化

枚举字段及其数组／列表保存整数值，FieldTypes 同时记录枚举类型、底层整数类型、显示名称与各名称对应的数值。Inspector 显示名称，Apply 根据底层类型校验范围；允许底层范围内尚未命名的值（含 Flags 组合），不会因成员名称改动而改变数据。超出 byte、short 等底层类型范围的输入被拒绝。

## 无界面编译与发布

`UrsaScriptCompile <项目目录> [Debug|Release]` 使用与 Editor 相同的发现、类型目录和后台编译服务，供发布流程和自动验证使用。它编译 Game 与 Editor 两组；`UrsaPak` 只打包 Game。详细命令见 [脚本发布](ManagedDeployment.md)。

本机出现 Visual Studio 的 C1902/PDB 管理器版本不匹配时，可以重新配置构建目录并加入 `-DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`。这将调试信息放入对象文件；它是工具链问题的规避选项，并非脚本系统的运行要求。

`Transform.Position` 读写世界位置（父对象包含旋转或非均匀缩放时，通过父世界矩阵的逆转换成本地位置）；`LocalPosition`、`LocalRotation` 和 `LocalScale` 编辑本地数据。`GameObject.Destroy()` 在游戏回调中标记待销毁，并在阶段安全边界清理。桥接由 `.bindings.cs` 生成的 Runtime/Rendering/Editor/ManagedHost 模块提供；固定 InitializeBindings 引导校验完整身份与依赖，旧表已移除。对象/组件句柄仍校验会话、代数、种类及所属线程；native 地址复用不能让旧句柄指向替换组件。

完整构建矩阵、自动测试和人工交互证据见 [验收记录](ManagedVerification.md)。
