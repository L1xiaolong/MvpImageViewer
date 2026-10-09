# 启动、预览和大目录性能

## 实现

- Grid/List/Gallery 使用单元格与视口的矩形相交来建立需求；每个视图有独立 owner，图像 URL 不包含视口状态。拖拽图像在实际拖拽时请求，避免 Drag.imageSource 绕过视口产生额外加载；更大的已缓存缩略图可直接复用。每轮可见项先于预取，按中心距离排序，同类请求跨面板轮转。合并请求可以提升队列优先级。
- 正常前向一屏、后向半屏；超过两屏/秒时前向半屏、不预取后方。快速滚动不暂停可见缩略图，仅暂停新的低优先级分析、元数据和全图预取。每视图最多 128 个候选（可见项超过时保留全部可见项），至多四面板合计 512 个普通候选，且不超过可见数量的三倍。
- 解码任务保留在可重排的应用队列，线程池内不积压任务。普通并发保持原有 2～6 上限，后台任务保留一个交互名额，LibRaw 串行。任务入队、优先级改变和工作线程结束后合并唤醒调度器，不等待16ms定时轮询；GUI结果提交仍限制为每16ms最多4ms，释放解码通道与发布结果分开。后台队列上限 512，磁盘写入上限 64；运行任务与待提交结果共用64项预留额度，GUI提交放缓时不再启动新的解码，结果提交每轮 4ms。
- 共享消费者计数提供取消上下文；最后一个消费者离开后任务取消。RAW 分块读取、YUV/Bayer 转换、LibRaw 进度回调检查取消，Qt 图片插件调用前后检查。不可中断的第三方调用及操作系统 I/O 仍可能延迟退出。
- 缓存读取不持有压缩/写入的大锁。写入独立单线程、同键去重、QSaveFile 原子提交，清理独立执行；磁盘目录为 thumbnails-v2，清理阈值 512MiB（维护前可能短暂超过阈值）。
- 目录首批 32 条或 50ms、后续 192 条，GUI 以 32 条子批处理、每轮 4ms。生产者最多有 8 个未消费批次，已消费缓冲及时丢弃，旧扫描取消时清除排队扫描。模型按倍数扩容，避免精确 reserve 导致的二次复杂度。自然排序键在线程中准备，按路径恢复滚动锚点；搜索防抖 100ms。递归图片文件夹查询也使用迭代器，避免先收集完整根目录文件列表，并在遍历、祖先路径展开和排序键准备期间检查取消。
- 帧同步只复制共享引用，不深拷贝像素。RGBA64 半浮点准备及大端 P010 归一化在线程中执行；纹理按槽更新，兼容纹理复用，平移/缩放不重新上传图像。原始数据仍保留用于精确探针。
- 对比、全屏和设置页面通过异步 Loader 创建，准备完成后再执行打开动作；首帧交换后开始目录恢复。诊断维护移至后台，早期崩溃捕获保留；剪贴板格式只探测一次并按变化失效。
- 完整 EXIF/IPTC/XMP 按需异步读取，显示前仍处理色彩和方向。预览使用 512/1024/1536/2048/2560 分桶，可复用原图和更大预览。停留 250ms 后在无交互积压及预算允许时升级；精确操作立即请求原图，临时预览不用于精确像素值。
- 相机 RAW 马赛克和 RAW/YUV 文件字节复用源缓存，键包含文件版本及帧字节范围，显示参数变化复用源数据。源缓存上限 96MiB，纳入解码缓存 384MiB 总账；自动原图估算预算 256MiB。会话退出释放引用，由共享 LRU 淘汰。原始源数据、上传准备和 GPU 分别记录资源估算。

这里的 384MiB 是解码缓存预算，不是进程 RSS 硬上限：活动帧、原生解码器临时分配、Qt 图片缓存、GPU/驱动仍需要额外内存。GPU 指标是按纹理尺寸/格式估算，不能代替驱动测量。

## 验证

```powershell
cmake --build build/windows-msys2-release -j 4
ctest --test-dir build/windows-msys2-release --output-on-failure
python tools/performance_benchmark.py --exe build/windows-msys2-release/MVPImageViewer.exe --images E:/images --output build/perf-results/local --runs 20 --scenario scroll --mode grid --panes 4
```

`--scenario` 支持 startup、scroll、fullscreen、compare、refresh、slot-update；`--mode` 支持 grid、list、gallery。`--cache-state cold` 为每次运行创建新的独立缩略图缓存目录；`disk` 使用输出目录下同一个缓存目录（第一轮预热、后续测磁盘命中）；默认 `existing` 沿用用户缓存。内存命中通过同进程反向滚动和 loader.memory_hit 验证，重新启动进程不能保留解码内存缓存。可用 MVPVIEW_THUMBNAIL_CACHE_DIR 指定实验磁盘目录。滚动场景会填充指定数量的面板并执行下滑、停止、跨距跳转和反向滚动。对比场景取目录前四张图。截图、完整日志、JSON 事件和 summary.json 留在输出目录。性能专用 CLI 动作仅在 MVPVIEW_PERF=1 时执行，不影响正常启动。

