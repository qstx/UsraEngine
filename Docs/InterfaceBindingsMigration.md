# 接口绑定与公开 API 迁移

本次以 C# `.bindings.cs` 定义绑定，生成跨语言机械代码。Unity 风格主要体现在属性、值类型、资源和编辑器服务的使用方式；namespace 仍为 Ursa，成员使用 PascalCase。当前迁移进度见 [覆盖矩阵](InterfaceBindingsCoverage.md)。本表描述目标契约，未完成条目不能当作已交付接口。

已实施的对象/组件包装保留公开职责与现有单位，底层改走 `NativeRuntime.bindings.cs`。泛型组件查询使用一次返回的句柄快照；Transform 与 Camera 投影使用固定值，不再搬运 10/3 元素的临时数组。Camera.RenderingSettings 直接传递颜色和 Viewport；Viewport 保留四个 float 与原构造函数，增加可写分量用于值契约。保存的字段名与资产身份不变。其余表项按覆盖矩阵继续迁移。

Mesh、Material、Texture、Renderer/MeshFilter 资源引用及 ComputeShader 元数据查询已使用具体方法和 wire 值。Material 的 Float/Range/Int/Color/Vector/Texture2D 分别有 getter/setter，类型不匹配在修改前失败。纹理属性传 null 会移除覆盖并使用 Shader 默认值；`RemovePropertyOverride` 也可显式恢复默认。材质数组保留 null 槽位及 shared/instance 的复制规则。

资源序列化使用 `Serialization.bindings.cs`：Mesh/Material 分别提供快照与恢复，脚本资产图有单独的加载入口。`ResourceReferenceArchive` 与 `ScriptableAssetGraphArchive` 均带 Version，内部 JSON 只承载已有持久化数据，不含 operation 分派。行为字段沿用 `BehaviourFieldArchive`，稳定身份和未知字段由原序列化服务处理。

## 公开接口映射

| 原接口/形态 | 目标用法 | 保持的语义与验证 |
| --- | --- | --- |
| `GameObject.Name/Layer/Parent/ActiveSelf` | 保留属性形式，调用生成的具体 getter/setter | Layer 为 0–31；父子/活动规则及会话句柄验证保持 |
| `Transform.Position/LocalPosition/LocalRotation/LocalScale` | 使用 Vector3/Quaternion 值的生成绑定 | 世界/局部空间不变；不再每个 getter 搬运十个浮点 |
| `CameraComponent.VerticalFovRadians/NearPlane/FarPlane` | 保持明确单位的投影属性和类型化设置 | FOV 保持弧度，近远裁剪面距离语义不变 |
| `CommandBuffer.DrawMesh(..., float[16], ...)` | `DrawMesh(..., Matrix4x4, ...)` | 固定矩阵值，明确字段顺序及 native/HLSL 转换；subMesh/pass 规则保持 |
| `SetGlobalMatrix/SetComputeMatrix` 的数值数组 | Matrix4x4 值 | 非对称矩阵与 GPU 像素验证行列关系 |
| `DrawingSettings.CameraPosition` 的 float 数组 | `Vector3?` | null 表示沿用相机位置，三个元素的数组长度不再成为运行时约定 |
| `Mesh.Vertices/Normals/Uv0/Colors/Tangents/Indices` 等数组 API | 类型数组或显式 Span 重载；生成连续数据封送 | 读取为副本，修改通过 setter/更新方法触发版本变化 |
| `Material` 的值、关键字、Shader 和纹理访问 | 属性或类型明确的 Get/Set 方法 | Shader 属性种类检查、共享/实例材质以及稳定资源引用保持 |
| `EditorContext.Selection/Document/Assets` 的 JsonElement | 明确的选择/文档/资产描述与只读集合 | 区分对象、组件、资产/子资产及所属编辑作用域 |
| `EditorGui.ObjectField/AssetField` 的 JsonElement | 声明允许种类的编辑引用类型 | null、错误种类及跨会话引用在应用前校验 |
| `SerializedProperty.Value/SetValue` 的 JsonElement | `PropertyType` 和匹配的 Bool/Integer/Float/String/Enum/Vector/Color/ObjectReference 访问器 | 不要求解析 JSON；错误种类访问不修改对象 |
| 属性集合通过 JSON 数组访问 | 大小与元素属性访问 | 一维数组/列表顺序、空值和引用种类保持 |
| 自动属性写入/应用 | `SerializedObject.Update`、typed setter、`ApplyModifiedProperties` | 刷新当前快照；成功应用才改持久对象、Dirty 与 Prefab 覆盖；刷新丢弃未应用变化 |

## 接口与数据的边界

普通属性、对象、资源、渲染和 Editor 调用由具体方法与参数类型确定，不再发出 `{ operation, ... }` 请求。专用序列化入口可以传递声明用途、格式和版本的 archive 数据，用于场景/Prefab、脚本资产图及重载快照；archive 不选择任意业务操作。

迁移仅改变源码接口和内部 ABI，不改变 `.meta` GUID、资源 ID、脚本稳定类型 ID、local file ID 和持久字段名称。旧资产必须能通过现有重建/导入流程恢复。原有坐标、角度单位和矩阵约定不能因对齐命名而静默改变。

脚本内容清单升级到 Version 2 并包含 RequiredBindings；Version 1 清单需要重新导入/编译，Scene/Prefab/脚本资产文件不升级格式。绑定身份和 Ursa.Bindings 支持程序集参加编译指纹。具体快照更新和完整重建命令见 [脚本发布](ManagedDeployment.md)。

## 作者与调用方

普通绑定作者维护 `.bindings.cs` 中的声明和 C++ 业务适配器；生成器维护函数表、注册、布局、桥接及错误转换。项目脚本继续通过 Engine API 开发，不增加项目自有 C++ 源码或绑定生成步骤。

本次允许不兼容的公开源码调整；所有 Engine 示例、Demo runtime/Rendering/Editor 脚本和测试夹具在交付前同步迁移，不用两套长期 API 名称掩盖缺失能力。后续各模块完成时，在覆盖矩阵记录实际声明与验证结果。
