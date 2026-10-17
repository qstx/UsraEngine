# 模块装载与原生快照

`.bindings.cs` 生成 Runtime、Rendering、ManagedHost 与 Editor 的模块 ID、SHA-256 指纹、函数表及依赖。Runtime、Rendering、ManagedHost 是必需的会话模块；Editor 是仅在扩展回调期间开放的模块，Player 不需要它。`Required`、`CallScoped`、依赖、方向、线程、参数/结果、引用值布局和基础 ABI 布局均参与指纹。

hostfxr 使用组件入口约定找到 `Bootstrap.InitializeBindings`。固定引导只交换版本明确的模块描述和回调描述；普通业务入口均由契约生成。业务函数采用 Cdecl，不能把 hostfxr 的引导约定当作业务 ABI。旧函数表和逐项 hostfxr 业务注册均已删除；框架初始化消息由生成的 InitializeFramework 回调处理，固定引导只交换模块。

引导先校验整个依赖集合、所有描述和原生上下文，再安装当前线程的视图。函数数相同不能替代指纹校验；缺失必需模块、错误平台/版本或不同字段布局均拒绝。失败候选保留原视图。会话保存独立 `BindingSnapshot`，选择时重新验证 native 租约，关闭后使旧视图失效。

native 上下文使用单调分配的租约 token。入口先在注册表中取得仍有效的租约、锁定并校验所属线程，才访问服务对象；不解引用从 managed 返回的任意对象地址。场景的句柄检查复用 `ManagedHandleRegistry` 的会话、种类及代数规则。提供并发服务的派生上下文必须在自身状态销毁前调用 `Deactivate()`。

Editor 的 `CallScoped` 模块只能通过 `EnterCallScope` 临时安装，不能出现在永久会话集合。嵌套作用域按相反顺序关闭；切换或关闭会话会清除临时模块，旧 scope 的 Dispose 不恢复前一会话。Rendering 帧 token 与资源句柄已复用原有校验；EditorInvocation 使用数字上下文 token 作为回调 generation，并检查脚本 session、所属线程、文档/目标存活及属性 revision。

managed 回调表由常驻框架的静态 thunk 构成。native 连接后复制表字段，仅保留回调注册 token；不会保留可释放的表内存或项目实例委托。关闭注册后，旧 token 的调用由 thunk 拒绝。回调动态输出通过 `CallbackResult<T>` 持有，在 native 完成复制后自动释放；不能只保存其中借用的 wire 字符串/数组。

## 生成与清理

在工作区根目录执行：

```powershell
cmake --build Engine/Build --config Release --target UrsaUpdateBindingsSnapshot -- /m:1 /nodeReuse:false
cmake --build Engine/Build --config Release --target UrsaCheckBindingsSnapshot -- /m:1 /nodeReuse:false
cmake --build Engine/Build --config Release --target UrsaBindingsTests UrsaBindingSnapshotTests -- /m:1 /nodeReuse:false
ctest --test-dir Engine/Build -C Release -R '^UrsaBindings(Signature|Generator)?Tests$|^UrsaBindingSnapshotTests$' --output-on-failure
```

更改声明、被引用共享值或生成规则后，先更新快照，再编译框架。ON 构建的 `UrsaBindingsGenerated` 输出位于 `Build/Bindings/<Configuration>`；框架目标依赖快照漂移检查。快照只包含生成的原生头、模块清单和输出所有权索引，位于 `Engine/Scripting/Bindings/Generated`，不手工编辑或执行 clang-format。

OFF 构建包含随源码提供的快照，ON 构建包含构建树的输出。`BindingModules.cpp` 在两种模式都编译生成的表和布局断言；两份头不会成为重复编译单元。快照更新使用 `--native-only`，同样支持 `--check` 和删除旧模块时清理旧输出。

包含 RecordedCommand 的模块另有 `<Module>.Commands.generated.hpp` 和 `<Module>.Commands.g.cs`。命令字段及其共享类型闭包参与 CommandHash，普通 ABI 仍使用模块 Hash；新增普通方法不会单独改变未变动的批次契约。框架在生成之后收集命令 partial，首次构建不依赖已经存在的文件；删除最后一个命令会清理对应产物。OFF 同样直接编译 native 命令快照，不运行 SDK。

可以删除构建树中 Bindings、BindingTools、暂存 Managed 和其输出后重新构建；源快照保留在版本管理中。错误声明不会发布混合产物，也不会把上一份输出作为本次成功生成的证据。共享字段即使大小相同，类型变更仍改变指纹；此时检查失败应重新生成并连同源码一起提交。

关闭托管的干净构建示例：

```powershell
cmake -S Engine -B Engine/Build/BindingsNativeOnly -DURSA_ENABLE_DOTNET=OFF -DURSA_BUILD_EDITOR=OFF -DURSA_BUILD_TESTS=ON -DURSA_PROJECT_DIR=
cmake --build Engine/Build/BindingsNativeOnly --config Release --target UrsaBindingSnapshotTests -- /m:1 /nodeReuse:false
ctest --test-dir Engine/Build/BindingsNativeOnly -C Release -R '^UrsaBindingSnapshotTests$' --output-on-failure
```

该路径不建立生成器或托管目标，也不查找 SDK。验收时同时从 PATH 中移除 dotnet，确认原生构建不会隐式依赖本机安装。

ManagedHost 的 21 个反向方法全部由 ManagedHost.bindings.cs 声明：行为、会话、内容/程序集、资产转移、管线、重载与 Editor 扩展。Editor 目录返回 EditorExtensionInfo[]；准备和调用不再注册独立 hostfxr 方法。EditorBindingCapability 只在框架之间传递模块描述和上下文 token；项目仅获得 EditorReference。回调安装临时 Editor 模块后不重复选择会话，退出时撤销。

生成输出还包含不引用业务头的 `BindingManifest.generated.hpp`。ScriptBuild 使用其中的模块身份构建指纹并生成 Version 2 内容清单的 RequiredBindings；AssetImport 与宿主均核对要求。Game 清单没有 Editor 项，Editor 清单才加入临时模块。C# 加载器在装载项目程序集之前再次对照已注册框架模块，避免只在第一次真实调用时才发现契约错误。
