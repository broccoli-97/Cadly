# Cadly

打开一个 STEP 或 IGES 文件，把它看清楚。就这么件事。

[![CI](https://github.com/broccoli-97/Cadly/actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[English](README.md)

Cadly 是一个用 C++17 写的原生桌面 CAD 查看器，只看不改。它通过 Open CASCADE
读入 STEP/IGES，对 B-Rep 做三角化，再交给手写的实时渲染器绘制（当前是 OpenGL
4.1 core，接口留好了接入 Vulkan 的位置）。界面外壳基于 Qt 6 Widgets。着色采用
PBR 金属度-粗糙度模型配合 IBL，调校目标是把加工面看清楚，而不是拍得好看。

![Cadly 打开一套 RC 越野车前悬挂装配](docs/images/shell-overview.png)

<table>
<tr>
<td width="50%"><img src="docs/images/shaded-zero-chrome.png" alt="隐藏全部面板的着色视图"></td>
<td width="50%"><img src="docs/images/wireframe.png" alt="带 GPU 轮廓线的线框视图"></td>
</tr>
<tr>
<td align="center"><sub>专注模式 —— 面板全部让位（<code>⌃.</code>）</sub></td>
<td align="center"><sub>线框模式，轮廓线由 GPU 逐帧生成</sub></td>
</tr>
</table>

## 它能做什么

导入走 OCCT 的 CAF 读取器，颜色、零件名和装配层级都会保留下来；遇到没有产品结构
的文件，则回退到仅几何的读取器。导入在后台线程进行，全程非模态 —— 进度和取消都在
文档标签页里，原来在看的模型仍然可以随意操作。

看模型有五种方式：着色、着色+边线、消隐线（可选把被遮挡的线暗显出来）、带解析式
LOD 边线层级的线框（放大时自动细分），以及用于检查三角化结果的网格叠加。两种线模式
都会由几何着色器逐帧生成视点相关的轮廓线，所以旋转视角时圆柱看起来仍然是圆柱。

导航遵循 CAD 习惯：右键拖拽旋转，中键拖拽平移，滚轮以光标为锚点缩放，默认正交
投影，`1`–`7` 切换标准视图。在装配树里单击零件会在视口中高亮，双击则隔离它，其余
部分虚化或直接隐藏。

围绕这些还有：多标签文档、带持久化导入选项（网格偏差、修复、焊接）的属性检查器、
深色/浅色主题、中英文界面，以及无需显示设备就能验证导入结果的 `cad_import_cli`。

## 构建与运行

CMake preset + Ninja。Linux 下用系统的 Qt 6 和 OCCT；Windows 与便携构建走 vcpkg
清单（`vcpkg.json`）。

```bash
cmake --preset linux-release          # 配置（RelWithDebInfo）
cmake --build --preset linux-release  # 构建 -> build/linux-release/bin/
ctest   --preset linux-release        # 冒烟测试

build/linux-release/bin/cadly [file.step]        # 图形界面，可在启动时打开文件
build/linux-release/bin/cad_import_cli file.step # 无界面导入，打印几何统计
```

其他 preset：`linux-debug`、`linux-qt68-{debug,release}`（Qt 6.8，启用 qlementine
样式）、`linux-vcpkg-debug`、`windows-msvc-{debug,release}`（VS 解决方案）、
`windows-ninja-{debug,release}`（单配置 Ninja，需要环境中已有 MSVC）。

依赖：支持 C++17 的编译器、CMake ≥ 3.24、Ninja、Qt 6（Widgets、OpenGL、
Concurrent、Svg，LinguistTools 可选）、Open CASCADE 7.6 及以上、glm、spdlog、
fmt，以及支持 OpenGL 4.1 core 的显卡驱动。

## 预编译包

每次推送到 `main` 都会在 Linux 和 Windows 上构建、测试，并把可直接运行的包上传为
workflow artifact。打了标签的版本会发布到 [Releases](../../releases) 页面：一个
便携的 Linux x64 tarball 和一个自包含的 Windows x64 zip，都不需要另外安装 Qt 或
OCCT。

发版就是推一个标签：

```bash
git tag v0.1.0 && git push origin v0.1.0
```

`Release` 工作流（`.github/workflows/release.yml`）会用与 CI 相同的流水线从该标签
构建两个包，创建一个带自动生成说明的草稿 release 并挂上产物 —— 确认无误后编辑说明
再发布。需要重新生成某个已有标签的产物时，也可以在 Actions 页面手动触发。

## 架构

`src/` 下每个模块对应一个 CMake target，导出为 `Cadly::<Name>`：

| 模块 | Target | 职责 |
|---|---|---|
| `platform` | `Cadly::Platform` | 日志、资源与路径查找 |
| `scene` | `Cadly::Scene` | 规范场景模型 —— 仅依赖 glm，不含 Qt/OCCT/GL |
| `renderer` | `Cadly::Renderer` | `IRenderer` / `RenderTypes` 接口 |
| `renderer_gl` | `Cadly::RendererGL` | OpenGL 4.1 后端，不依赖 Qt |
| `cad` | `Cadly::Cad` | 导入器 —— 唯一链接 OCCT 的 target |
| `ui` | `Cadly::Ui` | 控件与视口宿主 |
| `app` | `cadly` | 程序入口、设置、最近文件 |

数据流：文件 → `ImporterRegistry::select()` → OCCT STEP/IGES 导入器 →
`BRepMesh_IncrementalMesh` → `scene::Mesh` → `scene::Scene` →
`IRenderer::attach_scene()` → `render()`。

模块边界本身就是设计的一部分：`scene` 不碰 Qt、OCCT 和任何图形 API，因而能夹在
任意导入器与任意后端之间；`renderer_gl` 接收一个 GL 函数加载器，而不是链接 Qt。

`docs/cad-viewer-plan.md` 是最初的设计方案与里程碑规划，`docs/ui-redesign/`
记录界面外壳的布局设计，`CLAUDE.md` 列出改代码时需要保持的架构约束。

## 许可证

Apache License 2.0，详见 [LICENSE](LICENSE)。Cadly 链接了 Open CASCADE
（LGPL-2.1 及其例外条款）与 Qt 6（LGPL-3.0），这些组件仍遵循各自的许可证。
