# ConcordRTS

ConcordRTS 是使用 C++20 开发的 RTS 引擎工程，面向《命令与征服：红色警戒 2》及《尤里的复仇》（RA2/YR）的规则、地图和资源格式。

本仓库公开作者自行编写的引擎源码、构建工具、测试代码与示例，不附带原版游戏程序或官方素材。运行所需的游戏数据由使用者自行准备。

主要 C++ 接口、配置入口和命令行工具说明见 [API 文档](API.md)。

## 功能与开发状态

源码包含遭遇战、AI、战役、局域网与互联网联机、观战、存档与联机恢复，以及规则分析、地图解析、资源读取和导出工具。遭遇战菜单可选择 Win32 或可选的 SDL3 软件窗口；Win32 大厅和选图页另可显式启用 GPU 绘制，开始战局后使用 Win32 游戏窗口。主战局默认使用软件绘制与呈现，也可显式选择 D3D11 完整帧呈现或地图 GPU 绘制。地图 GPU 路径覆盖地形、物体材质、阴影、光照与迷雾显示；资源解码、几何、地形查询、视野和模拟仍由 CPU 的原实现负责。硬件与 WARP 分别选择。

另提供独立核心构建，支持在不创建窗口、图形、音频和视频后端的情况下构建所选实际生产模块。已验证的核心环境为 Windows Clang；文件和 LAN 仍保留私有操作系统适配，不表示整个游戏已完成跨平台运行。

项目仍在持续开发，不保证原版所有细节、全部战役或任意第三方 MOD 均已完整兼容。

## 更新记录

### 2026-10-28

- 将地图绘制接入 GPU，覆盖原 TMP 索引地形、SHP／VXL 材质、阴影、世界与局部光照、辐射效果及迷雾显示，保留原调色板、remap、整数颜色精度和稳定绘制顺序。
- 增加自有地图资源、绘制命令与原生覆盖合成接口，复用现有 D3D11 呈现器；正常路径不先生成完整 CPU 地图图像，也不逐帧读回 GPU 图像参与合成。
- 保留 CPU 的原资源解码、VXL 投影、索引深度、几何命中、地形和视野权威；接入原 Reference／Point 完整 VXL 模式，保持炮身、炮塔、炮管及阴影动画的既有行为。
- 修复 CPU 地形查询破坏 GPU 地形缓存编码的问题，保持同相机缓存复用、跨区域查询、原几何深度与双重归属信息，以及软件回退时的真实 RGB 数据。
- 增加独立的 `--map-drawing` 选项、地图着色器源码和原生地图合同；保留软件默认，以及设备或分配失败时的同帧软件回退。
- 开发验收完成 616 帧完整图像对照、156 个完整窗口案例、735 项原生 GPU 合同检查、20 项相关 CTest 和 62 个结构夹具检查；这些结果覆盖所选场景，未运行全部已注册测试，既有动画及场景深度问题仍保留。
- 分别记录软件、硬件与 WARP 的 CPU 准备、上传提交、GPU 合成和窗口呈现耗时，并对照混合移动场景的模拟状态。当前测量未证明整体加速，默认路径保持不变。

### 2026-10-27

- 将 Win32 遭遇战大厅和选图页接入实际 GPU 绘制，覆盖转场、按钮、下拉选项、地图预览、位图文字和软件鼠标；复用原资源解码、控制器及输入逻辑。
- 增加有序菜单绘制命令、资源代际和同步借用接口，GPU 直接处理索引图片、调色板、字形与客户区映射；正常路径不生成 CPU 完整菜单画面，也不做逐帧 GPU 读回。
- 提供独立的菜单绘制选择和离线着色器构建；保留软件默认、显式硬件与 WARP，以及失败或进入未支持菜单页时的软件回退。战局画面仍由 CPU 绘制。
- 修复下拉菜单关闭后、下一次重绘前仍可能命中旧弹出选项的问题，补充对应控制器回归断言。
- 完成所选完整菜单的像素、输入、资源寿命和软件回退对照，补充原生光栅合同与结构检查。所选菜单的 CPU 准备耗时有所降低，当前测量不构成全引擎提速或实际屏幕帧率结论。

- 接入 Windows D3D11 完整帧呈现：地图、界面、FPS 和软件鼠标仍由 CPU 组成，再按客户区尺寸上传并显示；保留软件呈现默认值，以及显式硬件和 WARP 选择。
- 完成窗口缩放、最小化恢复、焦点切换与关闭处理；设备创建、资源或呈现失败时释放 GPU 资源，并在当前窗口会话中转回原 GDI 路径。
- 补充原生 D3D11 合同和主窗口完整画面对照，覆盖硬件、WARP、软件路径、实际 API 错误及明确的故障注入；所选完整帧颜色字节和权威状态保持一致。
- 分别记录 CPU 画面组成、上传调用、GPU 上传时间与 Present 调用时间，并记录实际采用的后端和回退原因。已完成所选可见窗口采样，现有结果不支持整体提速结论，默认选择保持不变。

- 增加可选的 SDL3 遭遇战菜单窗口，复用大厅、地图选择、槽位设置及确认、取消流程；确认后进入原 Win32 战局，默认窗口仍为 Win32。
- 提供类型化的软件窗口与事件接口，菜单通过完整软件帧接口同步呈现；补充客户区像素坐标转换、缩放、鼠标捕获、焦点与关闭处理。
- 固定 SDL3 依赖版本、源码校验与本机构建入口，按需加载程序旁的 DLL，并将依赖来源、许可与通知文件纳入部署校验。
- 完成两种窗口所选菜单场景的画面、控制器状态、输入与生命周期对照，验证确认后进入战局；扩充真实窗口合同与结构检查。当前范围仅为 Windows 上的位图遭遇战菜单，主战局绘制与菜单音频继续使用已有实现。

- 将初始寻路目标候选计算接入有界执行器，先逐项对照同步结果，再按固定消费顺序采用结果；输入和输出独立持有，失败时使用相同输入同步计算，保留原搜索、队列、随机数与逻辑预算。
- 提供 `reference / shadow / parallel` 模式与 `0 / 1 / 2 / 4` 工作数选择。普通运行默认仍为 `reference`，并行仅作用于初始候选内核，现有测量不足以支持默认提速结论。
- 提取主窗口像素视图、后台缓冲所有权及 `SoftwareFrameView / FramePresenter` 接口，交付包含地图、界面、FPS 和软件鼠标的完整软件帧。
- 保留缓存及暂停画面的场景版本，区分提交尝试与实际成功；仅在原生绘制完成后同步借出像素，保留资源不足时的原 GDI 回退路径。
- 完成所选候选顺序、完整状态与保存文本、实际保存载入、回放、切图及双进程联机对照；补充完整画面、输入、窗口生命周期与资源失败合同，扩充结构检查。
- 主战局默认呈现为 Windows GDI，字体组成仍使用私有窗口设备适配。既有矿物生成器动画及部分场景深度、地面残骸复合检查失败继续保留，所选合同通过不代表全部渲染已通过。

- 根据本项目的功能需求，重新实现 SHP 图像解码、辅助动画帧选择、恐慌反应、开火临时视野与等级视野倍率相关逻辑，保留既有玩法、规则参数和消费者接口。
- 将 SHP 帧记录、行数据范围与像素展开分开处理，保留预览和运行时的兼容策略；辅助动画集中处理帧范围、往返循环、播放节奏与整数边界。
- 整理恐慌状态的刷新、暂停和到期处理，以及开火视野的重叠、续期和保存恢复；等级视野按当前等级的能力集合应用一次倍率。
- 补充自编测试输入和边界检查，覆盖长行与截断数据、动画循环与极端整数、恐慌恢复、视野重叠及非法输入。新增辅助动画合同已接入 CMake/CTest。
- 清理相关历史外部引用、注释和诊断表述，更新接口与测试说明。

### 2026-10-26

