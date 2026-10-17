# 脚本与游戏内容发布

在工作区根目录执行：

```powershell
cmake --build Engine/Build --config Debug --parallel --target UrsaEditor UrsaScriptPlayer UrsaAssetImport UrsaPak
Engine/Build/Editor/Debug/UrsaAssetImport.exe --project Demo --configuration Debug
Engine/Build/Debug/UrsaScriptPlayer.exe --project Demo --scene Snake/Snake.ursascene
Engine/Build/Debug/UrsaPak.exe --project-root Demo --configuration Debug --output Demo/Build/Content.pak
Engine/Build/Debug/UrsaScriptPlayer.exe --pak Demo/Build/Content.pak --scene Snake/Snake.ursascene
```

Editor 自动准备项目资源和当前脚本；未打包 Player 只读取已准备内容。C# 编译工具 UrsaScriptCompile 可单独编译项目脚本，但不保证全部运行资源就绪；完整运行请使用 UrsaAssetImport。

内容构建自动准备当前 Player 资源与 Game 脚本，再生成 pak。包不包含 Editor 程序集、源 .cs、Shader 源码或 Library 索引。与程序配置一致的托管引导文件通过 VFS 部署在项目 Library；不混用 Debug/Release。API 指纹变化使旧脚本结果失效。

脚本内容清单使用 Version 2，RequiredBindings 明确记录模块 Id/Name/Hash/FunctionCount/CallScoped。Game 只要求 Runtime、Rendering、ManagedHost；Editor 再要求临时 Editor 模块。松散和 Pak 发布保留同一份要求。宿主在读取项目程序集之前核对 native 与 managed 框架的模块身份；旧清单、缺少模块、重复项或错误指纹要求重建内容。绑定不匹配不能靠重新复制一个旧项目 DLL 修复。

ScriptBuild 指纹包含生成模块身份、生成协议版本，以及 Runtime/Rendering/Bindings（Editor 组额外包含 Editor）API 程序集和目录工具。绑定或共享布局变化后，即使项目 `.cs` 未改也必须重新编译。SourceFingerprint 也包含框架与工具输入，后台就绪检查能发现变化；发布前再次比较输入，防止编译期间混入另一代结果。

修改 `.bindings.cs` 后，在工作区根目录执行：

```powershell
cmake --build Engine/Build --config Release --target UrsaUpdateBindingsSnapshot -- /m:1 /nodeReuse:false
cmake --build Engine/Build --config Release --target UrsaAssetImport UrsaScriptPlayer UrsaPak -- /m:1 /nodeReuse:false
Engine/Build/Editor/Release/UrsaAssetImport.exe --project Demo --configuration Release --profile Both --force
cmake --build Engine/Build --config Release --target UrsaCheckBindingsSnapshot -- /m:1 /nodeReuse:false
```

源码原生快照在 `Engine/Scripting/Bindings/Generated`，ON 构建使用 Build/Bindings 下的生成头；`BindingManifest.generated.hpp` 仅含生成身份数据，脚本构建和原生内容检查不必装载 Editor 函数表。项目生成工程、程序集、目录及内容清单仍全部位于 `Library/ScriptBuild`。清空 Library 或在新项目目录只复制 Assets 后，执行同一条导入命令即可重新生成；不要在 Assets 内创建编译输出。

发行时部署 UrsaScriptPlayer、其原生动态库与 .NET 运行时以及 Content.pak。包通过 VFS 懒校验数据块，不需资源源文件或编译工具。更多参数和错误恢复见 [项目资源导入管线](AssetImportPipeline.md)。
