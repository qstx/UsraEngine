# 项目资源导入管线

## 缓存与身份

项目资源生成数据通过可写 `lib:` 管理，物理目录为项目 `Library`。内置源由只读 `ursa:` 提供，项目源由 `prj:` 提供；内置资源的产物位于项目 Library，不改变源 GUID 或逻辑归属。

`ProjectWorkspace::InitializeBuiltinAssets()` 在已挂载的内置源上建立只读数据库，与项目数据库共用项目缓存归属。`BuiltinDatabase()` 用于查询内置元数据及导入记录；`AssetResolver()` 统一按 GUID/local file ID 分派。缺失内置元数据返回诊断，不能通过 Editor 写回引擎来源创建或修复身份。

导入索引位于 `lib:AssetDatabase/<scheme>.json`，当前缓存格式版本为 4。原版本缓存不会被宣布为当前结果，可从源和元数据重新导入。`CheckImport` 按源身份及目标查找记录，并比较源、元数据、依赖、设置、导入器版本及输出完整性；不同目标保留各自的记录。命中某目标后，在当前数据库中选择其产物供后续解析；成功发布将选择状态持久化。

`PublishImport` 将产物写入 `lib:Artifacts/<输入指纹>/`，记录内容指纹与字节数。删除或损坏输出将导致缓存失效，可以重新导入恢复；失败发布不替换持久索引。项目之间的数据库及产物互相独立。

## 运行内容目录

`RuntimeContentSnapshot` 记录目标、程序配置、用途、输入指纹以及从逻辑虚拟路径到产物存储路径的映射。`RuntimeContentManifestPath` 生成以下路径：

```text
lib:RuntimeContent/<目标>/<配置>/<用途>/Ready.json
```

引导目录使用 `EditorBootstrap`，与完整的 `Editor` / `Player` 记录分开，启动准备不会覆盖已有完整结果。就绪记录还包含完整源文件清单，新增/删除文件也会使开发运行状态失效。

`CaptureRuntimeContentInput` 经 VFS 记录输入。`PublishRuntimeContent` 核对输入和全部输出，并原子写入就绪记录；输入变化或产物缺失时保持上一份记录。`ReadRuntimeContent` 校验结构、目标、配置和输出，传入源文件系统时也验证输入时效。诊断提供显式导入指引。

`OpenRuntimeContentView` 提供只读 `ursa:` / `prj:` 视图，映射到项目产物，不公开 `lib:`。其存储文件系统及产物代际须在读取期间保持可用；导入方不能原地更新正在使用的代际。就绪记录和编译缓存属于开发私有数据，不是发行内容。

## 项目写入协调

导入服务在更新元数据、索引或就绪目录前持有 `AcquireExclusiveLease(lib:Import.lock)` 的返回对象。另一写入方收到占用诊断，持有对象释放或进程退出后可再次获取。目录来源由 VFS provider 使用操作系统文件共享/锁规则实现，内存来源提供对应测试语义；只读挂载不能获取写租约。

协调租约与内容校验是两项独立职责：租约防止同项目写入互相覆盖；输入指纹和不可变产物负责拒绝过时数据及保持读取一致。发布 API 的调用者必须在整个读写事务期间持有租约，而不是只在最后一次写入时获取。

## 无界面导入与运行

在工作区根目录执行（Windows x64，程序配置与导入配置必须一致）：

```powershell
cmake --build Engine/Build --config Debug --parallel --target UrsaEditor UrsaScriptPlayer UrsaAssetImport UrsaPak
Engine/Build/Editor/Debug/UrsaAssetImport.exe --project Demo --configuration Debug
Engine/Build/Debug/UrsaScriptPlayer.exe --project Demo --scene Snake/Snake.ursascene
```

默认准备 Editor 和 Player 两个目录。再次执行会复用有效产物；Debug/Release 的托管部署独立，等价 Shader 字节码共用同一项目编译缓存。工具并不创建窗口或 GPU。可指定 `--engine <内置 Assets>`、`--target WindowsVulkan`、`--profile Editor|Player|Both`、`--scope prj:Textures` 和 `--force`。范围导入要求范围以外已有有效产物，否则明确提示先完成全量导入。

```powershell
Engine/Build/Editor/Debug/UrsaAssetImport.exe --project Demo --scope prj:Textures/UrsaEngine.png --force
Engine/Build/Editor/Debug/UrsaAssetImport.exe --project Demo --profile Editor --scope ursa:Shaders/Editor/ImGui.ursashader
```

首次缺少 SDK 编译工具、源格式错误、依赖缺失、同项目导入锁被占用时退出码为 1；参数错误为 2。修复输入后重新执行相同命令。失败不会覆盖上次完整就绪记录。有效 Shader 缓存不启动编译器；只有失效或强制导入才检查工具。不同项目不会共享 Library。