- 补充生产循环的 CPU 模拟、绘制、GDI 提交和加载阶段计时，保存有界原始样本与分位数，增加独立的设备能力诊断和重复采样工具。
- 新增标准库实现的有界任务执行器，支持同步参考模式、受限队列和字节预算、代际失效、合作取消、单次结果领取，以及停止和线程回收。
- 将巨炮炮塔的投影面光栅化接入首个实际并行预热任务。资源选择、解码、投影、炮管处理和结果安装继续由原所有者负责，工作线程只读取独占输入并写入私有输出。
- 增加 `0 / 1 / 2 / 4 / auto` 预热工作数选择，保留普通运行默认值。队列、执行器工作区、准备输入和安装后缓存分别设限，失败时保留已验证的默认朝向。
- 完成所选并行模式的像素、阴影、材质和缓存结果对照，核对真实加载、完整画面及原模拟顺序和世界哈希；补充取消、过期结果、错误所有者、异常及会话替换检查。
- 保留执行器与光栅内核的独立合同工程和素材集成测试，扩充结构保护。独立设备探针不代表游戏已采用 GPU 绘制，局部并行验证也不代表整个模拟已经并行化。

### 2026-10-24

- 提取游戏窗口生命周期与遭遇战菜单控制器，通过类型化服务连接原窗口、输入和配置，保留同步重入、关闭清理与选择提交顺序。
- 整理菜单图像资源、Westwood 位图字体解码与文字绘制，复用既有资源搜索和缓存，明确像素、颜色、字形及借用数据的接口。
- 分离菜单 CPU 像素缓冲与 Windows GDI 后台缓冲，保留原混合、缩放、刷新和呈现路径，补充创建失败、释放重试与资源所有权检查。
- 分离菜单音乐策略与 Windows 播放设备，保留轮询、循环重播和失败重开行为；不支持的设备明确返回失败。
- 将所选 TCP 消费者接入私有传输库，并提取联机快照会话的暂存、校验与提交状态，保留原协议和传输行为。
- 增加 `CONCORD_PORTABLE_CORE` 独立构建入口，复用实际世界、规则、联机和文件实现。Windows Clang 的 Release、Debug 核心配置各完成 10 项测试，未跳过；核心构建不创建游戏窗口或图形、音频、视频后端。
- 扩充窗口、图像、字体、像素表面、音乐、TCP 和快照的合同测试，保持运行时与普通测试各自的编译及 CRT 约定，并更新精确结构基线。

### 2026-10-23

- 提取 Bink 播放服务，集中管理解码子进程、进程间通信和自有帧像素，保留原视频协议、播放时钟、音量与停止清理行为。
- 将 LAN 大厅地图传输整理为独立状态机，通过显式输入与动作连接会话，保留原消息格式、重传、超时、完整性检查与缓存提交顺序。
- 完成本地保存、回放 Reset 与网络快照对计分、采矿、经济生产和定时炸弹状态的恢复一致性检查，补充非法 tick 与零校验值拒绝，保留原保存格式及事务提交顺序。
- 为离线能力和回放报告标注结论来源、输入方式、覆盖范围及未知项，新增可选 JSON 报告，明确区分输入事件与真实游戏运行验收。
- 增加结构防回退工具，检查模块依赖、平台类型、源码归属、测试注册和编译参数，并保留逐项审核的历史例外基线。
- 补充视频服务、地图传输、恢复一致性、离线报告与结构检查的测试源码和自编夹具。

- 将特殊机制的内嵌诊断迁到独立编译单元，保留原诊断入口、测试输入与断言。
- 提取 C4 与 Ivan 定时炸弹状态模块，通过对象事实、能力查询和事件接口连接原宿主，保留原保存格式、哈希及读取事务。
- 将寻路目标候选与受阻目标的环形回退候选整理为独立模块，保留候选顺序、截断规则，以及原搜索、队列、缓存和预算行为。
- 提取生产规则值类型与解析器，保留数值校验、默认值、配置叠加和警告顺序，并接入原游戏及工具目标。
- 为地图与地形绘制提取短期只读帧输入，集中准备资源就绪状态、光照和视野信息，保留原绘制、缓存查询与离屏视野处理时机。
- 增加定时炸弹、寻路候选、生产规则与地形帧测试源码，完成所选状态、事件、保存、候选序列及完整画面的前后对照。

### 2026-10-22

- 统一超时空传送的位置提交与生命周期通知，保留原地形判断、保存恢复及回调顺序。
- 将普通矿车、移动奴隶矿车与奴隶矿工的 AI 采矿改为显式户级经济上下文，移除临时替换玩家经济和科技状态的旧作用域。
- 提取 ComputerMining 状态的保存、读取和哈希接口，保留原保存格式、字段顺序与读取失败时状态不变的语义。
- 将每户经济收入与生产队列事务整理到独立固定步模块，保留收入、扣款、队列提交、世界交付及界面反馈的先后关系。
- 增加传送提交、经济上下文、采矿编解码与经济生产固定步测试，保留完整文本预期夹具以及保存恢复、旧新行为对照检查。
- 提取输入像素点、尺寸、矩形和 UI 毫秒时钟接口，保留原坐标比较、计时边界与调用时机。
- 将键盘准入与路由、选择编组、菜单会话、聊天文本和侧栏分页整理到类型化控制器，接回原命令与状态接口。
- 分离左右键手势、光标判断及取消与捕获处理，保留原同步重入、偏好保存和命令提交时机。
- 为导航地形提取只读查询端口和独立结果类型，保留墙、桥、岸线与地形通行判断。
- 将 TMP 类型、普通与 raw format 3 解码、资源定位及缓存整理为独立模块，绘图继续使用同一资源实现。
- 增加所选输入链、地形查询与 TMP 的独立合同和集成诊断，核对保存、命令传输与模拟消费的一致性。

### 2026-10-18

- 提取独立的席位角色判断与比赛计分模块，保留原记录、保存和哈希语义。
- 将纯计分诊断移到独立测试，继续调用生产实现。
- 提取共享 CSF、PAL、SHP 解码，明确保留各消费者的文字、颜色和格式兼容策略。
- 为地图预览增加显式资源会话、九类缓存及刷新生命周期。
- 为 CSF 和 INI 建立按消费者编译约定区分的静态库。
- 将回放事件快照拆到窄头文件，保持字段、默认值和原接口，并减少真实分析消费者的头文件依赖。

### 2026-10-17

- 将日常构建切换为 `clang-cl + Ninja`，保留显式 MSVC 参考入口，缺少工具时直接报错。
- 恢复并验证 Clang 光照处理的标量、SSE2、AVX2 路径，保留安全 CPU 分派和像素一致性检查。
- 完善游戏与视频助手的依赖检查、CRT 来源记录、部署校验及失败恢复。
- 在未安装完整 Visual Studio IDE 的 Windows 环境完成实际构建、部署，以及 12 项无需游戏素材和 4 项本机素材回归。
- 验证动态视频菜单、基本游戏操作、原生存档与载入及正常退出。

### 2026-10-14

- 完成 Clang Release x64 游戏程序与 x86 视频助手的实际编译和链接，并验证两者的跨进程通信。
- 将运行配置、命令行覆盖项与偏好保存接口整理到 `src/runtime/config/`，配置头文件与游戏窗口、模拟状态、渲染器分离。
- 新增地形原生深度计算与对应诊断代码。
- 完善 MSVC 参考构建的缓存、工具实例、架构、CRT 与 ABI 检查，部署时记录程序和运行库的 SHA-256 来源。
- 增加 Windows `clang-cl + Ninja` 配置入口与编译器合同，区分操作系统、编译前端、Microsoft ABI 和 CPU 架构。
- 增加 Windows 本地测试清单、分层执行与结果汇总入口，区分无需游戏素材的检查、本机素材检查及各类执行结果。

## 仓库内容

以下展示主要目录与常用入口：

