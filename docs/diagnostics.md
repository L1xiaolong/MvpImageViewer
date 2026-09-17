# 日志与崩溃诊断

## 使用

打开“设置 → 日志与诊断”。运行日志和崩溃报告默认分别开启；最低日志等级为 Info。运行日志开关与等级立即生效，崩溃开关在下一次启动生效，界面同时显示本次状态和待生效提示。关闭运行日志不影响已开启的崩溃采集及关键操作记录。

“导出 ZIP”支持最近 24 小时、7 天、30 天或全部保留资料。默认不包含 `.dmp`；只有主动勾选“包含崩溃转储”才加入。资料不会自动上传。导出包括当前会话和符合时间范围的已结束会话，跳过其他正在运行的实例。清理历史资料需确认，运行中的会话受保护；恢复默认设置不删除历史资料。

普通日志保留 7 天、最多 100 MiB，单文件最多 10 MiB；崩溃会话连同其关联资料保留 30 天、最多 10 份、200 MiB。容量限制是自动清理目标：新转储正在生成、多个实例正在运行时可能暂时超限。每分钟及启动时清理最旧的已结束会话，当前会话的普通日志也会轮转并限制容量。

默认目录为 `QStandardPaths::AppLocalDataLocation/diagnostics`。Windows 通常位于 `%LOCALAPPDATA%/ISPView/MVP Image Viewer/diagnostics`，macOS 位于用户应用支持目录；以设置页显示的实际目录为准。

## 文件与接口

每次启动创建独立的 UTC 时间戳 + UUID 会话目录：

- `session.json`：版本、构建标识、系统、架构和 Qt 版本。
- `log-0000.jsonl` 等：UTF-8 JSONL，含时间、等级、类别、会话、进程、线程、事件及上下文。
- `breadcrumbs.bin`：128 个固定 2 KiB 槽的内存映射关键操作记录；导出转换成按序排列的 `breadcrumbs.jsonl`。崩溃时不依赖日志线程收尾。
- `crash.dmp`（Windows）或 `crashpad/` 内的转储（macOS）。仅校验通过的异常与模块流才被识别为崩溃报告。
- `closed.json`：业务对象与 QGuiApplication 完成销毁后写入的正常退出标记。
- 导出包另有 `manifest.json` 和每个会话的 `summary.json`，包含崩溃时间、异常代码、线程及是否附带转储。

新埋点使用 `diagnostics::event(level, category, event, context, breadcrumb)`，文件使用 `diagnostics::fileId(path)`，跨线程操作使用 `operationId()` 关联。不要传入原始路径、图像像素、EXIF/GPS、令牌、完整 URL 或命令行。第三方 Qt 消息会进行路径、URL 与常见敏感字段过滤，这不等于任意文本的完整脱敏；转储本身也不保证脱敏。

`AppSettings` 管理 `loggingEnabled`、`logLevel`、`crashReportingEnabled`，键名为 `diagnostics/<property>`。`DiagnosticsController` 独立负责目录、空间、采集状态、最近报告、清理和异步 ZIP 导出。QML 不直接操作日志文件。

队列最多 4096 条，每条序列化记录最多 16 KiB；后台每 250 ms 刷新，Error/Fatal 请求立即刷新。过载优先丢弃低等级记录，并写出丢弃计数。重复警告与错误按事件及错误内容限频；Fatal 不限频。关闭日志后，已经接收的记录仍会完成写入。

## 构建与分析

### Windows x64

正常构建会同时生成 `ispview_crash_handler.exe`，必须与主程序一起部署。它只继承所需 IPC 句柄，使用独立进程调用 `MiniDumpWriteDump`。主进程的异常回调仅保存预分配上下文并有限等待，处理未捕获异常、终止、abort 和 Qt fatal 路径。

打包脚本自动在 `dist/symbols/` 保存未剥离二进制、DWARF 或 PDB 及 `symbols.json`，随后剥离暂存安装文件的 DWARF。清单保留原文件哈希、剥离后 PE 标识、架构、版本和提交信息。符号目录不加入用户诊断 ZIP。

```powershell
python scripts/archive_diagnostic_symbols.py --binary build/windows-msys2-release/MVPImageViewer.exe --output build/symbols
python scripts/analyze_minidump.py crash.dmp --binary build/symbols/<matching-build>/MVPImageViewer.exe
```

`analyze_minidump.py` 校验故障模块标识及归档哈希，使用 MinGW/LLVM `addr2line` 解析故障地址；它不是完整堆栈展开工具。MSVC 构建使用 WinDbg 打开转储，载入匹配 PDB 后执行 `!analyze -v`、`~* kb`。第三方模块的源码定位还需该模块匹配的符号。

### macOS arm64

首次配置时调用 `scripts/build_crashpad_macos.sh`，需要网络、Xcode、Python、Git；脚本准备 depot_tools，并固定 Crashpad 与其 DEPS。客户端和 handler 使用分开的 GN 输出目录，避免把 handler 入口点混入客户端静态库。可通过 `ISPVIEW_CRASHPAD_ROOT` 指定已经准备好的、目录名为 `crashpad` 的源码目录。配置 `-DISPVIEW_ENABLE_CRASHPAD=OFF` 可跳过 Crashpad 构建：应用仍可正常运行，崩溃采集显示为不可用，日志、清理与导出功能不受影响。

应用包包含 `Contents/Helpers/crashpad_handler`。数据库明确关闭上传；打包时部署许可证，先签名辅助程序再签名外层应用。发布符号归档包含二进制 UUID 和 dSYM。使用配套 Crashpad/Breakpad 工具将匹配 dSYM 转换为符号文件，再用 `minidump_stackwalk` 展开堆栈；不要直接把 minidump 当作 macOS `.ips` 文件交给 `atos`。

CI 把 `dist/symbols/` 作为独立 artifact 保留 90 天。发布维护者应为需要长期支持的版本另外保存该目录，避免 CI artifact 过期后丢失旧版本符号。程序不具备远程符号存储或上传功能。

## 验证

```powershell
cmake --build build/windows-msys2-debug --parallel 6
ctest --test-dir build/windows-msys2-debug --output-on-failure
# Release 构建开启 ISPVIEW_BUILD_BENCHMARKS 后：
build/windows-msys2-release/ispview_diagnostics_benchmark.exe build/diagnostics-benchmark.json
```

诊断测试使用独立临时目录和子进程，验证等级、中文/路径过滤、并发过载、轮转、故障写入、独立开关、清理、多实例快照、ZIP 完整性与取消，以及主线程/工作线程的访问异常、未处理 C++ 异常、abort、qFatal。测试驱动逐项检查 QtTest 完整结束，不能只凭子进程退出码判断成功。QML 测试检查两个开关独立，以及默认不导出转储。

性能工具交错运行关闭诊断和默认 Info 模式，以中位数比较 2000 文件扫描、连续 PNG 解码和 NV12 原始图像解码；输出是否在 5% 以内。它不代替实际 GPU 切图、全屏、相机 RAW 和 macOS 签名发布包测试。

断电、强制杀进程以及某些 fail-fast/驱动/内核故障不保证生成转储。只有未关闭会话时，下次启动报告“异常退出”，不会宣称已捕获崩溃。日志或辅助进程初始化失败不阻止使用软件，设置页显示降级状态。
