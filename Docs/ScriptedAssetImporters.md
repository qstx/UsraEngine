# 脚本资源导入扩展

这套接口对齐 Unity 的扩展方式，使用 Ursa 类型和命名空间。实现进度由根仓库的 `scripted-asset-editor-extensions` 任务清单跟踪；本页按已实现的基础契约更新，不把规划中的托管回调当作现有 API。

材质主对象、材质子资产与内置模型材质使用同一个当前 `.ursamat` 编码/加载器。纹理引用始终保存 GUID 和 local file ID，运行加载不会将子资产引用折叠为主对象；模型 importer 8 的旧产物需重新生成，不提供裸 CBOR 材质产物兼容路径。源格式已正确的 Engine/Demo 资产无需重写；仓库的 Gold/Ceramic 测试夹具已补齐格式 2 要求的 Shader 身份及原有参数。

## 导入规则快照

`AssetImportRules::Create` 接受脚本 importer 描述、处理器描述、编译指纹和非零 generation，生成拥有全部描述数据的不可变快照。快照不拥有跨程序集委托；实际执行回调属于宿主会话。

脚本类型由源 `.cs.meta` GUID 与非零 local file ID 标识，CLR 名称只用于查找和诊断，改名不会改变持久身份。importer 声明非零版本及扩展名；扩展名按 ASCII 小写规范化，可以使用带点或不带点的单段后缀。非法后缀、重复注册、同一类型重复声明及覆盖内置后缀均失败，冲突诊断列出双方。已有脚本 importer 缺失时，带所需身份的解析不会默默改用默认字节拷贝。

处理器按 `order` 再按稳定类型身份排序，声明纹理、模型或两者。项目处理器只应用于 `prj:` 对应内置导入器，不影响只读 `ursa:` 内容。规则指纹包含 importer 和适用处理链的身份/版本/顺序；使用托管规则时加入编译指纹，generation、CLR 展示名称和发现顺序不进入持久指纹。没有使用脚本规则的原生资源不因编译指纹变化失效。

## ScriptedImporter 声明与发现

将 importer 脚本放在项目 `Assets/Editor` 下，继承 `Ursa.Editor.ScriptedImporter`，使用 `[ScriptedImporter(version, extension)]` 声明格式并重写 `OnImportAsset(AssetImportContext)`。运行类型应放在 Editor 文件夹之外，例如 `Assets/CounterAsset.cs`：

```csharp
using Ursa;

public sealed class CounterAsset : ScriptableObject
{
    public int Value;
}
```

再在 `Assets/Editor/CounterImporter.cs` 声明格式：

```csharp
using System.Globalization;
using Ursa;
using Ursa.Editor;

[ScriptedImporter(1, "counter")]
public sealed class CounterImporter : ScriptedImporter
{
    public override void OnImportAsset(AssetImportContext context)
    {
        var asset = ScriptableObject.CreateInstance<CounterAsset>();
        asset.Value = int.Parse(context.ReadSourceText(), CultureInfo.InvariantCulture);
        context.AddObjectToAsset("main", asset);
        context.SetMainObject(asset);
    }
}
```

声明需要 public、非抽象、非泛型类型和 public 无参构造函数。版本必须非零，后缀按原生规则规范化；重复后缀、非法声明及内置格式冲突均失败，不依赖程序集枚举顺序选择赢家。类型目录将此类记录为 Editor `ScriptedImporter`，与 `.cs.meta` 一同保存源 GUID/local ID；改文件名、类名、命名空间或 importer 版本不改变已有类型身份。声明中的版本控制导入规则，不替代脚本资产身份。

`DiscoverManagedImportRules` 从独立 Import 会话获取类型化描述，再创建原生不可变规则快照。C# 注册器归属可卸载的项目会话，创建回调时才实例化 importer；关闭会话释放类型引用。注册后不允许原地替换，缺失的必需 importer 会明确失败。`UrsaScriptedAssetImporterTests` 在真实 CLR 下验证 Editor 类型目录、发现、稳定身份选择及具体导入入口；协议测试覆盖重复、冲突、非法声明、发现顺序和 CLR 改名，`UrsaScriptIdentityTests` 还验证 importer `.meta` 随文件/类改名保持身份。