```text
ConcordRTS/
├── src/                            引擎与运行程序源码
│   ├── core/                       基础工具与不依赖界面的数据逻辑
│   │   ├── io/                     INI 与文件读写
│   │   ├── players/                纯席位角色判断
│   │   ├── tasks/                  有界任务执行器、输入与结果所有权
│   │   └── results/                比赛计分、保存与哈希
│   ├── data/                       规则、美术、地图与战役数据库
│   │   └── rules/                  规则数据库、生产规则类型与解析
│   ├── assets/                     游戏资源格式读取、检查与导出
│   │   ├── text/                   共享 CSF 文字与 Westwood 字体解码
│   │   ├── menu/                   菜单图像值、调色板与资源接口
│   │   ├── codecs/                 共享 PAL、SHP、TMP 类型与解码
│   │   ├── terrain/                TMP 资源定位、缓存与元数据查询
│   │   ├── preview/                地图预览、资源会话与缓存
│   │   └── video/                  视频格式、播放服务与帧通信
│   ├── world/                      世界状态、对象索引与查询
│   ├── simulation/                 战斗、移动、经济、AI 与特殊机制
│   │   ├── ai/                     AI 采矿状态与保存、哈希接口
│   │   ├── economy/                户级经济上下文与收入固定步
│   │   ├── production/             生产队列与扣款事务
│   │   ├── abilities/              单位能力与特殊机制
│   │   │   └── unit_special/       定时炸弹状态、事件与独立诊断
│   │   ├── navigation/             导航、寻路与移动查询
│   │   │   ├── pathfinding/        寻路队列、目标候选与确定性候选任务
│   │   │   └── terrain/            导航地形类型与只读查询端口
│   │   └── superweapons/           特殊武器与传送位置提交
│   ├── runtime/                    运行时配置与游戏宿主
│   │   ├── config/                 配置类型、解析与命令行覆盖
│   │   ├── contracts/              回放事件与菜单选择数据
│   │   ├── window/                 窗口标识、生命周期与软件窗口接口
│   │   │   └── sdl3/               可选 SDL3 窗口、事件与软件呈现
│   │   ├── menu/                   遭遇战菜单控制器与同步服务
│   │   ├── persistence/            保存、加载与恢复一致性诊断
│   │   ├── replay/                 回放封包与宿主恢复内核
│   │   ├── performance/            生产循环性能计时与诊断
│   │   ├── presentation/           完整帧、后端选项与私有呈现适配
│   │   │   └── win32/              GDI、D3D11 完整帧、菜单与地图 GPU 绘制
│   │   │       ├── MenuRaster.hlsl  自编菜单顶点／像素着色器源码
│   │   │       └── MapRaster.hlsl   自编地图顶点／像素着色器源码
│   │   └── host/                   启动检查、游戏窗口与战局宿主
│   ├── render/                     地图、单位、效果与软件／GPU 绘图接入
│   │   ├── core/                   地图绘图、短期帧输入与自有 GPU 命令资源
│   │   ├── buildings/              建筑绘制与辅助动画帧选择
│   │   ├── voxel/                  体素绘制、巨炮光栅任务与预热生命周期
│   │   └── resources/              菜单资源接口的既有缓存实现
│   ├── ui/                         菜单、输入、HUD 与偏好保存
│   │   ├── input/                  输入值、键鼠、聊天与交互控制器
│   │   ├── time/                   UI 毫秒时钟与原计时策略
│   │   ├── values/                 与设备无关的 UI 颜色值
│   │   ├── drawing/                有序菜单命令、资源字节与代际
│   │   ├── surface/                CPU 像素缓冲与绘图操作
│   │   │   └── win32/              后台缓冲、像素借用与地图原生覆盖合成
│   │   ├── text/                   遭遇战位图文字测量与绘制
│   │   ├── audio/                  菜单音乐启停与重播策略
│   │   ├── skirmish/               遭遇战菜单、软件窗口接入与原生缓冲
│   │   └── sidebar/                侧栏分页与滚轮控制
│   ├── audio/                      音频播放、空间音效与世界音频
│   │   └── menu/                   菜单音乐设备与私有 Windows 实现
│   ├── network/                    联机协议、同步、会话与互联网服务
│   │   ├── lobby/                  大厅状态与地图传输
│   │   └── snapshots/              快照传输、暂存与会话提交
│   └── analysis/                   规则分析、命令行工具与报告
│       ├── reports/                能力报告与证据来源说明
│       └── sandbox/                离线模拟与回放分析
├── tests/                          按领域分组的专项测试
│   ├── fixtures/                   自编规则、地图、回放与文本预期夹具
│   │   └── offline_report_evidence/  离线报告的合成事件与规则
│   ├── assets/                     资源解码与视频服务合同测试
│   ├── infrastructure/             配置、文件、输入、窗口与菜单合同
│   │   ├── task_executor/          有界任务执行器独立合同工程
│   │   ├── main_back_buffer/       主窗口后台缓冲合同目标
│   │   ├── frame_presenter/        Windows 软件帧提交合同目标
│   │   └── d3d11_frame_presenter/  完整帧、菜单、地图与原生覆盖合同目标
│   ├── gameplay/                   战斗、生产、单位行为与机制检查
│   ├── multiplayer/                局域网、同步、快照与恢复检查
│   ├── internet/                   互联网服务、TLS 与匹配检查
│   ├── render/                     巨炮光栅内核独立合同工程
│   ├── rendering/                  绘制、深度、光照与表现检查
│   │   ├── auxiliary_animation_contract/  辅助动画的纯计算目标
│   │   └── software_frame/         完整软件帧公共合同目标
│   ├── pathfinding/                寻路与请求队列检查
│   │   └── candidate_task/         候选任务与完整队列对照目标
│   ├── rules/                      规则解析与生产规则检查
│   ├── maps/                       地图解析、地图包与随机地图检查
│   ├── campaign/                   战役、进度与影片解析检查
│   └── audio/                      音频解码、混音与反馈检查
├── tools/                          构建与开发工具源码
│   ├── Build-Game.ps1              默认 Clang 构建，兼容显式 MSVC 参考
│   ├── testing/                    测试清单、分层执行与真实窗口合同
│   ├── dependencies/               可选 SDL3 源码锁文件与本机构建脚本
│   ├── structure/                  结构检查器、精确基线与合成夹具
│   ├── toolchain/                  Clang 配置、构建、依赖与部署工具
│   │   ├── Build-WindowsClang.ps1   Clang 产品构建与部署
│   │   ├── ClangBuildSupport.ps1    缓存、产物和运行库校验
│   │   └── Configure-WindowsClang.ps1  显式工具链配置
│   ├── assets/                     资源调试与模型调整工具
│   ├── maps/                       自编地图生成工具
│   ├── benchmarks/                 移动、规模与重复性能采样工具
│   ├── diagnostics/                独立设备能力与计时诊断
│   ├── playability/                可玩性检查与结果比较工具
│   └── legacy/                     历史源码集成与迁移工具
├── samples/                        精简规则、地图与网络配置示例
│   ├── mod-extensions/             MOD 扩展示例
│   └── screen-shake/               屏幕震动示例
├── cmake/                          游戏、工具、平台与测试构建配置
│   ├── TaskExecutorContracts.cmake  执行器生产库与合同工程接入
│   ├── GrandCannonPrewarmContracts.cmake  巨炮光栅与预热库接入
│   ├── PathGoalCandidateTaskContracts.cmake  候选任务库与测试接入
│   ├── SoftwareFrameContracts.cmake  完整软件帧与呈现适配接入
│   ├── D3D11FramePresenter.cmake   私有 D3D11、离线着色器与原生合同
│   ├── Sdl3Window.cmake             可选 SDL3 软件窗口接入，默认关闭
│   ├── RunSecondWindowNative.cmake  真实菜单窗口合同执行入口
│   ├── dependencies/               第三方依赖的工具链配置
│   ├── CoreTargets.cmake           独立核心的实际目标与生产库
│   ├── CoreTestSuite.cmake         核心测试集合与串行检查入口
│   ├── ProfiledLeaf.cmake          按消费者编译约定建立静态库
│   └── toolchains/                 Windows clang-cl 工具链配置
├── CMakeLists.txt                  构建入口，按序包含 cmake 配置
├── README.md                       工程、目录、构建与资源说明
├── API.md                          主要源码接口与命令行说明
├── LICENSE                         GPL 第 3 版许可全文
├── .gitattributes                  文本格式与换行约定
├── .gitignore                      本机数据与构建产物排除规则
├── 编译.cmd                        调用默认 Clang 构建入口并转发参数
└── 启动.cmd                        使用本机数据启动游戏
```