性能事件包括 startup.first_frame、viewport.updated、directory.batch/directory.finished、loader.dispatched、loader.completed、loader.memory_hit、render.upload、render.resources、image.submitted/image.presented、viewport.presented、directory.model_chunk、ui.frame/ui.stall 和 scenario.motion。image.presented 在上传后下一次 frameSwapped 记录；viewport.presented 只有在当前几何可见项的Image均为Ready且路径仍匹配时，在frameSwapped记录。firstElapsedMs从首次非空可见需求计时，elapsedMs从本次可见路径集合变化计时；运动期间过期而未补齐的视口不产生完成事件，因此完成事件P95不能代替全部视口或滚动停止后的P95。目录项和文件名不代表图像已呈现。ui.stall 是 16ms 心跳间隔超过 50ms，包含截图抓取、平台调用和测试进程压力，不能仅凭心跳把原因归于某一函数。帧间隔统计只取滚动运动区间，排除开始后的 100ms，静止时期没有持续绘制，不能纳入 FPS。

回归测试覆盖：请求合并/提升/取消及重新请求、后台分析在快速滚动期间暂停、磁盘透明度与尺寸以及并发读写、源帧复用、NV12/NV21/I420/P010/MIPI RAW10/12/RAW16 大小端、方向与精确样本访问、PNG/JPEG/BMP、16-bit Display-P3 和上传准备、1万条目渐进发布/自然排序、扫描背压与等待期间取消。新增并行压力测试验证后台占用时交互请求可使用保留通道、并发不超过6、取消后不提交旧结果。Release 测试通过，最新一轮10.54秒（相机缓存审计修复后）；一万条目刷新场景重新扫描完成，未因批次背压挂起。

## 当前本机数据（2026-10-09）

Windows Release，默认 OpenGL，2160×1350 截图。目录为人工生成的有效 PNG：1万条目使用 1800×1200 图片，10万条目使用 320×240 图片，图像内容简单且文件较小。它们验证调度与目录行为，不能代表相机 RAW、超大 PNG 或慢盘的解码性能。

| 场景 | 首帧 | 滚动帧间隔 P50/P95 | 备注 |
| --- | --- | --- | --- |
| 原始3a6d623小目录启动，5次 | P50 4408ms / P95 4591ms | 不适用 | 原始源码独立构建，仅插入首帧记录 |
| 小目录启动，20次 | P50 1123ms / P95 1150ms | 不适用 | 平台剪贴板失败重试仍影响此环境 |
| 十万条目，单面板滚动，最终版本 | 1235ms | 16ms / 18ms | 拖拽请求按需后，仅547个解码完成，约10秒观测 |
| 一万条目，四面板同时滚动，重复两次 | 1243ms / 1135ms | P95 19ms / 32ms | 四面板可见图补齐，共享任务合并；尚不稳定 |
| 一万条目，Gallery滚动，最终版本 | 1125ms | 16ms / 19ms | 单次 |
| 四图对比 | 1311ms | 不适用 | 初始空槽为4字节占位；每槽预览与原图各一次上传（旧记录bytes字段为帧内存，已改正为提交数据量） |

原始数据位于本工作区 build/perf-results；不提交生成的十万文件或日志。启动的早期20次记录 P95 2151ms，剪贴板探测和隐藏 Gallery 请求修复后为1150ms；这是本轮实现内部前后对照。另从原始3a6d623独立构建Release，首次仅增加process/first_frame记录，5次首帧P50为4408ms；原始首轮与一轮冷缓存测试有短暂进程重叠，后四轮单独运行，首帧为4369～4417ms。不能据此推断所有格式首屏性能。

**尚未满足或尚未证明的验收项：**单面板最终单次滚动达到 P95≤20ms，但尚未重复或覆盖全部场景，四面板两次P95为19ms和32ms，未稳定达标；Gallery单次P95为19ms；观察仍有超过50ms心跳间隔；原始版本已完成启动和100张PNG冷缓存首屏对照，尚无真实全格式和严格冷/磁盘/内存缓存矩阵，不能宣称首屏改善30%、磁盘一屏200ms、内存命中两帧或其他指标回退≤5%。没有真实相机 RAW/HEIF fixture、慢盘和其他平台的性能验收；这些需使用真实数据继续测量。截图抓取导致的停顿也应在正式计时窗口之外。当前结果可复现，不能把实现完成等同于所有性能目标已达标。

## 补充验收（本轮）