## 主资源目录

导入缓存格式 3 显式保存 `mainObjectLocalFileId`，`ImportCacheStatus` 和 `ImportedArtifactRecord.isMainObject` 提供该信息。主资源必须存在于产物集合；非零主资源发布必须传入该 ID。无效输出即使命中缓存也会被拒绝。

`StableImportObjectId(importerIdentity, outputKey)` 从稳定 importer 身份和非空 key 计算非零 ID。key 区分大小写，不能依赖展示名称或输出枚举顺序。声明 key 的产物必须匹配此算法，key 保存在目录中；重复 ID、重复 key 和 ID/key 不匹配在发布前被拒绝，不通过改变枚举顺序重新分配。

`Resolve(guid, 0)` 查询目录声明的主对象，`Resolve(guid, 非零ID)` 查询准确产物。需要源路径时使用独立的 `SourcePath(guid)`，避免把 Library 产物作为源依赖或显示路径。Project 推断导入主对象种类，Editor 资产选择显式传输 `IsSubAsset`，不从 ID 是否非零推断。

运行目录格式 2 为每个产物声明 `isMainObject`，加载时验证每个资产恰好有一个主对象并建立零 ID 查询别名，Loose/Pak 使用相同规则。Mesh 的别名和规范 ID 返回同一缓存对象。ScriptableAsset 的原生 importer 显式标记其非零根 ID；Graphics 校验区分源文件和已导入归档，继续验证资产身份及当前 Game 管线类型。

旧 Library 和运行目录格式需要重新导入，不提供旧目录格式加载器。未受影响的元数据版本仍沿用现有格式；原生生成器可以明确保留零 ID 主对象。

## 设置状态与格式边界

`ImportSettingsDraft` 保存独立的持久基线和编辑草稿，`EffectiveSettings()` 返回持久基线的副本供本次前处理修改。`Edit` 仅更新草稿；`Apply` 在验证成功且源 GUID、importer 和设置未被外部修改时写入元数据，返回是否实际保存；`Revert` 重新读取已保存设置并丢弃草稿。导入调度由保存成功后的调用方安排，这个设置状态对象本身不直接导入资源。

验证失败、只读/写入失败、外部修改或源类型变化都保留未应用草稿，不覆盖源文件。内部设置归档使用 JSON；面向项目的 Inspector 仍通过类型化属性适配器访问，不需要解析此存储格式。

元数据当前版本仍为 1，其 importer 字符串可保存 `script:<源GUID>:<类型local ID>`，配合非零 importerVersion 和设置对象形成稳定声明。脚本声明必须为规范身份，CLR 名称不进入持久值；不为无变化的 envelope 人为升级版本。未支持的版本、损坏身份和错误字段类型会报告诊断，不自动生成替代 GUID 或实现旧格式加载器。Library 目录版本 3 与运行目录版本 2 需要由显式导入重建。

## 候选构建与正式发布

项目导入服务使用冻结的源视图和私有 Library 写入层运行原生生成器。构造期间的目录和产物仅存在于候选层；处理失败或源输入变化时整体丢弃，正式 Library 不会提前出现新候选。校验完成后，服务复核输入指纹，再一次发布主资源和子资产集合。

生成过程中新增的独立依赖产物也记录在同一候选批次中，例如模型转换导入的外部纹理。每个来源的输入和字节均复核后，一次写入目录；任一来源或目录提交失败，整批保留旧目录并清理新增候选文件。源视图只允许原生生成器更新本次主资产的元数据，依赖源及其元数据保持只读，不能把依赖默认值写成新的源输入。

正式产物路径同时包含输入指纹和产物内容指纹。即使 Force Reimport 在相同输入下产生不同字节，也会使用新的文件路径；目录保存失败不能覆盖仍被旧资源引用的文件。完全相同的输出集合复用原路径和目录，不重复写入。