`tests/fixtures/` 和 `samples/` 中的 INI、MAP 是自编测试输入，用于验证格式、规则和地图逻辑；它们不是完整原版游戏数据包，不能替代运行素材。

源码包不包含 `RedAlert2/`、`local_data/`、`resources/`、原项目 Git 历史、历史备份、截图或已编译程序。

`tests/fixtures/` 还保留自编采矿保存输入、文本预期结果（`.expected`）和经济生产固定步夹具（`.txt`），用于按完整字节比较行为。它们是测试所需的数据，不是玩家存档或私人运行记录。

## 编译

日常入口默认使用 Windows `clang-cl + Ninja`，串行构建 Release 的 x64 游戏核心 `concord_runtime` 和独立 x86 视频助手 `BinkFrameExtractor`，随后部署到 `bin/`。无需完整 Visual Studio IDE；仍需 Microsoft ABI、标准库、CRT 和 Windows SDK。核心编译不需要 Python 或官方游戏素材。

需要：

- Windows x64，Windows PowerShell 5.1；本地测试入口另需 PowerShell 7。
- LLVM，包含 `clang-cl.exe`、`lld-link.exe`、`llvm-lib.exe` 和 `llvm-readobj.exe`。
- CMake 3.20 或更新版本、Ninja；测试使用同套 CMake 中的 CTest。
- Microsoft C++ 的 x64/x86 头文件与库、x64 CRT 分发文件，以及 Windows SDK 的头文件、库和 RC/MT 工具；完整 Windows 产品构建还需要该 SDK 的 x64 `fxc.exe`，用于离线编译菜单与地图着色器。

已验收的组合为 LLVM 22.1.3、CMake 4.3.1、Ninja 1.13.2、Microsoft 头库/CRT 14.51.36231、ABI 兼容版本 19.51.36257、Windows SDK 10.0.26100.0；测试使用 PowerShell 7.6.5。这些工具和运行库不包含在源码包中。

### 第一次构建

先准备组件，再从仓库根目录执行。新构建目录必须显式提供 Microsoft 头库根、SDK 根和版本、ABI 兼容版本；CRT 分发根可指定，也可从所选 Microsoft 组件发现。下面路径是中性示例，请按自己已安装的组件修改；版本值对应上述已验收组合：

```powershell
$toolRoot = 'C:/Toolchains'
$cmakeExe = "$toolRoot/CMake/bin/cmake.exe"
$ninjaExe = "$toolRoot/Ninja/ninja.exe"
$llvmRoot = "$toolRoot/LLVM"
$msvcRoot = "$toolRoot/BuildTools/VC/Tools/MSVC/14.51.36231"
$sdkRoot = "$toolRoot/WindowsSDK"
$sdkVersion = '10.0.26100.0'
$compatVersion = '19.51.36257'
$crtRoot = "$toolRoot/BuildTools/VC/Redist/MSVC/14.51.36231/x64/Microsoft.VC145.CRT"
$winPs = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'

& $winPs -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Game.ps1 `
  -BuildDirectory build/clang-cl-x64-release `
  -CMakeExecutable $cmakeExe -NinjaExecutable $ninjaExe `
  -LLVMRoot $llvmRoot -MSVCRoot $msvcRoot `
  -WindowsSDKRoot $sdkRoot -WindowsSDKVersion $sdkVersion `
  -MSVCCompatibilityVersion $compatVersion -CRTRoot $crtRoot
if ($LASTEXITCODE -ne 0) { throw '构建失败，请检查本次错误信息。' }
```

CMake、Ninja、LLVM 可通过完整路径指定，或由脚本从普通终端发现。脚本不自动安装工具，不修改持久 PATH，缺失或冲突时不会回退到 MSVC。已有缓存固定源码目录、组件、架构与配置；请勿复制他机缓存，也不要在原目录中切换工具链。

完整 Windows 产品构建通过 [D3D11FramePresenter.cmake](cmake/D3D11FramePresenter.cmake) 从选定 SDK 查找 `fxc.exe`，将 [MenuRaster.hlsl](src/runtime/presentation/win32/MenuRaster.hlsl) 与 [MapRaster.hlsl](src/runtime/presentation/win32/MapRaster.hlsl) 离线编译为 Shader Model 5 顶点／像素字节码，并在构建目录生成头文件。脚本不会安装工具；找不到时会明确报错，可在 CMake 配置中用 `CONCORD_FXC_EXECUTABLE` 指定已有编译器的完整路径。生成头文件、字节码和编译器不包含在源码包中，运行时无需部署着色器编译器 DLL；独立核心构建不包含这些图形后端。

### 日常构建与部署

在默认 `build/clang-cl-x64-release/` 首次构建成功后，可双击 `编译.cmd`，或执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Game.ps1
```

若使用其他构建目录，继续传 `-BuildDirectory` 选择该目录。日常产品构建保持 Release；Debug 测试使用单独配置的目录。Ninja 是单配置生成器，切换配置不能复用同一缓存。

`Build-Game.ps1` 调用 [Build-WindowsClang.ps1](tools/toolchain/Build-WindowsClang.ps1)，先在独立目录构建核心与视频助手，再校验架构、导入依赖、源码和文件 SHA-256，从指定 CRT 分发源准备运行库。部署会保留旧程序组备份，并在失败时恢复已有程序组；`bin/build-provenance.json` 记录来源与构建参数。

`-ValidateOnly` 只检查已有配置，不配置、构建或部署；`-StageOnly` 会构建并准备隔离产物，完成校验后不部署到 `bin/`。构建记录、阶段产物和程序均留在本机，不属于源码发布内容。

`bin/` 内的运行库来自选定的 Microsoft CRT 分发源。视频桥接源码不包含 `Binkw32.dll` 或影片数据；播放视频时由使用者自行提供本地游戏所需文件。

### 可选 SDL3 遭遇战菜单窗口

普通构建默认关闭 `CONCORD_ENABLE_SDL3_WINDOW`，无需 SDL3。要启用此窗口，先完成上面的 Clang Release 首次构建，再按 [依赖锁文件](tools/dependencies/sdl3.lock.json) 中的官方地址、版本和 SHA-256 自行准备未经修改的 SDL3 3.4.16 ZIP 与解压目录：

```text
build/dependencies/sdl3-3.4.16/SDL3-3.4.16.zip
build/dependencies/sdl3-3.4.16/SDL3-3.4.16/
```

[Build-Sdl3.ps1](tools/dependencies/Build-Sdl3.ps1) 复用已选游戏工具链，先核对压缩包和解压文件，再从源码构建依赖；它不下载或安装依赖。沿用首次构建中的工具路径变量，从仓库根目录执行：

```powershell
$gameBuild = 'build/clang-cl-x64-release'
& $winPs -NoProfile -ExecutionPolicy Bypass -File .\tools\dependencies\Build-Sdl3.ps1 -GameBuildDirectory $gameBuild
if ($LASTEXITCODE -ne 0) { throw 'SDL3 依赖构建失败。' }

& $cmakeExe -S . -B $gameBuild -DCONCORD_ENABLE_SDL3_WINDOW=ON
if ($LASTEXITCODE -ne 0) { throw 'SDL3 窗口配置失败。' }

& $winPs -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Game.ps1 -BuildDirectory $gameBuild
if ($LASTEXITCODE -ne 0) { throw '游戏构建或部署失败。' }
```

