# SerializedObject 多目标编辑契约

`SerializedObjectGroup` 聚合一组有序 `SerializedObject`，以主选目标提供代表值，并以相同路径、字段描述和持久类型语义的交集提供可编辑字段。查询返回 `Value`、`HasMultipleDifferentValues`、`ValueState` 和 `TargetCount`；空值、缺失引用及普通值保持不同状态。同一名字但描述不同的字段不会进入公共属性。资源引用按 GUID 与 local file ID 比较，主资源 local ID 的 `0` 与缺省别名等价。

```cpp
auto group = SerializedObjectGroup::Create({primary.get(), other.get()}, 0);
auto state = group.value.DescribeProperty("/Fields/position/0");
if (state && state.value["HasMultipleDifferentValues"])
{
    // 显示混合值；读取不会登记修改。
}
group.value.SetValue("/Fields/position/0", 10.0);
group.value.Apply();
```

字段和分量写入使用 JSON 路径补丁，只更新明确编辑的位置；每个目标从自己的完整快照派生候选，因此未编辑字段、未知数据、引用和身份都从各自目标保留。`CommonPropertyPaths()` 对向量公开共同分量，对集合仅公开所有目标都存在的索引。集合描述报告共同最短长度、主选长度及长度混合状态。显式 `ResizeCollection` 保留各目标的有效前缀并按元素声明类型补默认值；`MoveCollectionElement` 只接受共同索引；`AppendReferences` 显式追加引用。读取不会将 null 集合转换为空集合。

批量适配器须实现完整的 `prepare`、`validate`、`publish` 和 `restore` 回调。`prepare` 返回 `SerializedTargetCheckpoint`，其中可保留原始序列化载荷、源数据、Prefab 覆盖、草稿 revision、Dirty、有效资源租约及适配器专属 token。Prepare 不得修改作者状态；Validate 检查全部准备结果；Publish 交付已准备候选并返回规范化载荷；任何目标失败时，事务会逆序调用 Restore，包括当前失败目标。缺少完整回调的自定义 adapter 可继续使用旧单目标 `Apply()`，但批量提交会拒绝该 adapter。

全部目标发布成功后，`refresh` 回调对已更改目标各调用一次。被设回原快照值的属性不会保留为修改项；没有有效变化时不会 publish、刷新或推进目标 revision。Dirty 归目标文档/草稿适配器所有，失败恢复其原值；一次事务不设置 Undo/Redo 历史。

契约回归位于 `UrsaSerializedObjectTests`：覆盖公共字段与异类型同名字段、代表/混合值、引用别名、null/缺失引用、分量补丁、集合长度与重排、事务第 N 项验证/发布失败、资源租约、Prefab 覆盖、revision、Dirty 和刷新次数。C# Inspector 与原生多目标 Inspector 的接入由对应 OpenSpec 任务继续完成。