Shader SDK 的临时文件仍由受控外部编译器边界管理；候选 Library 没有本机目录时使用编译器私有临时目录，不把内存 provider 伪装成本机路径。

## 项目服务与重导入入口

每个 ProjectWorkspace 拥有一个 `Importer()` 服务，项目准备、后台刷新和显式 Reimport 复用它。纹理/模型设置视图显式接收该服务；Shader 视图通过工作区服务读取正式产物，不另外创建不含项目注册的临时 importer。

注册采用不可变的共享快照；更新 ScriptableObject 类型目录或新增原生生成器时创建新一代快照。导入期间固定使用开始时的注册，提交前发现注册已替换则拒绝旧候选。注册更新不会使正在使用的 callback map 或迭代器失效。

## 模型 CPU 候选

模型导入器版本 7 使用 `BuildCandidate` 分离构造与编码。节点和局部变换、Mesh、材质参数、纹理引用均保留为 CPU 对象，内嵌纹理尚未编码；骨骼保留完整关节数据，动画与骨骼归档在最终编码时统一输出。全部产物按稳定 key 分配 ID，模型主对象也使用非零 ID；主对象选择仍可使用零 ID 查询别名。

候选允许调整节点、顶点、材质和绑定后再 `Encode`，校验层级连通性、环、引用、有限变换、几何数据及输出身份。被动画或骨骼引用的节点不能被无效删除。`UrsaModelImportCandidateTests` 用 OBJ、动画 FBX 和内嵌纹理 FBX 验证延迟编码、修改进入实际产物、非法修改拒绝以及完整 CPU 骨骼。

## 托管 Import 作用域

`DotNetHost::Connect` 在所属线程建立独立宿主端点，共用进程 CLR。每个线程独立保存绑定、回调、脚本会话、行为实例、ScriptableObject 和管线选择；会话及实例 ID 在进程内唯一。同一线程不能同时持有两个活动端点，关闭端点会释放该线程的会话及反向回调，不影响其他线程。

`CreateImportSession` 创建独立的 Import 脚本会话；普通 Editor/Play 会话不能进入导入调用。`ImportInvocation` 持有一次候选事务的源路径、会话、规则 generation、唯一调用身份及当前阶段。`Import` 正向能力与宿主 Begin/End 反向入口均来自 `.bindings.cs` 生成，托管入口进入前核对原生作用域身份。导入期间不能重入或替换为其他会话的绑定。

候选对象由作用域持有，句柄包含独立存储身份、种类及槽位代数。删除对象、复用槽位、改变规则 generation、完成事务或异常退出均不能让旧句柄恢复有效；跨事务及跨线程访问被拒绝。作用域结束时撤销能力并释放候选；宿主关闭也清理未显式结束的托管导入调用。ScriptedImporter 与纹理、模型处理器共用这一生命周期；Editor 与 CLI 的导入调度通过独立工作线程调用它。

## Import CPU 资源 API

每次 `ImportInvocation` 拥有独立的 CPU Scene API，宿主通过 `BindScriptSession(session, invocation.CpuApi())` 绑定本次候选存储，再进入 Begin/End。已有 `Mesh.Create`、顶点/索引修改、`Material.Create`、颜色及 Shader 参数操作复用 Runtime 生成绑定；这些对象不需要窗口、Device 或 GPU 上传。Runtime 能力也复核事务的规则 generation，过期后不能继续修改 Mesh/Material。结束事务会撤销 CPU 能力及其资源存储。

`new Texture2D(width, height, linear: false)` 只在活动 Import 调用中创建 RGBA8 CPU 候选；`Width`、`Height`、`IsLinear` 读取创建时的尺寸和颜色空间，`GetPixels()` / `SetPixels(Color[])` 读取或整体更新像素。像素数组必须与尺寸相符，通道必须有限；通道按 RGBA8 限幅并量化，设置像素不进行额外的 sRGB/Linear 数值转换。颜色空间作为纹理元数据随最终编码保存。修改同步生效，无需 GPU `Apply`。超出内存预算、空尺寸、无效数组或非有限像素会失败，原候选不被部分修改。

