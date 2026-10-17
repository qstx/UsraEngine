# C# 绑定声明与封送

绑定开发的唯一声明来源是 Engine 内的 `.bindings.cs`。公开项目 API 保持属性/方法和值类型形式，由生成的内部绑定调用 native 服务。功能迁移进度见 [覆盖矩阵](InterfaceBindingsCoverage.md)，公开源码变更见 [迁移表](InterfaceBindingsMigration.md)。当前生成器的普通方法、回调和值封送已有真实 ABI 验证；录制命令生成和业务模块迁移仍按 change 任务推进。

## 普通方法与回调

可执行示例直接位于 `Tests/Bindings/Contracts/Fixture.bindings.cs`，其共享类型位于 `Values.cs`，数学类型使用生产源码 `Managed/Ursa.Runtime/MathValues.cs`，不为测试维护第二份数学布局。

```csharp
[BindingModule("Fixture")]
[NativeHeader("BindingFixture.hpp")]
internal static partial class FixtureBindings
{
    [NativeMethod("Ursa::Tests::BindingFixture::Add")]
    internal static partial long Add(long left, long right);

    [ManagedCallback("BindingFixture.Callbacks.Scale")]
    internal static partial Point ManagedScale(Point value);
}
```

每个入口属于一个模块并声明方向。普通方法对应的 C++ 服务接受 `BindingCall&` 和明确的参数，返回 `Result<T>`/`Result<void>`；绑定生成器处理状态、调用局部错误、对象种类检查和模块表。struct 参数在 ABI 中使用指向固定 wire 值的指针，服务内使用 const 引用；标量直接传递。bool 在 ABI 中为 UInt32，普通 C++ 方法参数/返回值仍为 bool。

生成模块头先包含 wire 值定义，再包含 NativeHeader 指定的服务头。服务头不维护表字段或注册；native 签名错误通过生成 static_assert 和映射到 `.bindings.cs` 的诊断拒绝。回调通过框架静态 thunk 与有代数的注册作用域调用，异常不会穿越函数指针边界。

`WireValue` 标记枚举及明确的数据 struct/DTO；使用公共实例字段或有 get/set 的属性，其他业务行为不成为 wire 字段。C# 普通泛型便捷 API 在包装层调用具体绑定，不直接跨 ABI。递归对象图使用专用 archive 服务，不能递归展开为 wire DTO 或退回操作名 JSON。

## 输入、输出与延迟工作

| 数据 | 所有权规则 |
| --- | --- |
| 固定数值/数学值 | 按值转换为固定布局；无可保留的原生对象地址 |
| 输入字符串/数组/ReadOnlySpan | 生成包装拥有临时封送内存，native 服务只在同步调用期间借用 |
| native 字符串/集合结果 | 服务使用 `call.arena` 创建拥有结果，包装复制后在 finally 释放整个结果 |
| managed 回调的字符串/集合结果 | 生成包装持有独立结果 arena，经释放回调清理；不重复调用业务方法查询长度 |
| 延迟工作 | 接受方在返回前复制输入或显式保留资源，不能存储借用的 DataView/指针 |
| 业务 archive | 专用方法标明用途/格式/版本；数据由序列化服务解释，不负责选择任意引擎操作 |

native 输出中的动态范围必须属于该调用的 result arena；生成桥接在读取/交付前检查所有权和范围。输出不能引用栈或借用输入。多缓冲/嵌套 DTO 共用一次结果归属，解码异常也释放全部缓冲。输入和输出容量均有硬上限，调用上下文可进一步限制输出预算。

错误状态、会话标识与错误文本的所有权分开表达。即使输出预算耗尽而无法分配错误文本，状态和会话标识仍有效；适用时保留 managed 堆栈。基础 ABI 类型的 CLR 布局也参与模块身份，生成头会对 native 大小与字段偏移逐项断言。

UTF-8 使用显式长度，保留中文、嵌入零及声明允许的 null/empty 区别；非法 UTF-8 或不能编码的 UTF-16 会失败。长度、乘法溢出、presence/reserved 标记和 bool 值均在业务访问前检查。普通标量成功路径无需 JSON、装箱或输出缓冲分配。

双向夹具同时覆盖捕获输入后再使用：native 服务复制 managed 输入，managed 回调取得 native 输入的独立副本。源数据修改、作用域结束及 GC 后，延迟读取的值必须保持不变。返回数据在后续调用后仍是有效快照。

## 数学约定

Matrix4x4 的字段排列为列序：`M00,M10,M20,M30,M01,...,M33`，索引器为 `[row,column]`，采用列向量右乘。该约定与 native `Mat4::At(row,column)` 一致，世界坐标保持右手系、+Y 向上。

