# CAD 查看器渲染、材质、光照与 KeyShot 风格调研

**调研日期：** 2026-07-18  
**目标：** 在实时 OpenGL CAD 检查器中提升“高级感”，同时保持边界清晰、测量可信和大模型性能。

## 结论先行

Cadly 不是没有 PBR：`pbr.frag` 已实现 Cook-Torrance、GGX、split-sum IBL、三盏方向光、
法线/深度边缘增强和 MSAA；启动时还会用程序化 studio environment 烘焙 irradiance、prefilter
和 BRDF LUT。当前看起来平/灰/廉价的原因是灯光和材质的“信息量”不足，而不是把 Phong 换成
PBR 就能解决：

- 默认材质是 metallic=1 的各向同性银色；没有 tangent/anisotropy，所以它不是实际的 brushed
  metal，只是 frosted metal。
- 没有地面、接触阴影、屏幕空间 AO 或可靠的 shadow map，零件悬浮，凹槽缺乏尺度线索。
- 环境是 shader 中的三个球形高光 lobe，不是可旋转、可曝光、可预滤的真实 HDRI studio。
- PBR 在 shader 中直接 tone-map/gamma，主渲染目标是 `RGBA8` MSAA；没有 HDR 合成、标准
  ACES/AgX 色彩管理或统一曝光/白平衡。
- 法线导数暗边与 BRep edge overlay 同时工作，容易得到“卡通黑边”而不是工业绘图线。
- STEP 颜色只映射为颜色材质，没有提取 XCAF visual/physical material、透明度、纹理、层或
  实例级样式。

## 主流实时/产品渲染的共同做法

### 1. 线性工作流和标准色彩管理

基本链路应为：

```text
sRGB UI/file colors -> linear scene values -> HDR lighting (RGBA16F)
-> exposure/white balance -> ACES/AgX-like tone map -> sRGB output
```

关键点：

- 颜色、光源、IBL 都在 scene-linear；输入颜色要明确文件约定，不能一部分当 sRGB、一部分
  当 linear。
- 材质和光源的能量在 HDR 阶段合成；最后才 tone map 和 gamma encode。
- 曝光应是环境/相机状态，不要把 `color = pow(...)` 分散在 background、PBR 和 overlay shader。
- ACES/AgX 不是“更亮”，而是高光滚降、饱和色保护和中间调对比的统一规则。

可参考：

