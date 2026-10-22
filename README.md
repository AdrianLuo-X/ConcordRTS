# ConcordRTS

ConcordRTS 是使用 C++20 开发的 RTS 引擎工程，面向《命令与征服：红色警戒 2》及《尤里的复仇》（RA2/YR）的规则、地图和资源格式。

本仓库公开作者自行编写的引擎源码、构建工具、测试代码与示例，不附带原版游戏程序或官方素材。运行所需的游戏数据由使用者自行准备。

主要 C++ 接口、配置入口和命令行工具说明见 [API 文档](API.md)。

## 功能与开发状态

源码包含遭遇战、AI、战役、局域网与互联网联机、观战、存档与联机恢复，以及规则分析、地图解析、资源读取和导出工具。

项目仍在持续开发，不保证原版所有细节、全部战役或任意第三方 MOD 均已完整兼容。

## 更新记录

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
│   │   └── results/                比赛计分、保存与哈希
│   ├── data/                       规则、美术、地图与战役数据库
│   ├── assets/                     游戏资源格式读取、检查与导出
│   │   ├── text/                   共享 CSF 文字解码
│   │   ├── codecs/                 共享 PAL、SHP、TMP 类型与解码
│   │   ├── terrain/                TMP 资源定位、缓存与元数据查询
│   │   └── preview/                地图预览、资源会话与缓存
│   ├── world/                      世界状态、对象索引与查询
│   ├── simulation/                 战斗、移动、经济、AI 与特殊机制
│   │   ├── ai/                     AI 采矿状态与保存、哈希接口
│   │   ├── economy/                户级经济上下文与收入固定步
│   │   ├── production/             生产队列与扣款事务
│   │   ├── navigation/             导航、寻路与移动查询
│   │   │   └── terrain/            导航地形类型与只读查询端口
│   │   └── superweapons/           特殊武器与传送位置提交
│   ├── runtime/                    运行时配置与游戏宿主
│   │   ├── config/                 配置类型、解析与命令行覆盖
│   │   ├── contracts/              窄范围回放事件类型
│   │   └── host/                   启动检查、游戏窗口与战局宿主
│   ├── render/                     地图、单位、效果与软件绘制
│   ├── ui/                         菜单、输入、HUD 与偏好保存
│   │   ├── input/                  输入值、键鼠、聊天与交互控制器
│   │   ├── time/                   UI 毫秒时钟与原计时策略
│   │   └── sidebar/                侧栏分页与滚轮控制
│   ├── audio/                      音频播放、空间音效与世界音频
│   ├── network/                    联机协议、同步、会话与互联网服务
│   └── analysis/                   规则分析、命令行工具与报告
├── tests/                          按领域分组的专项测试
│   ├── fixtures/                   自编规则、地图、回放与文本预期夹具
│   ├── assets/                     CSF、SHP 解码合同测试
│   ├── infrastructure/             配置、文件、输入、时钟与地形资源合同
│   ├── gameplay/                   战斗、生产、单位行为与机制检查
│   ├── multiplayer/                局域网、同步、快照与恢复检查
│   ├── internet/                   互联网服务、TLS 与匹配检查
│   ├── rendering/                  绘制、深度、光照与表现检查
│   ├── pathfinding/                寻路与请求队列检查
│   ├── rules/                      规则解析与生产规则检查
│   ├── maps/                       地图解析、地图包与随机地图检查
│   ├── campaign/                   战役、进度与影片解析检查
│   └── audio/                      音频解码、混音与反馈检查
├── tools/                          构建与开发工具源码
│   ├── Build-Game.ps1              默认 Clang 构建，兼容显式 MSVC 参考
│   ├── testing/                    测试清单、分层执行与结果汇总
│   ├── toolchain/                  Clang 配置、构建、依赖与部署工具
│   │   ├── Build-WindowsClang.ps1   Clang 产品构建与部署
│   │   ├── ClangBuildSupport.ps1    缓存、产物和运行库校验
│   │   └── Configure-WindowsClang.ps1  显式工具链配置
│   ├── assets/                     资源调试与模型调整工具
│   ├── maps/                       自编地图生成工具
│   ├── benchmarks/                 移动与规模性能检查工具
│   ├── playability/                可玩性检查与结果比较工具
│   └── legacy/                     历史源码集成与迁移工具
├── samples/                        精简规则、地图与网络配置示例
│   ├── mod-extensions/             MOD 扩展示例
│   └── screen-shake/               屏幕震动示例
├── cmake/                          游戏、工具、平台与测试构建配置
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
- Microsoft C++ 的 x64/x86 头文件与库、x64 CRT 分发文件，以及 Windows SDK 的头文件、库和 RC/MT 工具。

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

### 日常构建与部署

