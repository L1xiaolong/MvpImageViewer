# UI 图标重绘提案（已确认并实施）

本目录保留已确认的设计稿，不参与 CMake / QRC。对应的 96 份应用资源已替换至 assets/icons/ui 与 assets/icons/ui-dark。

- `index.html`：亮暗主题、16/20/32 px 样本、16 px 工具栏、12 px 窗口控件及交互状态。
- `overview.png` / `overview.svg`：完整图标样式总览。
- `manifest.json`：48 个文件的功能、分组、设计说明。
- `svg/light/` / `svg/dark/`：96 份自行绘制的候选 SVG。

审查范围：assets/icons/ui、assets/icons/ui-dark 共 96 个 SVG，及 assets/brand/logo.svg。
依据：Theme.qml、AppIconButton.qml、TopToolbar.ui.qml、ComparePage.qml、WindowCaptionButtons.qml、FolderNavigator.qml 与资源清单。仓库截图用于理解旧图标语境，尺寸和主题以当前 QML 为准。

现状：项目 THIRD_PARTY_NOTICES.md 记录 27 个 Lucide/Feather 文件名，thumbnail-preview.svg 另含 lucide 标识。除第三方来源外，还存在实际像素/EXIF/图像信息/截图/缩略导航等语义不贴合、20/24 网格与平台填色风格混杂的问题。

方案：从空白 20×20 网格绘制几何路径，无图标库导入、无复制旧 SVG 路径。主线宽 1.5（16 px 时为 1.2）；微圆角；窗口图形线宽 1.4。通用箭头和窗口控制保持通用操作习惯，独立绘制不意味着通用符号具有排他性。
亮色 #303036；暗色 #DDDDE2；目录蓝/赭黄色提供主题变体，白色关闭图形保留在红色悬停背景。

品牌 Logo：assets/brand/README.md 明确记录为用户已确认稿，文件未见图标库标记，建议保留。没有对其来源作外部鉴定。

实施记录：已按用户“实施替换”的确认替换 96 份资源。实际 SVG 将 currentColor 按继承关系解析为显式颜色，保持确认稿路径、线宽和半透明填色不变。暗色生成脚本与打包声明同步更新。旧图标上游许可证留作历史参考，不随当前应用打包。详情见 IMPLEMENTATION.md。

后续修订：平滑显示改为图像框内的连续曲线；新增 fullscreen.svg，图标总数为 49 对（98 份运行时 SVG）。画廊底部三个按钮已使用 actual-size / fit / fullscreen，见当前 index.html 与应用。overview.png / overview.svg 保留为首次确认的 48 个图标历史快照。
