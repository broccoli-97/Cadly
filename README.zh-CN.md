# Cadly

> 一个简单的 CAD 模型导入查看器 —— 打开 STEP/IGES 文件并查看模型。

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

[English](README.md)

Cadly 是一个用 C++17 编写的原生桌面 CAD **查看器**（不是编辑器）。它通过 Open
CASCADE（OCCT）导入 STEP/IGES，对 B-Rep 做三角化，再交给手写的实时渲染器绘制
（当前为 OpenGL 4.1 core，接口预留了后续接入 Vulkan 后端的空间）。界面外壳基于
Qt 6 Widgets。材质采用 PBR 金属度-粗糙度模型配合基于图像的光照（IBL），针对工业
检视场景做了调校。

![Cadly 打开一套 RC 越野车前悬挂装配](docs/images/shell-overview.png)

<table>
<tr>
<td width="50%"><img src="docs/images/shaded-zero-chrome.png" alt="隐藏全部面板的着色视图"></td>
<td width="50%"><img src="docs/images/wireframe.png" alt="带 GPU 轮廓线的线框视图"></td>
</tr>
<tr>
<td align="center"><sub>无边框专注模式（<code>⌃.</code>）</sub></td>
<td align="center"><sub>线框模式，轮廓线由 GPU 逐帧生成</sub></td>
</tr>
</table>


## 功能

- **导入** STEP 与 IGES：使用 OCCT CAF 读取器保留颜色、名称与装配层级，并提供
  仅几何的回退读取器。
- **非模态导入**：导入在工作线程中进行，进度与取消位于文档标签页内，导入期间
  原有场景仍可交互。
- **显示模式**：着色、着色+边线、消隐线（可选暗显被遮挡线）、带解析式 LOD 边线
  层级的线框，以及三角网格调试叠加。线框/消隐模式的视点相关轮廓线由 GPU 逐帧
  生成。
- **CAD 式导航**：右键拖拽旋转、中键拖拽平移、滚轮以光标为锚点缩放，默认正交
  投影，`1`–`7` 切换标准视图。
- **装配树**：单击高亮部件，双击隔离（其余部件半透明虚化或直接隐藏）。
- **多标签文档**、带持久化导入选项（网格偏差、修复、焊接）的属性检查器、
  深色/浅色主题以及无边框专注模式。
- **多语言界面**：英文与简体中文。
- **命令行工具** `cad_import_cli`：无需显示设备即可验证导入结果。

## 构建与运行

使用 CMake preset（Ninja）。Qt 6 与 OCCT 的预编译依赖分别通过 Linux 的 apt、
Windows 的 MSYS2 CLANG64 和 macOS 的 Homebrew 安装。

```bash
cmake --preset linux-release          # 配置（RelWithDebInfo）
cmake --build --preset linux-release  # 构建 -> build/linux-release/bin/
ctest   --preset linux-release        # 冒烟测试

build/linux-release/bin/cadly [file.step]        # 图形界面，可选在启动时打开文件
build/linux-release/bin/cad_import_cli file.step # 无界面导入，打印几何统计
```

Windows 下先安装 [MSYS2](https://www.msys2.org/)，打开 **CLANG64** 终端。
先运行 `pacman -Syu` 更新；若提示关闭终端，重新打开后再次运行更新。
在仓库根目录执行：

```bash
bash scripts/setup-windows.sh        # 安装预编译依赖及 Clang
cmake --preset windows-msys2-release
cmake --build --preset windows-msys2-release
ctest --preset windows-msys2-release
build/windows-msys2-release/bin/cadly.exe [file.step]
```

Windows CI 使用相同的依赖和命令，无需从源码构建 Qt/OCCT，也无需维护依赖二进制
缓存。依赖随 MSYS2 仓库更新，Clang 和所有库统一使用 CLANG64 版本。
从 UCRT64 构建迁移时，运行 `cmake --fresh --preset windows-msys2-release`
清除旧 CMake 缓存。发布包自带运行所需 DLL，用户无需安装 MSYS2。
选择此工具链的 OCCT 导入回归说明见[平台差异记录](docs/platform-divergence.md)。

Linux 搭配共享版 OCCT 7.6.3 时，构建会下载固定版本的 OCCT 源码，在构建目录内重编译
两个 toolkit，优化 STEP 扫描和多边形求交。这需要 `patch`；可用
`-DCADLY_OCCT_PERFORMANCE_PATCHES=OFF` 使用系统库。其他 OCCT 版本使用原有库。
测试条件和实际收益见[导入深度性能报告](docs/research/05-step-import-deep-profile.md)。

STEP 默认并行转换独立零件，并保留完整几何修复。
`cad_import_cli file.step --profile --fingerprint` 输出阶段耗时与场景指纹；
`--step-threads 1` 使用普通串行转换。可选的 `--step-healing fast`（导入选项中也可选择）
跳过相邻边交叉修复，损坏的几何可能需要完整修复。
`cadly file.step --profile-import` 测量从打开文件到首帧提交显示的时间。

其他 preset：`linux-debug`、`linux-qt68-{debug,release}`（Qt 6.8，启用 qlementine
样式）、`windows-msys2-debug`、`macos-{debug,release}`（先运行
`scripts/setup-macos.sh`）。仍可选用 vcpkg preset：`linux-vcpkg-debug`、
`windows-msvc-{debug,release}`（VS 解决方案）、`windows-ninja-{debug,release}`
（使用环境中的 MSVC）。CI 在三个平台上完成构建、测试与打包；推送 `v*` 标签会把
这些包发布到 [Releases](../../releases) 页面。

### 依赖

- 支持 C++17 的编译器、CMake ≥ 3.24、Ninja
- Qt 6（Widgets、OpenGL、Concurrent、Svg；LinguistTools 可选）
- Open CASCADE 7.6 及以上
- glm、spdlog、fmt
- 支持 OpenGL 4.1 core 的显卡与驱动

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

`docs/cad-viewer-plan.md` 是最初的设计方案与里程碑规划，`docs/ui-redesign/`
记录界面外壳的布局设计，`CLAUDE.md` 说明修改代码时需要保持的架构约束。

## 许可证

本项目基于 Apache License 2.0 发布，详见 [LICENSE](LICENSE)。

Cadly 链接了 Open CASCADE（LGPL-2.1 及其例外条款）与 Qt 6（LGPL-3.0），这些
组件仍遵循各自的许可证。
