# 原生内存分配

`Core/Memory/Memory.hpp` 提供 CPU 分配入口。`Allocate` 接收字节数、对齐、类别和可选的对齐偏移，失败时抛出异常；`TryAllocate` 对无效参数或内存不足返回空指针。对齐必须是非零的 2 次幂，零字节分配允许返回可释放的非空块。

```cpp
auto tag = Ursa::Memory::RegisterTag("CustomSubsystem");
void* data = Ursa::Memory::Allocate(256, 64, tag);
Ursa::Memory::Free(data);
```

`Free` 根据块头找到原始分配器和统计类别，不依赖调用者提供大小或类别。仅允许传入这些接口返回的原始指针或空指针；不得传入栈地址、第三方地址、重复释放地址或对象内部地址。持久分配支持跨线程释放；显式创建的分配器必须比其所有分配活得更久。

默认堆按尺寸桶复用小块，每个桶最多保留两个完全空闲的 64 KiB 页；不能放入尺寸桶的大块或高对齐请求由系统堆回退处理。`ReleaseUnused` 释放完全空闲的页，保留正在使用的页。`HeapAllocator::Statistics` 给出保留字节、上游分配/释放次数及存活块数。

`Snapshot` 按类别返回请求字节数、峰值、存活块数、累计分配/释放次数与溢出次数。请求字节不包含块头、对齐填充或空闲池容量；统计开销与池保留量需单独评估。计数按分配线程分片，跨线程释放更新原分片；快照按固定次序锁定分片，保证同一快照的字节与计数一致。全局原子字节计数用于精确记录并发峰值。最多支持 128 个类别（内置类别包含在内），名称为 1–63 字节，同名注册返回同一类别。

统计使用 64 个固定分片，更多线程可共享分片；读取快照时会短暂阻塞各分片的统计更新，应按诊断需要低频采样。尺寸桶锁和统计分片锁为短临界区自旋锁，同类别高并发仍可能存在争用。元数据初始化使用独立的系统分配并满足缓存行对齐，避免统计递归进入自身分配入口。

默认堆与统计元数据保留到进程结束，支持 C++ 静态对象的析构阶段；不会依赖日志、VFS、EASTL 或应用初始化顺序。VMA、.NET GC 和第三方库私有内存不属于此 CPU 分配入口。

## 临时内存

`LinearAllocator` 在线程私有的连续内存中前移游标。默认线程临时分配器预留 1 MiB，可显式创建其他容量的实例。容量不足时通过默认持久堆取得溢出块，仍由临时作用域负责回收，并同时增加实例和类别的溢出计数。

```cpp
Ursa::Memory::TemporaryScope scope;
auto& allocator = scope.GetAllocator();
void* scratch = allocator.Allocate(4096, 64, Ursa::Memory::MemoryTag::Temporary);
// scratch 只在 scope 内使用。
```

作用域退出恢复进入时的游标并释放该作用域内仍存活的原始分配；嵌套作用域保留外层分配。单独 `Free` 可提前结束逻辑分配，但连续块的空间在作用域结束时才统一复用。`Reset` 回收该实例所有分配并增加代际；Debug 模式会检查线程归属和过时的作用域标记。

临时分配器不可跨线程共享或释放。任何指针、容器、智能指针或异步操作不得超出该作用域；带析构函数的对象及容器必须先销毁，再恢复作用域。原始内存批量回收不会替用户调用对象析构函数。临时容器应声明在 `TemporaryScope` 之后，以保证 C++ 析构顺序正确。原始指针使用后失效无法被 C++ 自动检测。

## 自定义 C++ 类型和容器

`Core/Memory/MemoryFactories.hpp` 提供 `Ursa::New<T>`、`Delete<T>`、`MakeUnique<T>` 和 `MakeShared<T>`，支持类别及可选分配器参数，无需继承内存基类。构造异常会释放已申请存储。