候选纹理继承 `Texture`，其 `IsValid` 在跨线程、不同调用或事务完成后返回 false；像素访问会拒绝这些状态。新建候选起初没有持久引用，编码前由上下文按稳定输出 key 分配 GUID/local ID。

## 临时层级与 ScriptableObject 引用

Import CPU API 的 GameObject、Transform 和原生组件操作只访问事务私有 Scene。MeshFilter/MeshRenderer 可绑定同一候选存储里的 Mesh/Material；层级可通过现有 Prefab 序列化写出。导入不调用 Scene 的游戏帧更新，创建、修改、移除及清理临时组件不运行游戏生命周期。

独立 Import 会话可创建 ScriptableObject，并使用同一字段 schema 序列化其 Mesh、Material、Texture2D 及其他临时 SO 引用。字段归档中的内部候选引用包含调用身份、不可复用的引用代数和 token；同一调用内可恢复到原对象，结束后或跨调用恢复会失败。临时引用不要求项目脚本预先拼接内部资源文件，也不冒充已经发布的持久引用；最终输出 key 到资产身份的转换由上下文统一发布完成。普通持久资产及文档字段的既有校验继续生效。

宿主的 SO 创建、字段读写及引用描述入口同样通过 `.bindings.cs` 生成。结束时先清理 SO，再释放托管候选引用和绑定；失败或未显式结束的调用随 Import 端点关闭清理。

## Import 能力门槛

Import 的 Runtime 能力允许 CPU 资源和临时层级操作，拒绝游戏帧/输入、窗口、Behaviour 执行、GPU 资源及渲染调用。即使误装入了 FrameContext，也不能取得游戏帧能力。Runtime 的通用 VFS 读写在 Import 中被拒绝；导入依赖读取通过后续 AssetImportContext 入口追踪。

Import 会话不能准备或调用 GUI 扩展、切换渲染管线、渲染场景、替换当前绑定或同步重入另一次导入。托管管线管理入口也直接检查导入作用域。跨事务候选、过期包装和旧引用归档不能在后续调用中复活。

纯原生配置继续使用提交的生成快照，不要求托管工具。验证配置为 `URSA_ENABLE_DOTNET=OFF`、Editor 开启，并将 dotnet 路径设为不存在的文件，Release 构建及 Import CPU 测试均可通过。

## 上下文与统一发布

`ReadSourceBytes()` 读取本次导入冻结的主源文件副本；`ReadSourceText()` 按严格 UTF-8 解码并移除 UTF-8 BOM。`AddObjectToAsset(identifier, object)` 登记 SO、Texture2D、Mesh、Material 或临时 GameObject 根；GameObject 输出使用标准 Prefab 编码。`SetMainObject` 选择已经登记的对象。键非空且在集合内唯一，同一个对象不能登记为多个输出；未指定主对象或引用未登记候选时，整次导入失败。

登记顺序和对象显示名称不参与输出资产身份。编码前，服务为完整输出集合分配 GUID/local ID，将 SO/纹理引用转换成持久引用，并绑定 Mesh/Material 身份；项目代码只操作 Ursa 对象，无需拼接内部文件或归档。Prefab 节点、组件和内嵌局部资源按源身份及逻辑层级分配确定性 ID，相同生成逻辑的强制重导入产生相同字节。托管调用结束并成功清理后，才通过项目导入服务的候选事务发布完整集合。

完整可编译示例为 [GeneratedFormatAsset.cs](Examples/GeneratedFormatAsset.cs) 和 [DatasetImporter.cs](Examples/Editor/DatasetImporter.cs)，分别放在 `Assets` 与 `Assets/Editor`。`*.ursadataset` 的输入例如：

```json
{"size": 2, "label": "Generated dataset", "main": "prefab"}
```