- `slot-update` 场景在稳定的四图对比中只替换槽位0的不可变帧引用，然后执行100%缩放和适应窗口；两次自动断言均得到 slotUploads=[0]、viewUploads=0。它验证槽位更新和视图参数隔离，不替代各格式GPU像素正确性测试。
- 原始版本额外加入只读的Image-ready/帧交换观测和独立缓存目录入口，未改变图片URL、解码调度或模型行为。100张1800×1200 PNG，3次独立冷缓存首屏补齐168/170/173ms；早期优化版本152/152/152ms；合并唤醒调度器后130/131/131ms；进一步按提交期限唤醒后125/128/133ms（首屏P50约25%改善，P95约23%），仍未达到30%目标。
- 同一100张目录的磁盘实验首轮预热156ms，后两轮119/90ms。只有这组简单PNG、当前SSD与窗口大小的证据，不能宣称全格式磁盘一屏P95≤200ms。
- 一万条目、四个可见面板同时滚动，最新3轮运动帧间隔P95为17/18/17ms。测试脚本已缓存视图对象列表，不再每16ms遍历全部QObject，也不操作隐藏视图。Windows/Qt运行环境为Qt6.11.0；源代码仍以Qt6.9最低要求配置，尚未进行独立Qt6.9二进制构建验收。帧间隔是frameSwapped的本机观测，未强制更改显示器刷新率。
- 快速滚动策略现在考虑滚动条pressed状态和不触发Flickable.moving的跨距跳转。四面板脚本的跳转在11ms后均报告fast=true，停止时撤销快速状态。测试中单个模型32条插入最高1ms；仍观察到启动/首屏和截图附近超过50ms的心跳间隔，未证明应用全程无长停顿。
- 所有本轮脚本日志无QML TypeError/ReferenceError/Cannot assign。解码完成事件新增failed计数，避免将“预览不可用”占位误当成成功解码。

未完成项仍包括真实全格式、超大PNG/相机RAW/慢盘压力、1～4个不同目录的联合负载、全部缓存状态的重复矩阵、滚动停止后的严格补图P95、精确像素与GPU对照、实际峰值进程资源、Qt6.9独立构建和所有关键指标≤5%回退检查。当前正在等待真实验收图片目录，同时继续完善可用样本覆盖的验证。

源缓存正确性审计还修正了相机RAW两个边界：无处理参数的普通预览继续走嵌入预览路线；缓存保存文件的原生处理默认值和裁剪后几何，不保存创建缓存那次请求的显示参数，避免取消/重置参数后继承旧的黑白电平、白平衡或CCM。真实相机RAW的像素对照仍需样本验证。

## 不同目录与完整样本统计（2026-10-09）

本轮基准进程通过 MVPVIEW_PERF_SETTINGS_DIR 使用独立 INI 设置与诊断目录，在首个 QSettings 创建前配置，关闭实验进程的自动更新检查；正常运行仍使用现有设置。benchmark.settings 事件确认是否实际隔离，旧版二进制缺少该事件时报告未知。早期基准沿用了正常设置，当前采用新的默认设置，跨这两种配置的数字不能直接作为回退率结论。

--pane-images 可重复指定第2～4面板的目录；没有指定时使用主目录。基准不再为了启动/滚动场景提前枚举整个图片目录，仅全屏/对比需要挑选文件。支持BMP/DIB及当前相机RAW扩展名。示例：

```powershell
python tools/performance_benchmark.py --exe build/windows-msys2-release/MVPImageViewer.exe --images build/perf-fixtures/10000 --pane-images build/perf-fixtures/100 --pane-images build/perf-fixtures/100000 --pane-images build/perf-fixtures/1000-b --panes 4 --output build/perf-results/distinct-observed --runs 3 --scenario scroll --cache-state disk
```

停止指标通过 viewport.stopped/settled 的独立 stopId 配对，从停止计时到当前几何视口全部Ready后的帧交换。目录继续插入、排序或恢复锚点时，viewport.retargeted更新目标集合并保留最初计时；重新滚动或目录切换会终止该次观测。包含滚动条跨距跳转。已经Ready的视口额外请求一次交换来确认呈现。未补齐停止数大于零时 stopFillP95 为null，不丢弃失败样本来计算P95。demandViewports/unpresentedViewports记录滚动中已被替换的视口，viewportFillP95仍只代表已完成代次。

Windows下脚本每50ms读取子进程GetProcessMemoryInfo：peakWorkingSetBytes是操作系统记录的生命周期工作集峰值，peakPrivateCommitBytes是私有提交峰值；不等同于解码缓存，也不包含独立的GPU显存计量。其他平台暂报告backend=unavailable，不用零表示未知。loader.cache_state在插入与预算淘汰后记录缓存，避免使用提交前的旧值。loader.dispatched的pendingResults记录已预留结果额度。

四个不同目录分别10000/100/100000/1000条有效PNG，3轮重复（首轮预热、后两轮实验磁盘缓存；滚动中也包含未预热图片与内存命中）：

| 指标 | 第1轮 | 第2轮 | 第3轮 |
| --- | --- | --- | --- |
| 运动帧间隔P95 | 33ms | 33ms | 32ms |
| 停止/跳转观察数量 | 12 | 12 | 12 |
| 未补齐停止观察 | 0 | 0 | 0 |
| 停止后补图P95 | 156ms | 121ms | 133ms |
| 未呈现的运动视口代次 | 68/315 | 106/337 | 107/340 |
| 峰值工作集 | 894MiB | 828MiB | 853MiB |
| 淘汰后缓存峰值 | 359MiB | 356MiB | 381MiB |

日志位于build/perf-results/distinct-observed。截图确认四面板目录独立、可见缩略图已补齐；无QML异常或解码失败。该负载下滚动P95尚未达到20ms；不能用先前四面板共享同目录的17～18ms替代这项验收。停止后数字是当前人工PNG混合缓存负载证据，不能宣称全格式纯磁盘命中200ms已经验收。早期停止计数把目录插入引起的程序性位置变动当成用户停止，且直接按可见代次配对；已改为移动状态转变、显式测试跳转与独立stopId，本节采用修正后的数据。

