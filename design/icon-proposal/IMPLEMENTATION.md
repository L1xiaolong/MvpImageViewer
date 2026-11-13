# 图标替换实施记录

用户已确认图标方案并明确要求实施替换。

## 已实施

- `assets/icons/ui/` 与 `assets/icons/ui-dark/` 的全部 48 个文件名、合计 96 个 SVG 已替换。
- 路径、20×20 网格、线宽、透明度与确认稿一致；将继承的 currentColor 正确展开为显式颜色，适配 Qt SVG 渲染器。
- 现有 QML 尺寸、主题切换、交互状态、命中区域与资源文件名保持不变。品牌 Logo 保留。
- `tools/generate_dark_icons.mjs` 使用确认稿主题色生成暗色版本，保留线宽与纯白关闭按钮。
- 中英文 README、第三方声明与打包脚本同步更新。旧图标许可证保留为仓库历史参考，当前打包不再分发；更新已有打包目录时清理该旧声明。
- 主题规则与维护方式记录在 `assets/icons/README.md`。原始确认总览保留为设计记录。

## 验证结果

- 应用 `MVPImageViewerQml`、`settings_ui_regression`、`folder_navigation_ui_regression` 构建成功；最终资源更新后再次构建应用和设置测试成功。
- 96 个 SVG XML 合法；CMake 资源清单精确覆盖 48 对文件；应用资源不含图标库 class 标识或依赖外部 currentColor。
- 使用应用当前 Qt 6.9 的 QSvgRenderer：576 次渲染，覆盖亮暗主题、12/16/20 px、1×/2×，全部非空、前景颜色正确；40 px 主题透明度逐像素相等。
- 暗色生成器重复运行产物哈希不变。
- `settings_ui_regression` 完整通过；最终资源更新后的浏览页亮暗 980/1440 宽度截图及窗口 12 px 图标对齐、加载检查共 10 项全部通过。
- 打包许可证脚本成功执行；`git diff --check` 通过。

## 原有测试失败

`folder_navigation_ui_regression` 的 `test_switch_between_loaded_panes` 失败：非活动分支预期折叠，实际展开。将 HEAD 的原始图标与未改动 QML 复制到 `/tmp/mvp-icons-baseline` 并对同一测试程序使用 `-input` 执行，得到完全相同的失败（actual 11 / expected -1）。该导航逻辑未在本次修改，未因图标任务扩展修复。

## 验证产物

- `build/macos-preset-release/icon-review/qt-icons-1x.png`
- `build/macos-preset-release/icon-review/qt-icons-2x.png`
- `build/macos-preset-release/settings-ui/browse-{light,dark}-{small,wide}.png`
- `build/macos-preset-release/settings-ui/caption-{light,dark}.png`
- 新构建应用：`build/macos-preset-release/src/qml/MVPImageViewer.app`

本次更新源码和构建产物，没有覆盖 `/Applications` 内已安装的应用。

## 独立复核

结论：ship，无本次实现引入的实质问题。独立逐项解析确认稿与运行时 SVG 的几何、继承配色、线宽、透明度及边界，96/96 一致；暗色文件全部匹配生成器映射。已查看亮暗浏览页、窗口按钮和 1×/2× Qt 图标样张，未发现裁切或缺失。

## 后续修订：平滑显示与画廊按钮

- 按用户反馈，将平滑显示改为图像框内的一条连续曲线，新增双向对角展开箭头 `fullscreen.svg`。当前为 49 对、98 份 UI SVG。
- 画廊底部实际像素、适应窗口、全屏打开改为共用的 AppIconButton：28 px 操作区域、16 px 图标、主题配色、悬停提示、无障碍名称与键盘焦点。缩放动作维持选中状态，未选图时三个按钮禁用。
- 两种缩放模式设置为互斥，重复点击当前模式不会取消选中。全屏按钮继续激活当前预览文件。
- 运行时、测试和 Design Studio 的资源清单，以及中文提示文字已同步。
- 新增行为回归，覆盖亮暗主题的 980/1440 宽度、图标加载、鼠标和键盘操作、重复点击、全屏分发及无图片状态。
- 当前 `index.html` 与 `manifest.json` 已同步新样式，最初的 overview 静态图保留为历史确认快照。

修订验证：完整 settings_ui_regression 通过；新增画廊场景在1×、2×与亮暗980/1440宽度下全部通过。独立复核结论 ship，无实质问题。实际按钮效果摘录保存为 gallery-controls-update.png；高DPI原始截图保存在 build/macos-preset-release/gallery-icon-review/。