它生成数据 SO、二维纹理、Mesh、Material 和 Prefab。将 `main` 改为 `data` 可选择 SO 为主对象；其余产物仍通过稳定 local ID 访问。运行侧不需要 Editor 程序集：`Texture2D.Load` 可读取已加载纹理的尺寸、颜色空间和像素，持久纹理保持只读。主 SO 的零 ID 查询别名会在图加载时规范化，重复加载返回同一实例。

## 源依赖、产物依赖和诊断

`ReadSourceAssetBytes(path)` / `ReadSourceAssetText(path)` 自动记录源文件依赖；`DependsOnSourceAsset(path)` 显式声明同样的内容依赖。路径使用 VFS 资产或配置路径，例如 `prj:Config/Import.txt`。文本读取严格验证 UTF-8。源文件缺失或在导入期间变化会拒绝本次候选，即使脚本捕获了读取异常也不能把缺失输入当作成功。

`DependsOnArtifact(AssetReference)` 声明已导入产物依赖，`ReadArtifactBytes(reference)` 读取并自动声明该依赖。零 local ID 查询依赖的当前主对象，非零 ID 查询准确子资产。上下文不暴露 Library 路径；文件内容仅用于需要读取编码后输入的场景，创建输出仍使用类型化 Ursa 对象。

产物依赖使用完成产物的内容指纹。依赖过时或尚未导入时，当前候选被丢弃，托管调用完整结束后由项目调度器先导入依赖，再重试当前源。回调不能同步重入 importer。缓存查询也检查传递依赖，源、规则改变时重新发现依赖集合，删除的声明不会永久保留。依赖重建但字节未变时，使用方仍可命中缓存。缺失 GUID、缺失子资产、自依赖或跨资产循环失败，诊断包含依赖链；所有候选发布前再次复核输入。

`LogImportWarning(message)` 将源、扩展类型和回调阶段附加到返回诊断；`LogImportError(message)` 同样记录诊断，并阻止完整候选集合发布。错误不会删除上次成功产物，也不会停用规则后静默跳过它。其他资产可继续导入。

可编译的 DatasetImporter 示例支持可选 `sourceDependency`、`declaredSourceDependency`、`artifactGuid`、`warning` 和 `error` 输入，演示读取、声明和报告的用法。

`UrsaScriptedAssetImporterTests` 在真实 CLR 下验证上述依赖、缓存内容语义、缺失与循环、警告及显式错误的原子失败，不需要窗口或 GPU。

## 验证记录

模型前处理将 `AssetImporter` 转成 `ModelImporter` 后，可修改 `ScaleFactor`、法线/切线生成、UV 翻转、材质/纹理导入和最大骨骼影响数。这些值只影响本次候选，不保存到源 `.meta`。模型后处理收到私有根 GameObject；`GetChildren()` 和 `GetComponentsInChildren<T>(includeInactive)` 可遍历原节点、静态和蒙皮渲染组件，通过现有 Transform、Mesh 和材质 API 修改结果。增加、删除和改名保留现存源节点身份，新增节点与资源按逻辑路径生成稳定 key；同一 Mesh 在不同节点使用不同材质槽时分离对应输出，保留有序空槽。

骨骼和动画归档在后处理结束后统一编码；删除仍被骨骼或动画使用的节点会拒绝候选。模型目录格式 2、导入器版本 7 使用材质槽数组，只读取当前格式。模型外部纹理依赖由项目服务先完成其处理链，随后重新构建模型候选；模型缓存追踪完成纹理内容，纹理重建但内容不变时仍可命中。

纹理与模型处理器抛异常、报告错误或留下非法候选时，都拒绝整个事务并保留最后成功集合。错误包含源、处理类型和具体前/后处理阶段，回调失败不会停用必需规则；其他资产仍可导入，修复后重新执行规则。真实 CLR 测试覆盖前后处理错误、非法像素/索引、删除模型根/骨骼节点、重试和独立资产继续处理。