native 适配使用 `BindingMath.hpp` 的逐字段转换，不依赖 native 数学类型与 CLR struct 恰好具有相同内存。DrawMesh、SetGlobalMatrix 和 SetComputeMatrix 接受 Matrix4x4；DrawingSettings.CameraPosition 为 Vector3?。矩阵以列序进入 native，uniform 写入根据 Shader 反射的矩阵 stride/major order 转换；不同 Shader stage 声明不一致会失败。FOV 的现有显式弧度 API 继续使用弧度。

真实 ABI 测试将一个非对称矩阵与平移矩阵分别在 C# 与 native Mat4 中相乘，并逐元素比较；组合矩阵再变换 `(1,2,3)` 得到 `(34,95,201)`。Quaternion、Color 及 Vector2/3/4 使用非默认字段值验证，不以单位矩阵或零向量掩盖布局问题。

真实宿主还将 C# 非对称矩阵发送到导入的 ComputeShader，分别执行 HLSL `row_major` 和 `column_major` 的 `mul(matrix, float4(2,3,5,1))`，读回均为 `(27,71,115,159)`。DXC 在 SPIR-V 中转置 HLSL 矩阵类型，反射标记不能直接当作 C# 字段排列；该边界由 native uniform 服务统一处理。

## 录制命令

命令同样声明在 `.bindings.cs`，第一个参数为录制存储 `BindingCommandWriter`，其余参数参与契约。下面是直接参与双向编译和执行的 `Tests/Bindings/Contracts/Commands.bindings.cs` 示例：

```csharp
[BindingModule("Fixture", Required = true)]
internal static partial class FixtureCommands
{
    [RecordedCommand]
    internal static partial void Capture(BindingCommandWriter writer, Packet packet,
        ReadOnlySpan<int> values, Matrix4x4 matrix);

    [RecordedCommand(NativeOnly = true)]
    internal static partial void Reset(BindingCommandWriter writer);
}
```

调用方用生成的 `Ursa.Bindings.Generated.FixtureCommands.Hash` 创建 writer，再调用 `FixtureCommands.Capture`，通过 `writer.ToArray()` 取得独立批次。模块指纹、批次契约身份和稳定 opcode 均自动生成；新增参数无需维护字节偏移。writer 在录制时复制字符串、集合和值，编码失败会撤销本条记录，保留此前成功记录。Clear/Dispose 只影响录制存储，交付副本与资源保留由业务执行层管理。

生成的 managed Validate 和 native Decode 校验版本、身份、记录数、长度、类型、presence、UTF-8、容量与零填充；native Decode 先产生完整命令集合，业务层再统一预检和执行。Decode 的动态 wire 值属于调用 arena 或借用输入，在转换为延迟工作时仍须复制数据并保留资源。native-only 命令只允许 native 编码/执行，managed 包装与普通提交解码均拒绝；业务 archive 不能嵌入录制命令。

普通方法、回调、WireValue 和命令示例共用下列夹具生成/构建命令。生产 `CommandBuffer` 调用 `RenderCommands.bindings.cs` 的具体命令；native 将生成的变体转换为内部执行操作，不解析手写字节偏移。内部执行器枚举不再承担 wire opcode 的职责。

## 构建与验证

在工作区根目录运行：

```powershell
cmake --build Engine/Build --config Release --target UrsaBindingsTests -- /m:1 /nodeReuse:false
ctest --test-dir Engine/Build -C Release -R '^UrsaBindings(Signature|Generator)?Tests$' --output-on-failure
```

单独生成夹具（输出位于构建目录）：

```powershell
dotnet Engine/Build/Managed/BindingTools/Release/Ursa.BindingsGenerator.dll --input Engine/Tests/Bindings/Contracts --source Engine/Managed/Ursa.Runtime/MathValues.cs --output Engine/Build/BindingExample
dotnet Engine/Build/Managed/BindingTools/Release/Ursa.BindingsGenerator.dll --input Engine/Tests/Bindings/Contracts --source Engine/Managed/Ursa.Runtime/MathValues.cs --output Engine/Build/BindingExample --check
```

只检查模式不写入。输入目录、共享源码和生成器变化都会使构建依赖失效；规范化产物不含机器绝对路径或时间戳。错误声明不发布新旧混合结果，删除的契约清理相应旧输出。

模块引导、会话和调用作用域、原生快照的更新/检查流程见 [模块装载文档](InterfaceBindingsModules.md)。

`UrsaBindingsSignatureTests` 还实际编译有效、目标缺失、返回类型错误和参数类型错误的 native 夹具，确认错误在构建时定位到声明。`UrsaBindingsTests` 检查真实跨语言值、输出预算耗尽、返回解码失败、关闭后的回调和预热后的标量托管分配；这些检查不替代后续完整 Engine/Editor/Player 验收。