Release回归测试最新通过6.63秒；新增递归查询验证根目录图像只出现一次、图片祖先分支保留、纯文档分支排除、自然排序与取消。仍需真实格式样本、GPU像素对照、Qt6.9独立构建和进一步定位不同目录持续扫描期间的帧间隔问题；完整目标继续进行中。

64项结果背压后另做最终烟测：不同目录四面板冷缩略图缓存下12次停止均完成，补图P95为158ms，运动帧间隔P95为33ms，实际最大预留结果14项，峰值工作集886MiB；事件确认设置已隔离。独立设置下slot-update仍通过slotUploads=[0]、viewUploads=0。结果位于build/perf-results/bounded-results与isolated-slot。

## RAW正确性与调度补充（2026-10-09）

新增tests/dng_fixture.h生成有效的未压缩Bayer DNG（1536×1024，已知14-bit样本，16-bit容器），通过真正的LibRaw打开、解包和处理。它不是注入缓存的假解码器，也不是真实相机拍摄样本。回归先后复现并修复：

- 源缓存命中时，Full CFA图错误套用960×720的CPU后备上限，实际图像变为960×640；CFA全图由编码纹理显示，必须保留原尺寸。现在缓存/首次解码的1536×1024 CFA像素逐点一致。
- LibRaw三通道16-bit RGB被当成Qt四通道RGBX64读取，导致无嵌入预览的DNG缩略图为空。现在按行写入拥有独立存储的RGBA64/RGBA8888，透明度始终不透明，避免引用已释放的LibRaw输出。按行检查取消；相机RAW缓存身份升级v8，旧像素缓存不会被错误复用。
- 自动发现文件RAW默认参数被当作用户编辑，触发ThumbnailUrlRole变化和第二次加载。adoptRawParameters只填充默认值；URI处理修订仅随真正编辑变化。首次结果按已发现参数的身份入内存缓存，复用原请求文件版本，避免重复计费、二次解码或错误归入新文件版本。

测试验证传感器样本(50,70)=2384、缓存默认处理与首次处理像素一致、取消后不渲染、首次URI稳定、重复请求返回同一不可变帧、编辑后正确失效。图片提供器从取消线程直接减少共享消费者，再在GUI线程完成Qt响应；起始与取消之间的竞态由互斥锁保护，避免等待GUI队列才通知解码器。Release构建和完整回归通过，最新7.23秒。

测试程序支持 --export-dng 输出同一有效fixture，目录创建由调用方负责：

```powershell
build/windows-msys2-release/mvpview_performance_tests.exe --export-dng build/perf-fixtures/dng/sensor0.dng
python tools/performance_benchmark.py --exe build/windows-msys2-release/MVPImageViewer.exe --images build/perf-fixtures/dng --output build/perf-results/dng-progressive --runs 2 --scenario startup --cache-state disk --capture-delay-ms 6000
```

本机64张fixture：首次未缓存补图381ms，下一轮实验磁盘缓存66ms；两个进程均只完成40个候选请求，没有解码失败，截图确认正常图像、1536×1024和14-bit信息。没有适用的原始DNG首屏速度对照：旧版本在无嵌入预览的16-bit RGB路径存在上述正确性错误。不能将占位图变成真实图像解释为30%速度达标。

## 帧节奏诊断（2026-10-09）

animated-scroll增加Qt动画时钟对照，不替换原来的timer scroll场景。相同不同目录四面板负载，3轮P95仍32/33/33ms，停止补图202/94/181ms。benchmark.runtime记录实际Qt版本、屏幕刷新率/DPR和影响Qt渲染的环境变量，减少测试配置歧义；capture-delay-ms允许慢格式延长观察窗。

开启QSG_RENDER_TIMING与QSG_INFO的单轮日志中，Qt明确报告broken vsync throttling并切换到系统定时器；按运动窗口相邻事件归类，GUI polish/sync的P95为0ms（毫秒取整，不能解读为没有成本），render约2ms、swap约1ms，框架输出本身的帧间隔仍约30ms。这里不是所有GUI任务的完整CPU采样，也不能据此证明没有其他停顿。数据位于build/perf-results/render-phase。

[Qt场景图文档](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)说明了vsync失效与QWindow请求更新等待的诊断方法。仅实验设置QT_QPA_UPDATE_IDLE_TIME=0，两轮P95为17ms，但P50为5～6ms，意味着绘制频率明显增加；停止补图138/239ms，并非所有指标改善。没有把该变量设置成应用默认，也没有改变OpenGL基线。实验位于build/perf-results/idle-zero，不能作为默认配置已达标的证据。

完整验收仍未结束：需继续验证真实相机/HEIF/超大PNG/慢盘压力、Qt6.9构建、GPU像素对照和完整回退矩阵。目标保持进行中。

## CPU图库预览的清晰度（2026-10-09）

RHI全图帧的原始平面为原生尺寸，但RAW/YUV的CPU后备图通常仅960×720。图库的QML Image只能显示CPU图，原来直接复用Full缓存会在切换到全图时降低清晰度。DecodeRequest新增requireDisplayImage：图库预览/全图请求独立的CPU表示，相机RAW与RAW/YUV的完整CPU显示保留原生尺寸；GPU画布仍使用有界后备图与原始平面。缓存身份包含表示类型，图库只复用同类足够清晰的Full/Preview；编码图片不需要不同表示，继续共享缓存。CPU专用表示不创建没有消费者的半浮点/P010 GPU上传缓冲。