可编译示例 [TextureImportRules.cs](Examples/Editor/TextureImportRules.cs) 和 [ModelImportRules.cs](Examples/Editor/ModelImportRules.cs) 分别演示 `GeneratedTextures/` 下的 PNG 规则和 `Models/Processed/` 下的模型规则，复制到项目 `Assets/Editor` 即可被目录发现。它们通过测试项目实际编译，与 DatasetImporter 示例共用同一注册与候选处理机制。

纹理处理先从持久设置复制本次有效设置。`OnPreprocessTexture` 可将 `AssetImporter` 转成 `TextureImporter`，修改 `TextureType`、`SRgb`、`Alpha`、`GenerateMipmaps`、`FilterMode`、`WrapMode` 和 `MaxDimension`。非默认纹理类型自动关闭 sRGB；尺寸必须在 1–16384 之间。设置只在前处理阶段可写，后处理可读取它们，超期访问会失败。

解码和尺寸限制使用有效设置。`OnPostprocessTexture` 收到本次可写的 CPU `Texture2D`，后续处理器能观察前序修改；同一处理器实例保留从前处理到后处理的字段状态。全部后处理成功后，才从修改过的基准像素生成最终 Mip 并编码。候选不写回源图或 `.meta`，有效设置单独记录在导入缓存中。处理器通过上下文读取的配置与源/产物依赖参与相同调度和失效机制。

`AssetPostprocessor` 使用明确的虚方法：`OnPreprocessTexture()`、`OnPostprocessTexture(Texture2D)`、`OnPreprocessModel()` 和 `OnPostprocessModel(GameObject)`。发现时要求公开、具体、非泛型类型和公开无参构造函数；必须重写至少一种处理回调。`GetVersion()` 默认为 0，`GetPostprocessOrder()` 默认为 0，二者用于冻结注册描述，不应依赖导入上下文。

系统按实际重写回调确定纹理/模型适用范围，按优先级升序、稳定脚本身份的序号字符串顺序执行。相同优先级与程序集发现顺序无关；处理器改名沿用脚本元数据身份。项目处理器不应用到 `ursa:` 引擎源，也不应用到无关原生格式。版本、适用范围、排序、增删和编译闭包都参与对应规则指纹。

发布前统一拒绝重复 key、同一对象多次登记、无主对象、失效或已销毁的资源、非法 Mesh 索引/像素，以及引用了未登记临时候选的 SO。SO 输出必须出现在当前会话的 Game 类型目录；Editor 目录中的类型即使可在导入时创建，也不能成为运行产物。类型检查在序列化前执行，程序集卸载时释放校验目录中的 Type 引用。

`UrsaScriptedAssetImporterTests` 包含上述负例，每个失败都核对旧产物的 local ID 和完整内容指纹，并验证失败后仍能重试规则。成功输出随后由独立的非 Editor 会话加载其 SO、纹理、Mesh 和材质引用。

`UrsaAssetImportRulesTests` 是无窗口、无 GPU 测试，覆盖后缀冲突、缺失 importer、CLR 改名、处理器排序/适用范围/指纹、主对象目录持久化、产物重排、移动、清空 Library 重建、子资产删除、无效发布以及 Loose/Pak 主对象别名。`UrsaEditorBindingsTests` 验证非零主对象的选择描述不会被当作子资产。

`UrsaImportSettingsDraftTests` 覆盖默认设置、草稿编辑/Apply/Revert、有效设置副本、校验失败、外部修改、只读写入失败、脚本类型改名和损坏/旧版本元数据恢复；失败期间保留原 GUID 和未应用编辑。

`UrsaAssetImportCandidateTests` 注入候选完成后的处理失败、生成中源变化和最终目录写入失败，验证候选隔离、完整集合提交、旧字节保留、失败清理及相同输入强制重导入的内容寻址。

