# ScriptableObject 持久资产

`ScriptableObject` 是项目 C# 定义的配置资产，不挂在 GameObject 上。具体公开类型
继承 `ScriptableObject`，可用带稳定 ID 的 `UrsaScriptType` 声明身份：

```csharp
using Ursa;

[UrsaScriptType("demo.render.settings", Version = 1)]
public sealed class RenderSettings : ScriptableObject
{
	public float exposure = 1.0f;
	public Shader? shader;
	public ComputeShader? compute;
	public Texture? lookup;
	public RenderSettings? shared;
}
```

字段服务对 `Behaviour` 和 `ScriptableObject` 使用相同的字段描述与 JSON payload。
它支持布尔、整数、浮点、字符串、枚举、向量、颜色、纹理、网格、材质、Shader、
ComputeShader、自定义资产引用，以及一维数组和 `List<T>`。Shader、ComputeShader
字段可用 `FromReference` 保存稳定资产 GUID/local file ID；
纹理字段使用 `Texture.Load` 加载已导入资源。`FromReference` 不创建 GPU 程序。游戏对象或组件字段只适用于
Behaviour，持久资产不得保存场景对象、组件、未保存 ScriptableObject 或临时资源。

类型身份包含脚本源 `.meta` GUID、稳定 `UrsaScriptType` ID、类型版本及由类型 ID
确定的 local file ID。CLR 类型名和程序集中的声明顺序不是持久身份。冲突类型或
不支持的字段会提供诊断；Add Component 只接受 `Behaviour` 类型。

## 源文件与归档

项目资产使用 `.ursaasset` 和同名 `.meta`。`.meta` GUID 标识资产本身，payload 中的
`SourceGuid`/`LocalFileId` 标识 C# 脚本类型，二者用途不同。源归档格式为
`UrsaScriptableAsset` version 1；JSON 与二进制封套共享 `typeId`、`typeVersion` 和
字段 payload。运行内容的 `.ursascriptableasset` 二进制产物由导入层生成，不直接
编辑。资产移动必须沿用 AssetDatabase 的 Move，复制会为副本分配新 GUID。

实际的字段 payload 保留 `FormatVersion`、稳定 `ScriptType`/`Version`、字段值和
可选 `FieldTypes` 描述。例如，C# 字段 `{ public float exposure = 2.5f; }` 表示为：

```json
{
  "FormatVersion": 1,
  "ScriptType": "demo.render.settings",
  "Version": 1,
  "Fields": { "exposure": 2.5 },
  "FieldTypes": {
    "exposure": {
      "Kind": "FloatingPoint",
      "ElementKind": "Unsupported",
      "TypeName": "System.Single"
    }
  }
}
```

保存先把实例序列化为候选数据，再原子替换源文件。字段验证、GUID 检查或 archive
编码失败时保留源文件字节。未知字段和值继续保留，可在同一稳定类型恢复后读回。
修改字段类型时显式提高脚本类型版本；不兼容候选不会写回原资产。

## 加载、依赖和恢复

加载器先收集受预算限制的整个资产图，再建立对象身份、还原字段、解析共享和循环
引用，最后调用启用回调。每个会话按 GUID/local file ID 复用同一实例；图持有引用
租约，只有最后一个图释放时才销毁由加载器创建的实例。外部拥有的实例在图关闭后
仍有效。会话关闭会使其中所有旧包装失效。

缺失脚本类型会保留完整源描述，并记录类型 ID、GUID/local file ID 与诊断；这类源
数据不能运行、发布或保存为一个空资产。兼容类型回来后，从原描述建立候选图。字段
恢复或 `OnEnable` 失败时丢弃候选，旧图和未保存编辑保持可用。候选成功后一次性替换
活动图，并释放旧实例。字段重命名使用 `FormerlySerializedAs`；旧值迁移到新名称，
不再把旧名称误当作未知字段重复保存。

## 生命周期和验证范围

帧状态、GPU backing、CLR 地址和反射对象不属于资产字段。资产图及其对象都由脚本
会话拥有；管线或项目设置需要持续实例时，应由项目资产图持有，并在重载或会话关闭
时释放。GPU 资源依赖完成状态回收。

目前已实现源归档读写、`.meta` GUID 分配、移动/复制、JSON/二进制往返、受限引用图、
托管字段和候选类型恢复。显式项目导入会校验 ScriptableObject 类型及字段模式，记录
脚本/Shader/纹理/材质依赖并发布二进制资产；运行会话从 Loose 或 Pak 目录加载资产
引用图。未导入 ComputeShader 的内容会在依赖闭包验证时报错。资产编辑器菜单、通用
Inspector 和 Graphics 项目设置仍属于后续 Editor 任务。