使用自定义依赖源码目录时，必须位于工程内，同时向依赖脚本传入 `-SourceDirectory`，并将 CMake 的 `CONCORD_SDL3_SOURCE_ROOT` 设为同一目录；ZIP 仍需位于该源码目录的父目录。游戏部署会校验并携带 `SDL3.dll`、`SDL3-LICENSE.txt` 和 `SDL3-NOTICES.txt`。运行时只从程序所在目录加载匹配版本的 DLL。

准备本地游戏资源后，从仓库根目录选择窗口：

```powershell
.\bin\concord_runtime.exe --skirmish-window sdl3
```

`--skirmish-window win32` 为默认选择。SDL3 入口仅接管独立的位图遭遇战菜单；确认后由既有配置映射进入 Win32 战局，取消则退出。未启用依赖或 DLL 不可用时会明确报错。菜单音频仍使用现有 Windows 设备实现，当前窗口不提供系统字体绘制、D3D、SDL Renderer、嵌入式父窗口或原生随机地图编辑器。

此仓库只包含本项目的适配源码、锁文件和构建脚本；上述第三方源码、压缩包、DLL 与本机构建记录均不随源码包分发。

### 独立核心构建

`CONCORD_PORTABLE_CORE=ON` 只创建所选生产模块及对应测试，复用完整产品使用的源码和静态库；不生成 `concord_runtime`、视频助手或菜单设备目标。配置入口使用 `-PortableCore`。核心与完整产品、Release 与 Debug 必须分别使用独立构建目录，脚本会拒绝混用缓存。

准备好“第一次构建”示例中的工具路径变量后，从仓库根目录执行：

```powershell
$coreBuild = 'build/clang-cl-x64-core-release'
& $winPs -NoProfile -ExecutionPolicy Bypass -File .\tools\toolchain\Configure-WindowsClang.ps1 `
  -PortableCore -Architecture x64 -Configuration Release -BuildDirectory $coreBuild `
  -CMakeExecutable $cmakeExe -NinjaExecutable $ninjaExe `
  -LLVMRoot $llvmRoot -MSVCRoot $msvcRoot `
  -WindowsSDKRoot $sdkRoot -WindowsSDKVersion $sdkVersion `
  -MSVCCompatibilityVersion $compatVersion
if ($LASTEXITCODE -ne 0) { throw '核心配置失败，请检查错误信息。' }

& $cmakeExe --build $coreBuild --target concord_core_check --parallel 1
if ($LASTEXITCODE -ne 0) { throw '核心构建或测试失败，请检查错误信息。' }
```

Debug 使用 `-Configuration Debug` 和另一个目录，例如 `build/clang-cl-x64-core-debug`。`concord_core_check` 会构建所需目标并串行执行 10 项测试；缺失测试、禁用测试和跳过条件不被接受。目标与测试集合见 [CoreTargets.cmake](cmake/CoreTargets.cmake)、[CoreTestSuite.cmake](cmake/CoreTestSuite.cmake)。

当前核心覆盖世界确定性哈希、地图、游戏命令、比赛设置、快照传输、兼容指纹、锁步、原子文件和 LAN 传输，其中新增世界哈希入口链接实际运行时编译约定的世界库。Windows 核心默认只链接 Kernel32，LAN 夹具另私有链接 Winsock；原子文件和 LAN 实现仍有私有系统分支。这一构建不依赖官方游戏素材，其通过结果只覆盖列出的核心范围。

### 显式 MSVC 参考构建

已安装相应 Microsoft C++ 构建工具和 Windows SDK 时，可使用独立的参考目录：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Game.ps1 -Toolchain msvc -BuildDirectory build/msvc-reference
```

参考入口发现已有 Visual Studio 2022/2026 系列的 C++ 工具或 Build Tools，使用匹配的 Visual Studio 生成器和独立 Win32 助手子构建。CMake 版本须支持所选生成器；MSVC 和 Clang 的缓存与产物分开保存。

只需配置特定测试目录时，可使用 [Configure-WindowsClang.ps1](tools/toolchain/Configure-WindowsClang.ps1)；工具链声明见 [windows-clang-cl.cmake](cmake/toolchains/windows-clang-cl.cmake)。配置入口同样要求明确组件路径和版本。

## 准备本地资源并运行

请使用自己有权使用的本地 RA2/YR 游戏文件。完整运行通常需要原游戏 MIX 文件，以及本地准备的规则、地图和资源导出目录，例如：

```text
你的私人数据包/
├── RedAlert2/                    原游戏数据，可包含 Game/、GameMD/
└── local_data/
    ├── runtime_config.ini       本机运行配置
    ├── rulesmd.ini              本地游戏规则
    ├── artmd.ini                本地美术规则
    └── resources/
        └── mix_exports/         已准备的 MIX 资源导出目录
```

当前部分菜单与渲染入口使用 `local_data/resources/mix_exports/` 中的导出资源。本仓库没有一键完整素材导入功能；仅指定一个原版安装目录，不能保证所有菜单、地图和图像可以直接运行。

已有私人开发包的用户，可让新编译的程序读取自己的资源目录。从仓库根目录执行，并将下面的示例路径替换成实际路径：

```powershell
.\bin\concord_runtime.exe `
  --config "D:\MyPrivatePack\local_data\runtime_config.ini" `
  --resource-root "D:\MyPrivatePack\RedAlert2" `
  --local-data-root "D:\MyPrivatePack\local_data"