`UrsaImportInvocationTests` 不需要窗口或 GPU，验证生成能力的身份、阶段、跨线程/跨事务拒绝、旧句柄、规则失效、ABI 漂移和异常清理。`UrsaManagedProtocolTests` 验证两个宿主线程的对象、管线和反向回调相互隔离；`UrsaDotNetHostSmoke` 在真实 CLR 下验证独立线程端点、Import Begin/End、重入/错误代数拒绝、未结束调用的清理及重新连接。

CPU 资源用同一无 GPU 测试直接调用生成的 Runtime/Import 入口，验证 Mesh 创建和修改后保存/加载、Material 修改后归档恢复、sRGB/Linear Texture2D 像素往返及实际纹理文件解码，并验证错误像素输入和过期 Runtime 能力拒绝。

`UrsaImportInvocationTests` 还验证临时层级、节点变换、Mesh/材质绑定、Prefab 序列化、游戏生命周期计数为零及作者/Play Scene 保持原状。`UrsaManagedImportSessionTests` 使用真实 CLR，单独运行 Import 会话、SO 创建/验证、资源引用字段往返和旧归档拒绝；测试入口不创建窗口或 GPU，也不依赖内容准备 fixture。


## Editor 与 CLI 冷启动

完整项目导入先编译 Game 和 Editor 脚本，发现不可变规则快照，再导入项目资源。规则在专用 CPU 线程上的独立 Import 会话执行；它不借用 Inspector、作者 Scene 或 Play 会话，也不创建窗口或图形设备。Editor 的 `ProjectWorkspace::PrepareManagedImports` 和 `UrsaAssetImport` 使用同一准备与注册路径。Engine 资源继续使用原生规则。

```powershell
UrsaAssetImport --project <project> --engine <engine-assets> --configuration Debug --profile Player
```

`--no-scripts` 要求当前配置的 Editor 规则已编译且全部输入仍有效。脚本或程序集闭包改变后，旧工作线程立即失效；必需规则编译或注册失败会使请求失败，保留上次完整产物。修复脚本后重新准备规则，不通过跳过扩展来恢复导入。Debug 与 Release 的框架读取使用各自的 VFS mount，避免同一 workspace 切换配置时混用 SDK。

代码闭包、importer 身份/版本以及排序后的处理器身份/版本/顺序都参与缓存；generation 只用于运行期撤销，不进入持久资产身份。增加或移除类型可能保留其 `.meta` 稳定身份记录，因此修复失败注册后的第一次请求可能重新导入，之后相同输入可以命中。

C# 编译只读取项目源脚本和已配置的框架程序集。importer 不能接管 `.cs` 或 `.dll`；需要本轮 importer 先生成 C# 类型才能编译该 importer 的链条构成编译/导入循环，必须先解除该依赖。

有效规则发现会更新 `ProjectSettings/ManagedImportRules.json`。该文件记录脚本源清单与必需的托管规则，应随项目源纳入版本控制。关闭 .NET 的工具可以导入没有脚本的原生项目；对于已确认无导入规则且脚本未变化的项目，也可继续原生导入。必需规则、缺失清单或清单后的脚本变化会明确报错，要求使用启用 .NET 的工具重新发现规则，不能把自定义格式默认拷贝为成功产物。只读元数据中的 `script:` importer 同样会阻止静默降级。

`UrsaManagedAssetImportWorkflowTests` 创建隔离项目，在 CLI 完成导入后移开其 Library，再通过 Editor workspace 从空 Library 重建。它比较自定义格式、真实 PNG 和带骨骼动画 FBX 的输入指纹、全部输出 ID 与内容哈希，并核对处理后的像素和 Player 清单中的 Editor 代码边界。测试不创建窗口或 GPU。


## Importer 设置 Inspector

`AssetImporterEditor` 和 `ScriptedImporterEditor` 沿用现有 `SerializedObject` / `EditorGui.PropertyField`。默认实现绘制全部字段和 Apply/Revert；自定义实现可以使用 `Target` 取得当前类型的 importer，也可以调用 `ApplyRevertGUI()` 复用控件。`TextureImporter` / `ModelImporter` 属性在前处理回调中访问本次有效设置，在 Inspector 回调中访问当前设置草稿。