回归通过真正的LibRaw DNG检查首次/源缓存CPU Full都是1536×1024且逐像素一致；先缓存960宽的GPU后备帧，再请求1536预览和CPU Full，验证不会降级、像素一致且再次预览直接命中CPU Full。1536×1024 RAW16和NV12也验证原生CPU尺寸。最终Release构建与CTest通过（8.71s）。

64张合成DNG的原生Gallery冷缓存单轮截图和日志位于build/perf-results/dng-gallery-display：首帧1315ms、可见缩略图首屏232ms、29个完成请求、无解码失败。该单轮在最后删除CPU专用上传缓冲之前运行，只用于UI与显示路径检查，不作为最终内存或全格式性能验收。截图确认大图及缩略图正常，测试的逐像素比对负责尺寸和缓存准确性验证。

## 显示设置变化后的图片身份（2026-10-09）

原有色彩设置处理会清空解码缓存并重新扫描目录，但图片URI仍只有文件版本和RAW编辑参数，无法使Qt Image自身的像素缓存失效。现在URI加入解码器显示配置身份的稳定摘要，包含ICC、方向、位深和目标色域设置。摘要按需初始化，避免在首帧前额外触发格式插件发现；只有显示身份变化才通知ThumbnailUrlRole，常规清缓存不会修改URI。设置变化不再重新扫描目录，Gallery现有onThumbnailUrlChanged同步更新当前预览。

回归使用16-bit Display-P3 PNG：保持设置清缓存时URI及通知次数不变；切换为8-bit后URI改变、对应图像角色恰好通知一次、实际解码为32-bit；恢复设置后URI恢复，模型没有reset、条目数量不变。Release构建及CTest通过（8.62s）。此测试确认像素缓存身份与解码结果，未将其当作真实显示器上的色彩/GPU像素验收。

## RAW/YUV源帧存储身份与缓存读取（2026-10-09）

源缓存命中原先仍执行QFile.open/seek。现在先用文件版本、帧索引、数据范围、尺寸、格式、有效步长、字节序、对齐及位深查源缓存，只有未命中才打开文件并分块读取。显示矩阵、白平衡、方向等显示处理不改变源字节身份。取消在查询文件之前检查，缓存命中仍在转换各行检查取消。

两帧NV12/NV21回归验证：矩阵变化后的Full结果与原帧共用同一个不可变字节缓冲，缓存仍24字节；切换布局产生独立24字节项；切换帧索引产生第三项且精确Y样本为第二帧的200，不复用第一帧128。Release构建和CTest通过（8.97s）。缓存命中仍会读取文件版本信息；这不等于已经消除慢盘上的所有stat延迟。

## 待提交解码结果的字节预算（2026-10-09）

原先最多64个结果及运行中任务的数量约束不能限制多个大预览占用的字节数。新增ResultBufferBudget，把工作线程结果提交前、invokeMethod排队期间和GUI分批提交期间的像素帧纳入同一个128MiB预算。工作线程等待可用预算，GUI只释放预约，不等待；每25ms检查消费者取消，退出时close唤醒等待线程，再waitForDone。单张超过预算的精确全图允许独占空预算，保持大图格式和精确检查可用；其他结果必须等该预约释放。既有64项数量上限及4ms GUI提交预算继续保留。

loader.completed的elapsedMs继续统计解码/准备时间，bufferWaitMs单独统计字节预算等待；loader.result_buffers输出占用、峰值及等待线程，基准汇总maxResultBufferBytes/maxResultBufferWaitMs。未新增上传或解码线程。元数据结果只共享已有像素帧，不重复预约像素字节。

回归检查超额预约等待、释放唤醒、取消、单张超预算独占及退出唤醒；最终Release/CTest通过（8.67s），Python基准脚本语法检查通过。原生测试：

- build/perf-results/result-byte-budget：四个不同目录（1万、100、10万、1000条目）同时动画滚动，两轮默认OpenGL/Qt6.11、独立设置，第一轮冷、第二轮使用已积累磁盘缓存。停止视口12/11个，全部补齐，停止补图P95为145/108ms；结果峰值6.9/12.8MiB，预算等待0ms，解码失败0。运动帧间隔P95仍30/32ms，未满足20ms。进程峰值工作集约880/850MiB，不能把128MiB结果预算当成进程内存上限。
- build/perf-results/result-byte-budget-large：四张3072×3072 RGB16 PNG的合成渐变对比，单轮结果峰值113246208字节（108MiB），预算最长等待8ms，4个预览正常显示、无解码失败；截图已检查。进程峰值工作集738004992字节。合成渐变不代替真实超大PNG/全部格式的改善30%验收。

该预算限制的是提交队列：解码器正在构造的帧、LibRaw/Qt临时分配、会话活动帧、Qt Image缓存和GPU纹理仍需联合统计与调度。工作线程数量提供在途结果的数量边界，但还没有这些资源统一的字节准入约束；保持该项未完成。

