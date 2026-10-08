# 场景行为生命周期与一帧调度

## 对象与组件启用状态

GameObject 的 `activeSelf` 表示对象自身是否活动；`activeInHierarchy` 会同时检查全部祖先。通过 `GameObject::SetActive` 设置自身状态，使用 `ActiveSelf()` 和 `ActiveInHierarchy()` 查询。组件的 `SetEnabled`/`IsEnabled` 独立控制该组件。

行为只有在所属 GameObject 层级活动且组件启用时，才会收到更新。停用祖先会停止整个子树的行为和呈现；重新启用会恢复行为。

## 生命周期事件

原生 `Component` 和托管 `Behaviour` 共用以下事件顺序：

1. 场景对象、组件字段和跨对象/资源引用恢复完成。
2. 活动层级中的组件依次执行 `Awake`；即使组件自身禁用也会执行。处于非活动层级的对象等到首次变为活动时执行。
3. 有效启用的组件执行 `OnEnable`。
4. 全部初始活动对象完成 `Awake` 和 `OnEnable` 后，启用的行为执行一次 `Start`。
5. 各帧执行 `FixedUpdate`、`Update`、`LateUpdate`。

组件或任一祖先停用时，已启用行为执行 `OnDisable`；重新启用会再次执行 `OnEnable`，不会重复执行 `Awake` 或 `Start`。实际销毁时先执行需要的 `OnDisable`，再对曾活动过的行为调用一次 `OnDestroy`。底层资源清理由原生 `OnRemove` 路径完成，即使对象从未活动或游戏回调失败也会执行。

继承 Component 并覆盖对应事件即可实现原生行为：

```cpp
class MoveComponent final : public Ursa::Component
{
public:
	Ursa::Result<void> Start(const Ursa::FrameContext& context) override;

	Ursa::Result<void> Update(const Ursa::FrameContext& context) override;
};
```

## 帧顺序

Engine Application 和 Editor Run 使用同一个 SceneFrameRunner：

```text
窗口事件与输入
      |
      v
FixedUpdate (0..N 次，固定步长)
      |
      v
Update -> LateUpdate -> 结构变更与生命周期同步 -> 世界变换/呈现收集 -> 渲染
```

每个渲染帧只采样一次输入，同一帧的多次 `FixedUpdate` 收到相同按键沿；按下沿和释放沿不会因为读取而消耗，下一次采样更新它们。`FixedUpdate` 的 `deltaSeconds` 和 `fixedElapsedSeconds` 使用固定模拟时钟；`Update` 与 `LateUpdate` 的 `deltaSeconds` 使用本帧经过时间。单帧固定步数受配置上限约束，剩余过量积压会被丢弃。

窗口最小化时暂停场景行为和渲染，恢复时清除固定步长积压。场景外的 GameObject、Prefab 独立预览和编辑场景不会执行游戏生命周期。运行中结构修改在安全边界提交；新活动对象先完成初始化，再参与后续可执行阶段。

## 验证

`UrsaSceneGraphTests` 检查生命周期与启用转换，`UrsaFrameSchedulerTests` 检查一帧内阶段、固定时间和输入沿，`UrsaApplicationSceneTests` 与 `UrsaPlaySessionTests` 检查 Player 和 Editor Run 使用一致调度。