```cpp
auto object = Ursa::MakeUnique<MyType>(Ursa::Memory::MemoryTag::Scene, constructorArgument);
auto shared = Ursa::MakeShared<MyType>(Ursa::Memory::MemoryTag::Scene, constructorArgument);
```

`UniquePtr<T>` 使用正确调用析构及 `Free` 的释放器，支持移动和 `reset`，只持有工厂实际创建的类型；不支持将它隐式转换为基类的 `UniquePtr`。需要多态共享时使用 `MakeShared`，它保持 `std::shared_ptr` 的类型和控制块生命周期，包括弱引用持有控制块的行为。`New/Delete` 必须使用同一实际类型及工厂返回的原始地址。自定义分配器必须活到全部强/弱引用或容器释放完毕之后。

`Core/Memory/MemoryContainers.hpp` 提供 `MemoryVector`、`MemoryMap`、`TemporaryVector`、`TemporaryMap` 别名。持久别名可传 `ContainerAllocator(allocator, tag)`，临时别名默认使用当前线程临时分配器，也可传入显式 `TemporaryContainerAllocator`。标准库适配边界使用 `StlAllocator<T>`；它的 `allocate/deallocate` 名称遵循标准库协议。

原有 `Ursa::Vector/String/Map` 等别名不变，其 EASTL 默认分配器统一计入 `Containers` 类别。对象本身与其成员容器是独立分配；成员需要不同类别/分配器时应显式注入。第三方库私有分配、普通 `std::make_shared` 和未迁移的标准库分配仍使用各自的入口。

扩展原始分配后端时，`AllocateRaw` 必须返回至少满足 `std::max_align_t` 对齐的存储，并由配对的 `DeallocateRaw` 释放。公共接口负责更高对齐、元数据和类别统计；后端及分配/释放钩子不得递归使用自身的公共分配入口。

## 已接入的帧路径

- Application 和 Editor 的可绘制帧、SceneFrameRunner 的一次 Tick 都建立可嵌套临时作用域。窗口最小化分支不会建立更新帧作用域。
- Scene 的更新和生命周期同步使用临时组件顺序表；阶段或同步结束即回收，包括回调返回失败的路径。回调内从线程临时分配器取得的存储也受当前阶段约束；需要跨 Update、LateUpdate 或下一帧保存的数据应使用持久分配器。
- SceneDrawEncoder 录制中的模型快照、绘制项和 Pass 编码缓冲使用临时容器。缺省材质的共享控制块也限定在录制作用域内；持久材质克隆使用 `Assets` 类别。帧对象不会保存在 GPU 缓存中。
- ImGuiBridge 录制时的顶点和索引 CPU 拼装使用临时容器，写入 GPU 上传缓冲后即可释放。ImGui 本身的私有分配器不变。
- Editor 脚本构建的可写 Windows 命令行缓冲通过标准库分配器适配接入线程临时内存；`CreateProcessW` 返回时系统已复制参数，不会保留缓冲地址。

## 基准测试

从工作区目录执行：

```powershell
cmake --build Engine/Build --config Release --target UrsaMemoryBenchmarks UrsaMemoryWorkloadBenchmarks --parallel
Engine/Build/Release/UrsaMemoryBenchmarks.exe
Engine/Build/Release/UrsaMemoryWorkloadBenchmarks.exe
```

第一组对比原始 CRT、携带相同统计的 CRT、持久池及临时线性分配，同时比较绘制项排序和 Editor 宽字符命令缓冲。第二组使用真实 `MaterialInstance` 对象及 128 组件场景帧。预热后上游调用数、排序结果和作用域清理是确定性检查；耗时只报告，不作为依赖机器速度的测试门槛。

原始 CRT 不承担类别、所有权和一致快照开销，不能将它与带统计的接口视为完全等价。部分小块模式可能仍由 CRT 更快；按实际工作负载决定是否使用持久池、临时容器或保留第三方路径，避免宣称所有分配都会加速。
