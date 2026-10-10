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

发行时部署 UrsaScriptPlayer、其原生动态库与 .NET 运行时以及 Content.pak。包通过 VFS 懒校验数据块，不需资源源文件或编译工具。更多参数和错误恢复见 [项目资源导入管线](AssetImportPipeline.md)。