## 视口随帧上报与大目录锚点（2026-10-09）

移除可见视图持续运行的16ms Timer。几何变化标记dirty并请求一帧，afterAnimating中合并需求；不滚动且几何不变时不轮询。隐藏或窗口最小化立即撤销需求。GUI同步前的信号时序见[Qt QQuickWindow文档](https://doc.qt.io/qt-6/qquickwindow.html#afterAnimating)。性能观测新增viewport.report，按面板/GUI帧检查重复上报。

排序优化后首先跑出的frame-demand-native-sort虽然运动帧间隔P95达到18/17ms，但两轮均有一个停止视口未补齐，因此未作为通过结果。检查发现旧锚点用尚未布局的indexAt计算，且每次插入都callLater恢复，未考虑GridView origin偏移。现在从真实可见委托保存文件路径，在本帧模型变化合并后只forceLayout一次，优先按同路径委托的实际y恢复，缺失时才使用含originY的行坐标。布局及origin规则见[Qt GridView](https://doc.qt.io/qt-6/qml-qtquick-gridview.html#forceLayout-method)、[Qt Flickable](https://doc.qt.io/qt-6/qml-qtquick-flickable.html#originY-prop)。

最终build/perf-results/frame-anchor-fix：1万、100、10万、1000条目四个不同目录同时前滚、跳跃、反向和停止，两轮独立设置、默认OpenGL、Qt6.11，第一轮冷、第二轮复用磁盘缓存。每轮12个停止视口全部补齐，停止补图P95为100/78ms，运动帧间隔P95均17ms，重复上报0、解码失败0；十万目录分别在进程时间10560/10301ms完成入模。进程峰值工作集758976512/680452096字节，解码缓存峰值209350656/207507456字节。相较之前大量重复需求，完成请求降为437/431，但不把请求数量下降单独当作准确性证明。具体帧间锚点路径/像素偏移仍需更直接的自动回归。

List/Gallery单面板一万条目冷缓存的frame-demand-list、frame-demand-gallery在最终锚点修复前检查：每轮3个停止视口全部补齐，停止补图P95为52/76ms，运动帧P95为12/15ms，重复上报0。100张PNG的frame-demand-startup三轮首屏143/119/122ms，不能宣称冷首屏改善30%。最终四面板日志的GUI心跳仍记录启动阶段70～290ms停顿；完整硬性验收没有通过。这些有限的合成PNG结果也不能推广到全部格式、Qt6.9或慢盘。

## 大目录排序比较开销（2026-10-09）

ThumbnailFilterProxyModel在设置源模型时保存安全的QPointer。ThumbnailModel路径直接比较ImageFileRecord字段和已准备排序键，减少每次比较中的qobject_cast、角色分发及QVariant构造；其他源模型继续走通用角色路径，缺失文件类型仍保留原有推断。相等类型键直接比较自然名称，不再重复调用类型collator。扫描线程在同一次扫描中共享重复扩展名的不可变字符串和类型键，名称键仍分别准备；缓存不跨collator/扫描会话。

测试程序新增仅供内部使用的--sort-benchmark。100000条目同机Release内部前后单次对照（不是原始3a6d623基准）：

| GUI模型操作 | 字段比较前 | 字段比较后 |
|---|---:|---:|
| 初始映射 | 39ms | 30ms |
| 类型排序 | 40ms | 22ms |
| 大小排序 | 43ms | 18ms |
| 修改时间排序 | 42ms | 17ms |
| 名称排序 | 31ms | 21ms |

日志为build/perf-results/model-sort-before.log、model-sort-after.log。测试入口先同步准备扫描记录（8116/8296ms），该部分不包含在GUI排序表中；生产路径在DirectoryScanner工作线程扫描。复现命令：设置QT_QPA_PLATFORM=offscreen、QT_FORCE_STDERR_LOGGING=1后运行build/windows-msys2-release/mvpview_performance_tests.exe --sort-benchmark build/perf-fixtures/100000。不要将单次测试解读为完整P50/P95或其他指标不回退的证明。

回归覆盖混合目录/扩展名、自然数字名称、准备键/缺失键和缺失类型的顺序一致，以及替换成通用QStandardItemModel后不使用旧源字段。最终Release构建、CTest通过（7.66s），Python基准脚本语法检查通过。

## 启动剪贴板探测（2026-10-09）

新增按作用域开始/结束的性能事件，分别覆盖首次剪贴板检查、浏览器初始化、系统导航图标、导航位置/磁盘列表和目录打开GUI阶段。未开启MVPVIEW_PERF时不启动计时或输出日志。首次剪贴板绑定原来会进入Qt/OLE格式枚举，即使没有任何可粘贴文件格式也耗时约200ms。

Windows平台先调用IsClipboardFormatAvailable检查文件、URL、文本和Shell路径相关的标准/注册格式，仅在全部缺失且没有API错误时返回不可粘贴。该API查询格式可用性而不读取数据，参见[Microsoft文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-isclipboardformatavailable)。有任一格式、注册失败、查询错误或非Windows QPA插件时，继续使用原有Qt解析和每次剪贴板变化的缓存失效逻辑。没有修改用户剪贴板，也没有将文件/文本内容读取改成异步。

两个相邻版本分别运行100张合成PNG、两轮冷缓存、独立设置、原生Windows/默认OpenGL/Qt6.11：build/perf-results/startup-phase-scopes的clipboard.probe为206/198ms；build/perf-results/clipboard-format-guard均记录clipboard.no_file_formats，probe为0/0ms（整数毫秒精度）。对应首帧1341/1152ms与1042/907ms，首屏补图140/129ms与125/120ms。该两轮对照仅确认当前缺失格式的剪贴板路径改善，不是原始基准的正式P50/P95验收，不能推论有数据的剪贴板同样不阻塞。

最终Release构建、CTest通过（7.45s）。offscreen剪贴板回归覆盖文件URL、剪切标记、普通文本使旧缓存失效、纯文本路径和清空；测试不接触真实Windows剪贴板。新的原生日志仍记录75～142ms启动GUI心跳间隔；首次图标和浏览器初始化等开销继续保留作用域观测，50ms硬性停顿验收未通过。

## 浏览模型装饰位图按需创建（2026-10-09）

ThumbnailModel原先在构造时用QPainter绘制Loading文字并读取主题文件夹图标，即使生产QML只使用ThumbnailUrlRole，也会触发字体/图标初始化。现在保持空位图，首次读取对应Qt::DecorationRole时才创建并保留；旧式模型消费者仍得到相同装饰，不改图片URL或解码行为。两个辅助函数加入model.text_placeholder/model.folder_placeholder作用域观测。

Release构建、CTest通过（8.00s）。build/perf-results/lazy-decoration-startup与CTest曾并行执行，不用于性能对照；随后独立重复的build/perf-results/lazy-decoration-startup-serial为原生Windows、默认OpenGL、Qt6.11、100张合成PNG、两轮冷缓存和独立设置。首次browse.initialize为41/35ms，前一提交clipboard-format-guard为148/88ms；生产启动没有model.*占位绘制事件。整体首帧1015/933ms、补图123/126ms，与前一提交1042/907ms、125/120ms相比没有证据支持整体改善，字体初始化可能移到了真实文字绘制。GUI心跳仍有72～132ms间隔。该修改只确认移除了不使用的构造工作，不宣称50ms启动停顿或完整性能目标达成。

## 导航Shell图标异步加载（2026-10-09）

SystemFolderIconProvider使用ForceAsynchronousImageLoading，将Quick Access的Shell图标查询交给Qt的图片加载线程。requestImage创建局部QAbstractFileIconProvider，不再访问GUI线程创建的QFileSystemModel，也不为图标提供器额外构造文件系统模型。保留原生按路径图标和Folder后备规则、图片URL及Qt图片缓存，不增加解码线程。线程行为见[Qt图片提供器文档](https://doc.qt.io/qt-6/qquickimageprovider.html)；[Qt6.9 Windows图标实现](https://raw.githubusercontent.com/qt/qtbase/v6.9.0/src/plugins/platforms/windows/qwindowstheme.cpp)也有工作线程COM初始化和Shell查询路径。静态源码检查不能代替Qt6.9独立构建。Qt该类型使用每个引擎一个图片加载线程，慢Shell图标仍可能延迟同线程的其他图片；没有把这个有限导航请求优化当成全局资源预算已完成。

navigation.icon记录guiThread，navigation.icon_result记录空图和尺寸。Release构建、CTest通过（7.60s）。原生独立测试build/perf-results/async-native-icons：100张合成PNG、三轮冷缓存、独立设置、默认OpenGL/Qt6.11，每轮5个图标均guiThread=false、非空，日志没有线程/图片警告，截图确认Home/Desktop/Documents/Downloads/Pictures图标正常。首帧895/851/892ms，首屏补图125/124/141ms；相邻版本lazy-decoration-startup-serial首帧1015/933ms，但不同样本次数的局部对照不构成正式回退矩阵。GUI心跳仍72～138ms，启动50ms门槛未通过。

build/perf-results/async-icons-four-panes：1万、100、10万、1000条目四个不同目录，两轮动画前滚/跳跃/反向/停止，第一轮冷、第二轮复用本次积累的磁盘缓存。每轮12个停止视口均补齐，停止补图P95为107/74ms，运动帧P95为16/17ms，重复上报与解码失败均0，峰值工作集750358528/618946560字节。与前次frame-anchor-fix的100/78ms相比冷停止补图单次增加7ms，不能声称全部指标回退不超过5%；需扩充配对重复次数并覆盖真实格式。

## 像素分配前的解码工作预算（2026-10-09）

此前128MiB结果预算只在完整帧生成后准入。DecodeRequest现在提供工作线程的reserveWorkingMemory/prepareAllocation接口，生产ImageLoader在解码期间管理独立的512MiB工作预约；Qt在读取头信息后、read之前申请，RAW/YUV在源字节resize和转换前申请，LibRaw在缓存源帧渲染、unpack_thumb或unpack前申请。缓存版本和图像身份不包含此调度接口，直接解码调用未配置预算时维持兼容行为。缺少Qt头尺寸时按整个工作预算估算，避免无尺寸请求默认零成本。

Qt PNG/JPEG/BMP按整数像素表示估算原生8字节/像素及显示/转换/上传16字节/像素，其他插件为原生16及输出32字节/像素；即使插件仅在解码后缩放，也保留原生像素估算。RAW/YUV包含源读取、上传副本和输出转换；LibRaw按原始传感器48字节/像素估算工作缓冲，缓存传感器渲染仅计新的显示缓冲。算术饱和避免大尺寸乘法溢出。它是调度估算，不是逐个库分配的精确统计；头解析、插件内部和色彩库不透明临时分配不能由此保证RSS上限。

预算等待按实时优先级和同级先后顺序准入。合并请求及视口更新同步原子优先级并唤醒等待者；消费者取消每25ms检查，退出关闭两种预算后再等待工作线程。工作预约保留到上传准备及结果字节预约成功后释放。允许单张超过预算的图像在空预算内独占，保留精确全图和超大格式支持。解码缓存384MiB、自动原图256MiB、结果128MiB和既有并发上限不变，没有增加线程。metadata结果和内存命中不重复预约已有像素。

loader.decode_working输出估算预约和峰值；loader.completed的workingWaitMs独立于解码elapsedMs及结果bufferWaitMs，基准增加maxDecodeWorkingBytes/maxDecodeWorkingWaitMs。回归验证两个400MiB的并行任务不能同时进入像素阶段、等待请求的动态提升、预算取消/退出/超额独占，以及Qt PNG/JPEG/BMP、NV12源缓存和真正LibRaw DNG在拒绝准入时不返回像素。最终Release构建与CTest通过（7.73s），Python语法检查通过。

初版256MiB与统一偏保守的估算使合成四图对比最后上传相对首帧推迟到357ms，因此未保留为默认；按格式估算和优先级后仍需合理的独立工作空间，最终采用512MiB。早期实验decode-admission-large、decode-admission-large-integer、decode-admission-priority-large不是最终验收结果。

- build/perf-results/decode-admission-final-large：四张3072×3072 RGB16合成PNG，两轮冷缓存、独立设置、默认OpenGL/Qt6.11；四个Preview的工作预算等待均0，最高预约529530880字节，低于512MiB；仅缩略图最长等待65ms。结果峰值113246208/75497472字节，无解码失败。峰值工作集686714880/616075264字节，最后预览上传相对首帧123/122ms。前一结果预算实验单轮同指标约101ms，非配对样本且中间有其他改动，不能证明≤5%回退；还需正式对照和真实超大PNG。四图截图已检查，不能代替GPU精确色彩验收。
- build/perf-results/decode-admission-final-four-panes：1万、100、10万、1000条目四个不同目录，两轮动画前滚/跳跃/反向/停止；第一轮冷、第二轮复用本次积累磁盘缓存。每轮12个停止视口全部补齐，停止补图P95为89/62ms，运动帧P95为17/16ms，重复上报和解码失败均0，工作预算等待均0。工作预约峰值120440832/60220416字节，进程工作集峰值696340480/619020288字节。

分配前估算准入已接入三类生产解码器，但源缓存、活动帧、Qt Image、上传所有者及GPU仍未形成去重的统一账本或全局准入；512MiB工作预算不等于进程内存上限。等待预算的工作线程仍占执行通道，极端压力下需继续验证新交互的通道等待和跨面板公平性。完整目标仍进行中。

## 预算等待时保留交互执行通道（2026-10-09）

新增回归复现了调度层和预算层之间的缺口：20个可见图片任务各请求400MiB工作预约，普通通道只有一个能获得像素预算，其余工作线程等待；后来的32MiB交互请求有足够剩余字节，却不能进入全部已占用的普通线程池。旧实现的预留规则只对priority<60生效，VisibleThumbnail可以占满通道。新增回归在修复前失败（800ms等待超时，CTest约0.92s），不是仅凭代码推断。

现在priority<100的普通任务，包括可见缩略图，在现有普通池内最多占maxThreadCount-1个执行通道。当前大图和精确检查可使用剩余通道，并按上一阶段实时预算优先级获得字节。普通并发上限、相机RAW串行池及所有字节预算不变，没有扩大线程池。已经执行且仍有消费者的解码不会被强行打断；交互请求自身超过剩余字节时仍须等待预算释放，不宣称它总能立即开始。

最终Release构建、CTest verbose通过（7.69s），该压力回归实际记录Budget pressure interactive start: 2ms；验证旧消费者取消后退出、不丢交互结果、峰值并发不超过6。受控假解码器使用预约值而非实际分配400MiB，只证明通道/预算协调，真实像素压力另由大PNG场景覆盖。

build/perf-results/interactive-lane-four-panes：原生Windows、默认OpenGL、Qt6.11、独立设置，1万、100、10万、1000条目四目录同时动画前滚/跳跃/反向/停止，两轮第一轮冷、第二轮复用本次磁盘缓存。每轮12个停止视口全部补齐，停止补图P95为99/71ms，运动帧P95均18ms，重复上报与解码失败均0，工作预算等待均0。工作预约峰值100367360/60220416字节，进程峰值工作集691449856/617631744字节。相邻阶段运动P95为17/16ms、停止补图89/62ms，两轮样本不能证明≤5%回退；20ms滚动目标在这些有限合成PNG样本中满足，但正式全格式和配对回退矩阵仍未完成。