- [Khronos glTF 2.0 PBR metallic-roughness](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)
- [Google Filament Materials and Lighting](https://google.github.io/filament/Filament.html)
- [Academy Color Encoding System (ACES)](https://www.oscars.org/science-technology/sci-tech-projects/aces)
- [UE4 Physically Based Shading model](https://blog.selfshadow.com/publications/s2013-shading-course/)

### 2. PBR 材质不是默认全金属

金属工作流最重要的约束是：

- dielectric 的 F0 通常约 0.02-0.08，base color 影响漫反射；
- metallic=1 时几乎没有 diffuse，base color 是 specular tint；
- roughness 控制反射 lobe 宽度，不能用它代替法线、纹理和微表面；
- 透明、clearcoat、sheen、anisotropy、normal/roughness map 是不同现象，不能用一组
  `metallic/roughness` 近似全部。

对“未知 CAD 材质”，行业查看器通常采用中性 clay/painted plastic 预设，让几何和边缘先可读；
只有当来源明确是铝、钢、橡胶或玻璃时才赋予金属/透明语义。Cadly 当前把未着色模型默认为
`Material::brushed_metal()`（[Material.h](../../src/scene/include/cadly/scene/Material.h#L14)），
这会让没有丰富环境反射的场景变成一块灰银板。

推荐预设（数值是起点，全部为线性 scene value）：

| 预设 | base color | metallic | roughness | 用途 |
| --- | --- | ---: | ---: | --- |
| Neutral Clay | 0.55-0.70 灰 | 0.0 | 0.38-0.48 | 默认检查，突出轮廓和凹槽 |
| Painted Metal | 来源色 | 0.05-0.20 | 0.32-0.48 | 涂装零件，颜色稳定 |
| Aluminum | 0.75-0.95 灰 | 1.0 | 0.18-0.32 | 明确的金属件 |
| Machined Steel | 0.45-0.65 灰 | 1.0 | 0.25-0.38 | 钢件；需要环境反射 |
| Rubber | 0.03-0.08 灰 | 0.0 | 0.75-0.90 | 轮胎/密封件 |
| Glass | 颜色 + alpha | 0.0 | 0.03-0.15 | 单独透明排序/折射路径 |

真正的 brushed metal 需要 tangent、各向异性 GGX 和方向一致的微表面纹理；在没有这些数据
   时应改名为 `satin_metal`，避免产品承诺与画面不符。

### 3. Studio lighting 的“高级感”来自反射形状

KeyShot 和类似产品的金属质感依赖可读的反射卡，而不只是三盏点光：

1. **大型 softbox key：** 形成长而柔的白色高光，展示圆角、倒角和曲率。
2. **宽 fill：** 保留阴影侧的颜色和形状，不能把黑面抬成无方向灰。
3. **窄 rim/kicker：** 描轮廓，但不能在每条边上画一圈黑线。
4. **中性 HDRI：** 地面暖、天顶冷、水平线中性；可旋转、曝光和强度；prefilter 与 roughness
   一致。
5. **地面和 shadow catcher：** 给装配一个接触参考，阴影透明度可调。

关键不是把 key_color 设得更亮。当前程序化环境在
[env_capture.frag](../../shaders/glsl/env_capture.frag#L17) 中用 `pow(dot, 14)`/`pow(dot, 6)`
产生两个 blob，方向无法在 UI 调整，也没有真实矩形面积的反射边界。应把环境作为资产（HDRI
或离线生成的 KTX2），把旋转、曝光、温度和地面强度作为可持久化 scene settings。

### 4. 接触关系和边缘要分层

建议的实时 pass 顺序：

```text
depth prepass
-> shadow maps (key / cascaded or focused)
-> opaque PBR in HDR
-> GTAO/SSAO (small radius, low intensity)
-> ground/contact shadow
-> selected highlight / BRep edges / hidden-line
-> transparent pass
-> tone map + color transform + TAA/SMAA
-> HUD/scale/axes (sRGB overlay)
```

AO 只应增强接触和凹槽，半径按模型包围球的 0.5%-2% 起步；过大或过黑会把 CAD 接缝渲染成
脏污。边线应使用真实 BRep edge 和深度偏移，screen-space normal/depth edge 只在没有 BRep
拓扑的网格模式启用。当前 `pbr.frag` 的 feature-edge enhancement（见
[pbr.frag](../../shaders/glsl/pbr.frag#L181)）会把颜色乘到 0.55，再叠加 `draw_edges`，建议
在 Shaded 默认关闭或降低到 0.05-0.15，并在 Hidden Line/Wireframe 独立控制。

## 对当前实现的逐项 review

### 已做对的部分

- Cook-Torrance + GGX、金属/粗糙度和 IBL split-sum 的结构正确，远好于简单 Lambert/Phong。
- 方向光保持世界固定，用户旋转相机时光照关系会改变，能显示曲面起伏。
- `GL_RGBA16F` 的 IBL 目标与 irradiance/prefilter/BRDF LUT 分工合理。
- BRep 边与面三角形共享顶点，`glPolygonOffset` 处理 shaded-with-edges 的深度冲突，思路正确。
- renderer 负责 scale bar、axes、pivot，符合“viewport overlay 不用 QPainter”的架构要求。

### 直接导致画面不够好的问题

1. **默认材质语义错误。** `metallic=1` 会丢漫反射，环境不够丰富时平面只剩灰色反射；改为
   Neutral Clay/painted dielectric 默认，并把 aluminum/steel 作为显式预设。
2. **材质颜色可能被重复相乘。** importer 把默认/面颜色写入 `Vertex::color_rgba8`，又为面
   分配 `Material::base_color`；shader 以 `u_base_color * v_vertex_color` 合成。彩色面会被
   二次 tint，实例色和面色也容易互相污染。应选择一个颜色来源，或明确 `vertex_color` 是
   乘数而不是 base color。
3. **XCAF 材料没有映射。** `STEPCAFControl_Reader::SetMatMode(true)` 只打开读取；当前 scene
   只取颜色，没有 PBR/common material、alpha mode、double-sided、texture 或物理材料。
4. **没有透明路径。** `Material` 有 alpha，但 opaque pass 不启用 blend、排序或 refraction；
   玻璃/透明层会当作不透明颜色。
5. **没有地面/阴影/AO。** 截图中的零件悬浮，接触和装配间隙只能依赖边线，缺少 KeyShot 风格
   的重量感。
6. **没有 HDR 后处理。** `pbr.frag` 在片元中直接 tone-map、clamp、gamma；MSAA FBO 是
   `GL_RGBA8`。高光在片元阶段就被压扁，无法在曝光变化后重新合成。
7. **启动时烘焙 IBL，且每次运行重复。** irradiance 每个 texel 约 25k 次采样，prefilter 每
   个 texel 1024 次，属于启动 GPU stall；`bake_*` 还没有逐目标检查 FBO complete。应离线烘焙
   KTX2/GLI 资产，或按 GPU tier 缓存。
8. **边线增强叠加。** normal/depth derivative 暗边和 BRep edge 两套轮廓同时生效，造成卡通
   黑边/脏边；需要按 display mode 互斥并校准 edge intensity。
9. **环境贴图分辨率和控制不足。** 128 cubemap、5 个 prefilter mip，粗糙面反射层次有限；
   没有环境旋转、强度、白平衡、HDRI 选择和纹理导入。
10. **无抗闪烁的稳定时间滤波。** MSAA 解决几何边缘，但旋转时 IBL 高光、细线、AO/阴影仍会
    闪烁；大型装配需 TAA 或至少 SMAA/temporal reprojection。

## 推荐的实现路线（不改变 scene/renderer 边界）

### Phase A：一到两个迭代的高收益改进

1. 把默认 preset 改为 Neutral Clay；保留 `Aluminum`/`Steel`/`Painted Metal` 菜单，并给
   材质名称、metallic、roughness 提供可见诊断。
2. 统一颜色空间：CAD `Quantity_Color`、UI swatch、HDRI 和 shader uniform 明确 linear/sRGB；
   只在最终输出做 transfer function。
3. 增加 `Exposure`, `Environment Rotation`, `Environment Intensity`, `Ground Shadow` 四个
   scene/render settings；先用一张离线 2K 中性 studio HDRI。
4. 主场景改为 HDR `RGBA16F`（可保留 MSAA color/depth），新增全屏 tone-map pass；默认用 ACES
   fitted 或 AgX-like 曲线，提供 exposure reset。
5. 关闭默认 screen-space dark edge；BRep edge 使用 0.35-0.55 的中性深灰，选择高亮才使用
   accent。

### Phase B：检查质感

1. 一张聚焦 key shadow map + ground plane/contact shadow；大装配不必立刻上完整 CSM。
2. 小半径 GTAO/SSAO，分辨率 half-res + bilateral blur，强度 0.15-0.35。
3. TAA 或 SMAA；线条/选择 pass 使用独立 depth-aware resolve。
4. 把 IBL 烘焙改为构建时脚本或第一次运行缓存，目标至少 256 env / 64 irradiance / 完整 mip
   prefilter；低端 GPU 使用 128/32 tier。

### Phase C：材质和高级模式

1. `Material` 增加 `alpha_mode`, `alpha_cutoff`, `normal_texture`, `roughness_texture`,
   `metallic_texture`, `occlusion_texture`, `clearcoat`, `anisotropy`, `tangent`；没有数据时
   保持固定默认值，不伪造贴图。
2. XCAF `XCAFDoc_VisMaterial`/PBR 映射到 scene，保留 source material name 和转换诊断。
3. 增加“Technical”与“Studio”两个渲染模式：Technical 以隐藏线、平面色、测量可读性优先；
   Studio 以 HDRI、阴影、AO、材质高光优先。不要让一个模式同时满足两套视觉目标。
4. 若客户需要 KeyShot 级静态图，提供独立 offline render/export（OSPRay/Embree+OIDN 等
   经过验证的引擎）而不是把实时 viewport 变成不可预测的路径追踪。

## KeyShot 风格如何“参考”而不是照搬

| KeyShot 体验 | 实时等价物 | Cadly 的边界 |
| --- | --- | --- |
| HDRI studio 和 softbox 反射 | 预滤 HDRI + area-light/shadow map | 不做每像素路径追踪，保证 CAD 交互 |
| 物理材质库 | 可解释的 scene material presets + XCAF 映射 | 没有真实材料参数时显示“估计” |
| GI/接触阴影 | GTAO + ground shadow + 低强度 fill | 不把 AO 当作几何真值 |
| 高光滚降 | HDR + ACES/AgX + exposure | 颜色输出由统一 postprocess 管理 |
| 细腻边缘 | 倒角几何、法线质量、TAA、适度 silhouette | 不用全屏黑描边替代几何 |
| 画面构图 | 相机 framing、标准 studio 背景、可保存相机 | 不遮挡选择/测量 HUD |

质量验收应使用固定的“材质球 + 倒角盒 + 阶梯孔 + 装配间隙 + 透明片”场景，比较：

- 高光宽度与方向是否随 roughness 单调变化；
- 0°/45°/90° 视角下倒角、圆柱和凹槽是否仍可读；
- 平面接触处是否有柔和阴影而不是黑缝；
- 颜色在低曝光/高曝光下是否保持色相；
- 旋转时细线、IBL 和 AO 是否闪烁；
- Technical 与 Studio 模式是否各自完成目标。