在默认 `build/clang-cl-x64-release/` 首次构建成功后，可双击 `编译.cmd`，或执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Game.ps1
```

若使用其他构建目录，继续传 `-BuildDirectory` 选择该目录。日常产品构建保持 Release；Debug 测试使用单独配置的目录。Ninja 是单配置生成器，切换配置不能复用同一缓存。

`Build-Game.ps1` 调用 [Build-WindowsClang.ps1](tools/toolchain/Build-WindowsClang.ps1)，先在独立目录构建核心与视频助手，再校验架构、导入依赖、源码和文件 SHA-256，从指定 CRT 分发源准备运行库。部署会保留旧程序组备份，并在失败时恢复已有程序组；`bin/build-provenance.json` 记录来源与构建参数。

`-ValidateOnly` 只检查已有配置，不配置、构建或部署；`-StageOnly` 会构建并准备隔离产物，完成校验后不部署到 `bin/`。构建记录、阶段产物和程序均留在本机，不属于源码发布内容。

`bin/` 内的运行库来自选定的 Microsoft CRT 分发源。视频桥接源码不包含 `Binkw32.dll` 或影片数据；播放视频时由使用者自行提供本地游戏所需文件。

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

## 联机与 MOD

局域网和互联网相关实现包含在源码中。互联网联机需要自行部署、配置本项目的服务端、证书和客户端凭据；它不代表兼容或接入原版 Westwood Online 服务。公开配置示例中的域名和凭据均为占位符。

示例 MOD 需要本机已有规则和素材。支持范围以源码实现为准，不保证任意 Ares、Phobos 或其他 MOD 可直接运行。

## 测试与开发

根目录 `CMakeLists.txt` 按顺序包含 `cmake/` 下的配置文件，它们共享根目录作用域；整理文件夹后仍使用 `tools/Build-Game.ps1` 作为普通构建入口。

完成对应构建目录的配置后，使用 PowerShell 7 运行明确的本地测试集合。默认 Clang 构建目录示例：

```powershell
pwsh -NoProfile -File .\tools\testing\Run-LocalTests.ps1 -Compiler Clang -BuildDirectory build/clang-cl-x64-release -Configuration Release -Profile windows-minimal
```

`windows-minimal` 包含 12 项不需要原游戏素材的检查；`windows-local-assets` 包含 4 项需要本机规则素材的检查。入口按显式选择的编译器校验缓存，再串行构建所选目标并精确执行对应测试。使用 MSVC 参考目录时传 `-Compiler MSVC -BuildDirectory build/msvc-reference`。测试入口的 `-Compiler` 缺省仍为 MSVC，因此 Clang 检查必须显式传 `-Compiler Clang`；这些集合不代表全部游戏功能或其他平台的验收。

结果默认保存在 `build/local-tests/` 的独立目录中。`results.json` 分别记录 `passed`、`failed`、`skipped`、`unsupported`、`not-built` 和 `not-run`，跳过、未构建或未执行不能计为通过。清单与条件见 [test-profiles.json](tools/testing/test-profiles.json)。

角色、计分、解码、预览会话、回放类型，以及新增输入、UI 时钟与地形查询检查，按具体 CMake 目标和 CTest 名称独立注册，未自动加入上述 12＋4 集合。标准合同和部分桥接检查见 [TestsUnitAndNetwork.cmake](cmake/TestsUnitAndNetwork.cmake)，地形与资源运行检查见 [TestsRuntimeRegression.cmake](cmake/TestsRuntimeRegression.cmake)，指针、光标与取消捕获检查见 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake)。

本次经济与传送标准目标定义见 [RuntimeSmokeTargets.cmake](cmake/RuntimeSmokeTargets.cmake)。传送提交和户级经济上下文的测试注册在 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake)，采矿编解码及经济生产固定步注册在 [TestsUnitAndNetwork.cmake](cmake/TestsUnitAndNetwork.cmake)。这些目标读取源码包内的自编夹具，仍按独立目标运行，不计入固定的 12＋4 集合。

已记录的验证覆盖所选标准合同、Windows 适配、资源查询、命令桥接及经济保存恢复对照。旧回归中已知的月球剧场深度、部分交互和几何绘制，以及 Yuri 采矿收入与完整 AI 经济检查失败仍保留；完整物理输入、IME、双端运行游戏、其他窗口后端与平台及性能不据此视为通过。

CMake 保留专项测试目标。部分测试只使用自编夹具，部分需要本地原游戏数据；资源相关测试与调试工具需要按自己的环境配置路径。不能将无素材环境下的编译成功视为全部游戏功能或全部测试通过。

普通构建只编译游戏核心与视频助手，避免一次构建全部历史测试目标。`samples/` 内的演示准备脚本与部分开发工具需要先具备相应本地数据；Python 工具的解释器和依赖应自行准备。

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