## 编辑器

```powershell
Engine/Build/Editor/Debug/UrsaEditor.exe --project Demo --scene Snake/Snake.ursascene
```

打开项目时先在项目 Library 准备 ImGui、字体与预览引导资源，再创建界面和准备初始内容。普通源资源默认自动刷新，每两秒检查一次指纹，恢复焦点时补扫；Assets 菜单可关闭 Auto Refresh、Refresh 或 Reimport All。保存、移动、导入设置和 Reimport 请求进入同一队列。后台导入完成后，在帧边界等待 GPU 空闲、刷新材质/纹理/Shader 会话与预览，保留已有材质对象引用。失败信息出现在状态栏及 Console，并保留最后可用显示内容。多个资源会话先完成预检，再一起提交替换；未保存的材质草稿受到保护，干净的材质 Inspector 会重新加载。

只读 pak 预览不会改写包；Editor 的界面引导内容另行准备在所指定项目 Library。发行 Player 不需要这条编辑器引导流程。

## 显式内容构建

```powershell
Engine/Build/Debug/UrsaPak.exe --project-root Demo --configuration Debug --output Demo/Build/Content.pak
Engine/Build/Debug/UrsaScriptPlayer.exe --pak Demo/Build/Content.pak --scene Snake/Snake.ursascene
```

`--project-root` 是游戏内容构建入口：先调用同一项目管线准备当前 Player 资源和 Game 脚本，再由 `PakBuilder` 校验并原子发布包。导入失败会保留原包；只打包只读运行视图中的 `ursa:` / `prj:`，不收录源 Shader、图片、模型、`.meta`、Library 路径、Ready.json 或 Editor 程序集。底层 `PakBuilder` 只组装调用方提供的有效内容，不执行导入。

包内 `prj:__UrsaCooked/AssetCatalog.ursacatalog` 保留 GUID/local file ID 到逻辑运行路径的映射。模型网格、材质、纹理、骨骼和动画子资产由同一目录解析。部署程序、运行所需原生库与 .NET 运行时以及 Content.pak 后，Player 不依赖源 Assets、项目 Library、引擎 Build 目录或 Vulkan SDK 工具。

## 编译、测试与迁移

普通 CMake 配置/构建不导入资源，不创建项目 Library 或引擎资源镜像；Shader 工具可在运行导入工具前配置。`UrsaEngineResources` 与 `UrsaEngineShaderAssets` 已移除。保留 Visual Studio/MSVC 的 `/MP` 和 `--parallel`。

CTest 显式 fixture 在构建目录的 `TestProjects/Runtime/<配置>/Library` 准备内置资源及测试 GLSL，在 `TestProjects/Sample/<配置>` 准备 Demo 的隔离副本。直接编译测试程序不准备内容。执行测试时缺少工具会报告 fixture 错误；已准备内容可重复命中。

旧默认 `URSA_ASSET_CACHE_DIR`、`URSA_ASSET_MIRROR_DIR`、`URSA_ENGINE_RESOURCES_ROOT` 和 `URSA_SHADER_COMPILE_CACHE_DIR` 会在重新配置时清除。未知自定义旧路径会明确要求移除选项并执行项目导入；迁移不删除数据。Engine/cache 和 Build/Resources 不再作为默认资源来源。旧格式项目 Library 可以删除后重建；Assets 与 `.meta` 必须保留。

`URSA_CONTENT_PROJECT` 是显式的只读工具/测试内容选择入口，指向已准备项目，使用当前程序配置的 Editor 运行目录；不进行导入。正常 Editor/Player 启动使用 `--project` 或 `--pak`。


## 导入对象展示目录

Library 导入索引格式 4 保存每个输出的 `displayName` 和 `visibleInProject`，与二进制产物、主对象 ID 一起原子发布。版本 3 及更早索引需要显式重新导入；保留源文件和 `.meta`，不修改 GUID、稳定输出 key 或 Scene/Prefab 引用。Editor 和 UrsaAssetImport 使用同一生成路径。

Project 源文件行代表主资产，展开区只显示可见子资产。ModelNode 等内部对象仍保留身份并参与模型、骨骼和动画引用，但不会成为平级资源条目。显示名称不作为身份；修改名称后即使资源字节不变，目录也会更新。导入失败保留上一份完整目录及诊断，不能把失败候选当成成功删除对象。

资源选择器使用与 Project 相同的名称、类型和源路径。主资产的零 ID 别名及实际 ID 只对应一个选项；拖拽子资产传递准确 GUID/local file ID，接收字段按类型校验。
