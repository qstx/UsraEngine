# 脚本身份与源元数据

普通脚本直接继承 `Behaviour` 或 `ScriptableObject`，无需声明 `UrsaScriptType`：

```csharp
using Ursa;

public sealed class Mover : Behaviour
{
    public float speed = 1;
}
```

Editor 在首次成功发现类型时把身份写入 `Mover.cs.meta`。元数据与源文件一起保存和提交；Library 中的类型目录和程序集都是可重建缓存。

## 身份如何保持

原有 GUID 标识源码资产，新增的 `scriptTypes` 表记录文件内各类型的 `Id`、`Version`、`LocalFileId`、当前 `TypeName`、`Primary` 与旧声明兼容标记。这些字段由引擎维护，普通脚本开发不需要编辑它们。旧显式身份进入兼容迁移路径时保留已有组件的 GUID/local file ID 解析行为；自动生成的新身份校验当前 GUID 与 local file ID。

单个具体脚本或与文件同名的脚本作为主类型。修改主类型的类名、命名空间，或通过资产操作移动源码和 sidecar 后，目录更新 CLR 类型映射并保留身份。Scene、Prefab 和 ScriptableObject 数据继续使用持久身份；Player 加载旧 CLR 名称的组件时也按当前已验证目录查找类型。

一个文件仍可包含多个脚本类型，记录不依赖声明顺序；其他类型按完整名称或唯一简单名称关联。多个辅助类型同时改名可能无法唯一推断，不能把它当作可靠自动迁移：保留旧记录和数据，明确元数据对应关系后再重编译。普通项目推荐一个文件一个主脚本。

通过 Project 复制源码会分配新 GUID 并清除复制件的类型身份表，下次发现生成独立身份；移动保留原表。复制后还需要给 C# 类改名，避免重名声明。更新导入器设置不丢失类型身份。

## 已有项目的迁移

1. 升级 Engine 后保留旧声明，执行一次 Scripts → Compile Scripts 或 `UrsaScriptCompile`，将已有身份写入 `.meta`。
2. 检查 `.cs.meta` 已有 `scriptTypes`，再移除 `UrsaScriptType`；GUID、旧 ID、版本和 local file ID 保持。
3. 随源码提交 `.meta` 并重新编译，原场景、Prefab、资产和扩展目标仍使用旧 ID；清理 Library 后重建不需要恢复特性。

旧特性继续兼容既有源码和框架类型。已有未注解脚本首次初始化时沿用 `auto:<源 GUID>:<首次 CLR 类型名>`，保留原序列化身份；该字符串可能包含历史名称，初始化后不随改名重算。

数据版本保存在元数据中，默认 1；不是源码修改计数。现有版本及字段兼容性检查仍执行，不兼容版本不会自动转换旧数据。

## 故障与恢复

- 语义发现失败不写候选身份，不替换活动内容；修复后重编译。
- 发现期间有新修改时，过时写入被拒绝，可重试。多文件逐文件原子写入，中途失败不代表正式发布成功。
- 重复身份、非法表或歧义映射会报告错误，不通过随机换身份掩盖问题。
- 元数据改变会使正式构建失效，IDE 自行构建仍不替代正式发布。
- 源码与 `.meta` 成对移动、备份和提交。删除 sidecar 会丢失身份；清理 Library 不会。

实际回归和项目迁移结果见 [验收记录](ScriptIdentityEvidence.md)。