```

如果旧配置中的默认地图使用了另一台电脑的绝对路径，可额外传入 `--map "你的本机地图路径"`。命令行参数会覆盖配置中的对应设置。

也可参考 `samples/runtime_config.example.ini` 创建自己的配置。相对路径按启动工作目录及引擎识别的工程根解析，并非按 INI 所在目录解析；外部私人资源建议使用绝对路径。

若把私人资源放在仓库根的 `RedAlert2/` 和 `local_data/` 下，可使用 `启动.cmd`。这些目录已被 `.gitignore` 排除，仍应在上传前核对实际文件列表。

查看运行参数：

```powershell
.\bin\concord_runtime.exe --help
```

### 主战局呈现后端

完整 Windows 产品构建包含私有 D3D11 呈现器，使用已选 Windows SDK 的 D3D11、DXGI 和 DWM 接口，构建时一并离线编译菜单与地图着色器。这里的完整 CPU 帧上传路径仍不使用着色器；地图 GPU 绘制由独立选项控制。独立核心构建不包含这些后端，也无需 SDL3。准备本地游戏资源后，在仓库根目录显式选择：

```powershell
.\bin\concord_runtime.exe --frame-backend software
.\bin\concord_runtime.exe --frame-backend d3d11
.\bin\concord_runtime.exe --frame-backend d3d11-warp
```

| 参数值 | 含义 |
| --- | --- |
| `software` | 默认软件／GDI 呈现 |
| `d3d11` | 请求硬件 D3D11，最低功能级别 11_0，并检查格式与计时查询能力 |
| `d3d11-warp` | 明确请求 WARP 软件设备，用于兼容性和对照，不代表硬件 GPU 性能 |

这些选项只影响本机显示，不写入游戏配置、存档、协议或确定性哈希。D3D11 使用已完成的 BGRA8 CPU 帧，按当前客户区尺寸上传并以 `Present(1, 0)` 呈现；不改变原 CPU 缩放、输入坐标或世界绘制，也不进行着色器绘图、颜色转换或 alpha 混合。

设备或呈现失败时会记录原因，释放 GPU 资源并转回原 GDI；同一窗口会话中不反复尝试切回 D3D11，也不会自动改用 WARP。`--skirmish-window sdl3` 只能与 `software` 组合，选择 D3D11 或 WARP 时明确拒绝。

性能记录分别反映 CPU、GPU 上传及提交等待，不能据此声称整体加速或屏幕实际输出帧率。主战局字体组成仍有 Windows 私有实现，当前选项不表示整个游戏已跨平台。

### 遭遇战菜单 GPU 绘制

`--skirmish-drawing` 独立控制 Win32 遭遇战大厅和选图页的绘制方式，默认 `software`。完成构建并准备本地资源后，可显式选择：

```powershell
.\bin\concord_runtime.exe --skirmish-drawing d3d11
.\bin\concord_runtime.exe --skirmish-drawing d3d11-warp
.\bin\concord_runtime.exe --skirmish-drawing software
```

该路径让 GPU 直接执行有序绘制命令，包括索引图像、调色板、裁剪、整数混色、位图字形和最终客户区映射；复用现有 SHP／PAL 解码、菜单控制器及输入处理。正常呈现不先合成完整 CPU 菜单画面，也不读回 GPU 结果再合成。

硬件与 WARP 请求分开，WARP 仍是软件设备。切入随机地图编辑等未支持页面或发生后端故障时，释放 GPU 资源并在当前窗口会话中回到软件路径。SDL3 窗口仅支持软件菜单，与 GPU 菜单选项组合时明确拒绝。

`--frame-backend` 继续单独控制战局完整 CPU 帧如何显示，两组选项互不代替；启用菜单 GPU 绘制不改变世界模拟、资源优先级或战局绘图。所选菜单保留原像素与输入行为，测量结果只覆盖该菜单场景。

### 地图 GPU 绘制

`--map-drawing` 独立选择 Win32 战局地图的绘制方式，默认 `software`。完成构建并准备本地游戏资源后，从仓库根目录运行：

```powershell
.\bin\concord_runtime.exe --map-drawing software
.\bin\concord_runtime.exe --map-drawing d3d11
.\bin\concord_runtime.exe --map-drawing d3d11-warp
```

GPU 从自有 TMP／SHP／VXL 索引、调色板与原光照表执行颜色计算和有序合成，覆盖地形、物体、阴影、光照与迷雾。CPU 仍负责原解码、投影、索引深度、命中测试、地形查询、视野和模拟，不以硬件深度缓冲替代原遮挡规则。当前 VXL 范围为原 Reference／Point 模式，不表示 GPU 三维投影或其他绘制模式已经接入。

该路径复用窗口所属线程的 D3D11 呈现器，正常情况下不需要完整 CPU 地图图像或逐帧 GPU 图像读回；原生文字与界面覆盖通过私有适配组成绘制命令。设备或分配失败时，保留同帧转回原软件绘制的处理。

`--frame-backend` 控制完整 CPU 帧的呈现，`--skirmish-drawing` 控制菜单绘制，`--map-drawing` 控制地图绘制。显式同时启用 D3D11 完整帧呈现与地图绘制时，两者必须选择相同设备；硬件与 WARP 混用会报错。SDL3 菜单与任一 D3D11／WARP 选项组合也会被拒绝。

硬件和 WARP 的测量结果分别记录；GPU 时间包含融合地图合成过程，不能逐层归因，也不能从 Present 调用或窗口提交间隔推定屏幕帧率。当前对照未证明整体加速，软件默认保持不变。

## 联机与 MOD

局域网和互联网相关实现包含在源码中。互联网联机需要自行部署、配置本项目的服务端、证书和客户端凭据；它不代表兼容或接入原版 Westwood Online 服务。公开配置示例中的域名和凭据均为占位符。

示例 MOD 需要本机已有规则和素材。支持范围以源码实现为准，不保证任意第三方 MOD 可直接运行。

## 测试与开发

根目录 `CMakeLists.txt` 按顺序包含 `cmake/` 下的配置文件，它们共享根目录作用域；整理文件夹后仍使用 `tools/Build-Game.ps1` 作为普通构建入口。

完成对应构建目录的配置后，使用 PowerShell 7 运行明确的本地测试集合。默认 Clang 构建目录示例：

```powershell
pwsh -NoProfile -File .\tools\testing\Run-LocalTests.ps1 -Compiler Clang -BuildDirectory build/clang-cl-x64-release -Configuration Release -Profile windows-minimal
```

`windows-minimal` 包含 12 项不需要原游戏素材的检查；`windows-local-assets` 包含 4 项需要本机规则素材的检查。入口按显式选择的编译器校验缓存，再串行构建所选目标并精确执行对应测试。使用 MSVC 参考目录时传 `-Compiler MSVC -BuildDirectory build/msvc-reference`。测试入口的 `-Compiler` 缺省仍为 MSVC，因此 Clang 检查必须显式传 `-Compiler Clang`；这些集合不代表全部游戏功能或其他平台的验收。

结果默认保存在 `build/local-tests/` 的独立目录中。`results.json` 分别记录 `passed`、`failed`、`skipped`、`unsupported`、`not-built` 和 `not-run`，跳过、未构建或未执行不能计为通过。清单与条件见 [test-profiles.json](tools/testing/test-profiles.json)。

角色、计分、解码、预览会话、回放类型，以及新增输入、UI 时钟与地形查询检查，按具体 CMake 目标和 CTest 名称独立注册，未自动加入上述 12＋4 集合。标准合同和部分桥接检查见 [TestsUnitAndNetwork.cmake](cmake/TestsUnitAndNetwork.cmake)，地形与资源运行检查见 [TestsRuntimeRegression.cmake](cmake/TestsRuntimeRegression.cmake)，指针、光标与取消捕获检查见 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake)。

经济与传送标准目标定义见 [RuntimeSmokeTargets.cmake](cmake/RuntimeSmokeTargets.cmake)。传送提交和户级经济上下文的测试注册在 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake)，采矿编解码及经济生产固定步注册在 [TestsUnitAndNetwork.cmake](cmake/TestsUnitAndNetwork.cmake)。这些目标读取源码包内的自编夹具，仍按独立目标运行，不计入固定的 12＋4 集合。

源码还保留 [定时炸弹模块测试](tests/gameplay/RuntimeTimedBombModuleSmoke.cpp)、[定时炸弹兼容检查](tests/gameplay/RuntimeTimedBombCompatibilitySmoke.cpp)、[寻路候选独立测试](tests/pathfinding/PathGoalCandidatesIndependent.cpp)、[生产规则合同测试](tests/rules/RulesProductionLoaderContract.cpp) 和 [地形帧测试](tests/rendering/RuntimeMapTerrainFrameSmoke.cpp)。这些新增测试源码在开发环境单独构建验证，尚未注册为当前 CMake/CTest 目标，也未加入固定的 12＋4 集合；公开包不包含私人验证脚本、旧版本对照副本或运行记录。

视频服务、回放封包与恢复一致性检查分别以 `bink_playback_service_contract_test`、`match_replay_envelope_contract_test` 和 `runtime_recovery_consistency_contract_test` 注册在 [TestsUnitAndNetwork.cmake](cmake/TestsUnitAndNetwork.cmake)，LAN 地图传输检查 `runtime_lobby_map_transfer_contract_test` 注册在 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake)。它们仍独立于固定的 12＋4 集合。视频服务的无素材合同只验证服务边界和缺失输入处理，不能代替实际影片播放验收。

窗口生命周期、菜单控制器、颜色值、图像、字体、CPU 像素表面、音乐策略及其 Windows 适配，以及 TCP 边界和快照会话，均有单独的合同目标与测试注册。相关入口见 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake) 和各模块的 `*Contracts.cmake`；这些检查没有自动并入固定的 12＋4 集合。独立核心的 10 项测试通过 `concord_core_check` 执行，使用上文单独的核心构建目录。

辅助动画的 [自编合同测试](tests/rendering/RuntimeAuxiliaryAnimationContract.cpp) 以目标 `concord_auxiliary_animation_contract` 构建，并注册为 `runtime_auxiliary_animation_contract`，不需要官方素材。它检查帧范围、往返循环、速率、缺省输入及整数边界；SHP 边界输入继续由 `shp_codec_contract_test` 使用 [ShpCodecCorpus.h](tests/assets/ShpCodecCorpus.h) 检查。这些测试未加入固定的 12＋4 集合。

有界执行器和巨炮光栅内核分别注册 `bounded_task_executor_contract_test`、`grand_cannon_raster_contract_test`，均使用自编输入，不需要官方素材。在已配置的完整产品构建目录中，可单独构建并运行：

```powershell
$gameBuild = 'build/clang-cl-x64-release'
& $cmakeExe --build $gameBuild --target concord_bounded_task_executor_contract concord_grand_cannon_raster_contract --parallel 1
if ($LASTEXITCODE -ne 0) { throw '合同目标构建失败。' }
& "$toolRoot/CMake/bin/ctest.exe" --test-dir $gameBuild --no-tests=error --output-on-failure -j 1 -R '^(bounded_task_executor_contract_test|grand_cannon_raster_contract_test)$'
if ($LASTEXITCODE -ne 0) { throw '合同检查失败。' }
```

两份 [执行器合同工程](tests/infrastructure/task_executor/CMakeLists.txt) 和 [光栅合同工程](tests/render/grand_cannon_raster/CMakeLists.txt) 也支持作为独立 CMake 源工程，直接编译相同生产实现。它们不属于上文独立核心的固定 10 项集合，也未并入 12＋4 本地测试集合。

素材合同 `runtime_grand_cannon_prewarm_contract_test` 只在 Windows 且工程中存在 `RedAlert2/` 时注册，还需要本机规则、资源及配置指定的地图。它比较原预热结果与多种工作数的完整输出；无素材的纯内核合同不能替代这项检查。

候选任务和软件帧新增以下 CTest 注册，入口见 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake)：

| 测试名 | 范围 |
| --- | --- |
| `path_goal_candidate_task_contract_test` | 自有输入、结果身份、取消、异常与同步回退 |
| `path_goal_candidate_queue_differential_test` | 完整队列对照，默认使用 4 个工作线程 |
| `main_back_buffer_win32_contract_test` | Windows 主缓冲所有权、写入代际与释放 |
| `software_frame_contract_test` | 完整帧元数据、像素借用与提交计数 |
| `frame_presenter_win32_contract_test` | Windows 完整帧提交、字体组成与失败处理 |

这些测试不需要官方素材，但 Windows 适配合同仍需要相应系统能力；它们按完整产品配置中的独立目标执行，没有自动加入 12＋4 本地集合或独立核心的固定 10 项集合。[寻路结果对照](tests/pathfinding/PathGoalCandidateDifferential.cpp) 已接入上述队列测试。

[离线报告检查脚本](tools/testing/Test-OfflineReportEvidence.ps1) 使用 [自编规则与事件](tests/fixtures/offline_report_evidence)，核对报告来源、空输入、部分输入、完整字段与错误处理；生成的报告不等同于真实游戏行为通过。

结构检查入口见 [Run-StructureChecks.ps1](tools/testing/Run-StructureChecks.ps1)。它需要本工程已校验的 Clang/Ninja Release 构建目录、PowerShell 7，以及 Python 3.10 或更新版本的绝对路径。先按自己的安装位置修改示例解释器路径：

```powershell
pwsh -NoProfile -File .\tools\testing\Run-LocalTests.ps1 -Compiler Clang -BuildDirectory build/clang-cl-x64-release -Configuration Release -StructureOnly -PythonExecutable 'C:/Toolchains/Python/python.exe'
```

`-StructureOnly` 单独执行结构检查，不追加执行 12＋4 测试集合。入口校验缓存、刷新 CMake 元数据、冻结目标清单并运行合成夹具，然后检查依赖与精确例外；它不会构建游戏目标。结果默认写入已忽略的 `build/local-tests/structure-*/`，其中可能包含本机路径和环境信息，应留在本机。

[结构基线](tools/structure/structure-baseline.json) 保留 4,660 条逐项审核的精确例外。新增违规、出现次数增加或已经消失而尚未收紧的例外都会导致失败；工具不会自动扩大豁免范围。它提供所选构建配置的静态结构证据，不表示完整游戏、其他平台或性能已经验收。

已记录的验证覆盖所选标准合同、Windows 适配、资源查询、命令桥接及经济保存恢复对照。旧回归中已知的月球剧场深度、部分交互和几何绘制，以及 Yuri 采矿收入、完整 AI 经济、部分编队紧凑度与矿石动画检查失败仍保留；完整物理输入、IME、双端运行游戏，以及未覆盖的窗口、平台和性能场景不据此视为通过。

CMake 保留专项测试目标。部分测试只使用自编夹具，部分需要本地原游戏数据；资源相关测试与调试工具需要按自己的环境配置路径。不能将无素材环境下的编译成功视为全部游戏功能或全部测试通过。

普通构建只编译游戏核心与视频助手，避免一次构建全部历史测试目标。`samples/` 内的演示准备脚本与部分开发工具需要先具备相应本地数据；Python 工具的解释器和依赖应自行准备。

### 可选菜单窗口合同

`runtime_second_window_contract_test` 在 Windows、`CONCORD_ENABLE_SDL3_WINDOW=ON` 且工程内存在 `local_data/resources/mix_exports/multimd/2peaks.map` 时注册。它需要自行准备的完整游戏资源和匹配的 SDL3 DLL，会实际创建窗口，不属于无素材测试集合。

执行入口为 [RunSecondWindowNative.cmake](cmake/RunSecondWindowNative.cmake)，另提供 [Run-SecondWindowContract.ps1](tools/testing/Run-SecondWindowContract.ps1) 用于指定运行程序和新的工程内输出目录。检查覆盖两种窗口的所选菜单画面与控制器状态、真实事件、像素坐标、捕获、缩放、关闭及资源释放；输出记录保存在本机构建目录中。它只验证所选菜单场景，不代表主战局已使用 SDL3 或所有游戏检查均已通过。

### D3D11 原生呈现合同

[D3D11FramePresenterWin32Contract.cpp](tests/infrastructure/D3D11FramePresenterWin32Contract.cpp) 使用自编像素输入，覆盖尺寸与步长、完整颜色、所属线程、窗口生命周期、失败清理和原 GDI 回退。它不需要官方游戏素材，但会实际创建窗口并检查硬件及 WARP，需要相应 Windows 图形环境；不是无窗口测试。

先完成完整 Windows 构建并设置“第一次构建”中的工具路径变量，再执行：

```powershell
$gameBuild = 'build/clang-cl-x64-release'
$ctestExe = Join-Path (Split-Path -Parent $cmakeExe) 'ctest.exe'
& $cmakeExe --build $gameBuild --target concord_d3d11_frame_presenter_win32_contract --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'D3D11 合同构建失败。' }
& $ctestExe --test-dir $gameBuild --no-tests=error --output-on-failure -j 1 -R '^d3d11_frame_presenter_win32_contract_test$'
if ($LASTEXITCODE -ne 0) { throw 'D3D11 合同检查失败。' }
```

已完成的开发验证还包括主窗口软件、硬件和 WARP 的所选完整帧对照。设备移除场景使用明确故障注入，未据此声称真实驱动重置、设备热拔出、跨显示器 DPI／HDR 或长期设备恢复均已验证。既有游戏表现问题仍按原记录保留。

### GPU 菜单光栅合同

[MenuRasterD3D11Contract.cpp](tests/infrastructure/MenuRasterD3D11Contract.cpp) 使用自编像素、索引和字形输入，覆盖裁剪、命令顺序、透明索引、remap、混色、缩放、资源代际、错线程与重复提交，以及正常路径没有诊断读回。它不需要官方素材，但需要可创建真实窗口的 Windows 图形环境，并分别检查硬件和 WARP。

沿用完整产品构建的工具路径变量：

```powershell
$gameBuild = 'build/clang-cl-x64-release'
$ctestExe = Join-Path (Split-Path -Parent $cmakeExe) 'ctest.exe'
& $cmakeExe --build $gameBuild --target concord_menu_raster_d3d11_contract --parallel 1
if ($LASTEXITCODE -ne 0) { throw '菜单光栅合同构建失败。' }
& $ctestExe --test-dir $gameBuild --no-tests=error --output-on-failure -j 1 -R '^menu_raster_d3d11_contract_test$'
if ($LASTEXITCODE -ne 0) { throw '菜单光栅合同检查失败。' }
```

正式菜单的 [完整画面对照](src/ui/skirmish/diagnostics/RuntimeGpuMenuContract.inl) 与 [计时入口](src/ui/skirmish/diagnostics/RuntimeGpuMenuTiming.inl) 也保留源码；它们需要自行准备的游戏资源和交互桌面。开发验证覆盖了完整菜单、所选真实输入及预热后的重复资源创建与关闭，不将此结果扩大到全部游戏测试、长期设备恢复或其他平台。

### GPU 地图与原生覆盖合同

[MapRasterD3D11Contract.cpp](tests/infrastructure/MapRasterD3D11Contract.cpp) 与 [MapNativeOverlayD3D11Contract.cpp](tests/infrastructure/MapNativeOverlayD3D11Contract.cpp) 保留自编像素、材质、命令与原生覆盖输入，检查颜色、顺序、资源寿命、错误输入和失败回退。它们不需要官方素材，但需要 Windows 交互桌面及可用的图形设备。

沿用前文构建工具路径变量，可单独构建和检查：

```powershell
$gameBuild = 'build/clang-cl-x64-release'
$ctestExe = Join-Path (Split-Path -Parent $cmakeExe) 'ctest.exe'
& $cmakeExe --build $gameBuild --target concord_map_raster_d3d11_contract concord_map_native_overlay_d3d11_contract --parallel 1
if ($LASTEXITCODE -ne 0) { throw '地图 GPU 合同构建失败。' }
& $ctestExe --test-dir $gameBuild --no-tests=error --output-on-failure -j 1 -R '^(map_raster_d3d11_contract_test|map_native_overlay_d3d11_contract_test)$'
if ($LASTEXITCODE -ne 0) { throw '地图 GPU 合同检查失败。' }
```

实际地图、物体、阴影、光照、迷雾与 VXL 的集成诊断源码保留在各绘图模块的 `diagnostics/` 下；[地图计时入口](src/runtime/performance/diagnostics/RuntimeMapGpuTiming.inl) 也随源码提供。正式场景诊断需要自行准备的游戏资源；本机验收脚本、截图、原始计时和交接记录不随源码发布。

另保留独立的 [可见窗口诊断源码](tools/diagnostics/s41/visible_observer/VisibleObserver.cpp) 及其构建工程，用于观察指定进程的窗口可见性和呈现环境；它不属于游戏日常构建，也不附带本机采样数据。

### 并行预热与性能工具

巨炮预热的专用环境变量为 `CONCORD_GRAND_CANNON_PREWARM_WORKERS`，可取 `0`、`1`、`2`、`4` 或 `auto`。`0` 在提交线程同步执行；`auto` 根据逻辑处理器数量预留两个后限制在 1–4，未知数量按 1 处理，不设置系统亲和性。普通游戏默认仍为 1，穷尽预热诊断默认仍为 4。此选项只影响这一预热消费者；分析工具的 `--workers` 含义保持原样。

[性能工具目录](tools/benchmarks) 保留重复运行、输入身份记录和原始日志汇总脚本；新增的 `.mjs` 脚本需要支持 ES 模块的 Node.js，仅使用内置模块。基线和设备诊断入口使用公开版默认 `build/clang-cl-x64-release/`，核对 Release 程序及本机部署来源，并要求新的工程内输出目录。基线运行还需要自行准备游戏数据和所选地图。

[设备诊断目录](tools/diagnostics) 是独立 Windows Clang 项目，检测设备、纹理、读回和计时能力，设备库不链接进游戏或独立核心。CPU 提交间隔、独立 GPU 探针和实际显示帧率是不同指标；当前工具没有测量游戏 GPU 帧时间或屏幕扫描输出帧率，所选阶段计时也不构成全游戏加速结论。

运行时产生的环境信息、绝对路径、日志和输入哈希属于本机记录，请将输出放在 `build/` 下并保留在本地。源码包不附带这些记录，也不附带依赖私人文档的验收和交接脚本。

### 确定性候选计算与软件帧

`CONCORD_PATH_CANDIDATE_MODE` 可设为 `reference`、`shadow` 或 `parallel`，缺省为 `reference`。`reference` 使用原同步路径；`shadow` 对照结果并继续返回原同步结果；`parallel` 在原消费点校验并采用任务结果，拒绝或失败时同步回退。工作数由 `CONCORD_PATH_CANDIDATE_WORKERS` 指定为 `0 / 1 / 2 / 4`，缺省为 1；0 在提交线程执行，不创建工作线程。

这组选项只控制初始目标候选内核，与巨炮预热选项及分析工具的 `--workers` 分开使用。原 A*、环形回退、请求顺序、缓存、随机数及逻辑预算继续由原所有者处理，不能据此推断整个寻路或模拟已经并行。

软件帧接口同步借用最终像素，支持默认 GDI 和可选 D3D11 完整帧呈现，故障时保留原 GDI 路径。每次提交有新序号；复用场景时，场景版本与模拟 tick 保持，界面叠加仍可变化。完整帧准备与最终提交共同计入当前提交 CPU 时间，不能与旧版只统计 `BitBlt` 的数值直接比较。

需要实际游戏素材的 [候选联机及生命周期诊断](src/simulation/navigation/pathfinding/diagnostics/RuntimePathCandidateNetworkSmoke.inl) 须从工程根运行，自行准备 `RedAlert2/`、`local_data/` 和指定地图。公开版将诊断记录、配置与保存路径限制在 `build/smoke/path-candidate/` 内，并要求新的输出位置；本机运行记录不随源码发布。

## 上传 GitHub

仅上传这个源码目录的文件。不要上传原私人开发包、父目录、构建验证目录或旧 `.git` 历史。

首次创建公开仓库时，先进入这个源码目录，执行 `git init -b main` 创建全新独立仓库，再提交文件、配置你自己的 GitHub 远程地址。已有公开仓库更新时，沿用其独立 `.git`，复制更新后的源码内容再提交和推送，勿复制原开发工程的 `.git`。不要直接通过父目录的现有仓库推送，以免带入旧历史。若使用 GitHub 网页上传，请先解压源码 ZIP，再上传解压后的源码文件。

本仓库的 `.gitignore` 排除了原版数据目录、游戏素材格式、个人配置、缓存、编译产物和压缩包。源码公开包中的自编 INI/MAP 测试夹具有明确例外；不要用原版文件覆盖这些示例后提交。

游戏图像、模型、调色板、音频、视频、字体、官方地图、完整原版规则文件、游戏 EXE/DLL 以及从 MIX 提取的内容，均不属于本仓库发布范围。

## 版权与源码许可

RA2/YR 的名称、商标、原版程序及素材归各自权利人所有。本项目为非官方工程，与原游戏权利人无官方关联；公开引擎源码不授予原版游戏或其素材的使用、复制或再分发许可。

本仓库中由作者编写并有权许可的引擎源码、构建与开发工具、测试代码、自编测试夹具及示例，采用 **GNU General Public License 第 3 版（仅第 3 版）**，SPDX 标识为 `GPL-3.0-only`。完整许可证见 [LICENSE](LICENSE)。

你可以依照该许可证使用、修改和再分发这些内容；再分发时须遵守 GPL 第 3 版的要求。本项目按现状提供，不附带任何保证，具体以许可证全文为准。

上述源码许可不包含原版 RA2/YR 的程序、素材或商标授权，也不改变任何第三方内容本身的权利归属。