[DatasetImporterEditor.cs](Examples/Editor/DatasetImporterEditor.cs) 是实际编译、执行过的示例。配套 DatasetImporter 的公开 `scale` 字段持久化在源 `.meta` 的 importer 设置中，并在下一次导入回调前恢复。字段值参与缓存，调整为 2 会让输入中的尺寸乘以 2；修改设置不会改变主/子资源身份。

Inspector 字段提交只更新草稿。Apply 校验并保存元数据，然后请求项目导入；导入失败会保留已成功保存的设置和此前完整产物。元数据写入或验证失败则保留未应用草稿。Revert 回到持久设置，不请求导入。`SaveAndReimport()` 只用于活动 importer Inspector，导入回调不能同步调用它重入。

设置保存使用项目导入写入租约，不能与另一批发布同时改写元数据。草稿恢复数据存于 Library，既不进入源 `.meta`，也不进入运行内容。关闭/重载后可恢复未应用编辑；持久设置或稳定 importer 身份冲突会保留恢复数据并诊断，不能自动覆盖新设置。旧回调的 importer/SerializedProperty 包装不能用于新的目标。

`UrsaAssetImporterInspectorTests` 通过真实 ImGui 字段和 Apply/Revert 控件验证上述保存门槛、非法设置、写入失败、导入失败、只读、恢复和租约；`UrsaManagedImporterInspectorTests` 在真实 CLR 下验证类型化目标、过期包装、元数据及活动文档隔离；`UrsaScriptedImporterInspectorWorkflowTests` 从空 Library 编译文档示例，调用 ScriptedImporterEditor 并验证保存的字段进入生成 SO 的运行数据。


内置 PNG/模型设置嵌入同一 Inspector 属性区域，共用 `AssetImporterInspector` 的默认字段、CustomEditor 分派及 Apply/Revert 命令。已有的 Source/Imported 纹理与模型层级信息仍由对应视图提供。已创建设置草稿后，显式 Reimport 读取持久元数据，不会用旧视图快照覆盖刚保存的设置。

切换到其他资产、Hierarchy 对象或清空选择时，未应用设置会显示 Apply / Discard / Cancel。Apply 在允许保存且成功写入后切换并排队导入；Discard 只删除草稿及 Library 恢复记录；Cancel 保持原选择和草稿。导入、脚本工作或 Play 期间禁用 Apply。切回独立 Prefab 的对象会恢复其 Prefab 层级选择。恢复记录位于 `Library/EditorDrafts/Importers/<guid>.json`，兼容的 importer 算法版本更新不会单独阻止恢复；稳定身份或持久基线改变则报告冲突。


### 类型化读取已导入资源

Import 回调可通过 Mesh.Load、Material.Load、Texture2D.Load 和 ScriptableObject.Load<T> 读取已完成产物。读取器沿用上下文的产物依赖跟踪、就绪校验及缺失/过期依赖调度，不回退读取未经规则处理的源文件。无窗口创建的内置材质使用 CPU Shader schema；不要求分配 GPU 资源。

已发布 Mesh/Material 的写入、销毁和直接注册为新输出会被拒绝；Clone 后的独立候选可编辑、注册和编码。持久 ScriptableObject 不能重新注册为输出或销毁，回调完成时还会核对其字段，修改持久对象会使整个导入失败并保留此前完整产物。持久引用可以作为新候选的引用保存，使用原 GUID/local file ID。


生成的纹理可直接用于生成材质：`material.SetTexture(MaterialTextureSlot.BaseColor, texture)` 或内置 Unlit 的 `material.SetTexture("_BaseMap", texture)` 接受本次 `Texture2D` 候选。材质与纹理都需要 `AddObjectToAsset` 注册；最终发布统一解析稳定身份，因此可在注册前绑定，也可在绑定后继续修改像素。外来/过期候选和未注册纹理使整次导入失败；已发布纹理仍使用持久资源引用。可编译 DatasetImporter 示例版本为 5。
