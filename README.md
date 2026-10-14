# ConcordRTS

ConcordRTS 是使用 C++20 开发的 RTS 引擎工程，面向《命令与征服：红色警戒 2》及《尤里的复仇》（RA2/YR）的规则、地图和资源格式。

本仓库公开作者自行编写的引擎源码、构建工具、测试代码与示例，不附带原版游戏程序或官方素材。运行所需的游戏数据由使用者自行准备。

主要 C++ 接口、配置入口和命令行工具说明见 [API 文档](API.md)。

## 功能与开发状态

源码包含遭遇战、AI、战役、局域网与互联网联机、观战、存档与联机恢复，以及规则分析、地图解析、资源读取和导出工具。

项目仍在持续开发，不保证原版所有细节、全部战役或任意第三方 MOD 均已完整兼容。

## 更新记录

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
│   ├── core/                       基础工具、INI 与文件读写
│   ├── data/                       规则、美术、地图与战役数据库
│   ├── assets/                     游戏资源格式读取、检查与导出
│   ├── world/                      世界状态、对象索引与查询
│   ├── simulation/                 战斗、移动、经济、AI 与特殊机制
│   ├── runtime/                    运行时配置与游戏宿主
│   │   ├── config/                 配置类型、解析与命令行覆盖
│   │   └── host/                   启动检查、游戏窗口与战局宿主
│   ├── render/                     地图、单位、效果与软件绘制
│   ├── ui/                         菜单、输入、HUD 与偏好保存
│   ├── audio/                      音频播放、空间音效与世界音频
│   ├── network/                    联机协议、同步、会话与互联网服务
│   └── analysis/                   规则分析、命令行工具与报告
├── tests/                          按领域分组的专项测试
│   ├── fixtures/                   自编 INI、MAP 等测试输入
│   ├── infrastructure/             配置、文件、状态哈希与索引检查
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
│   ├── Build-Game.ps1              MSVC 参考构建、检查与部署入口
│   ├── testing/                    测试清单、分层执行与结果汇总
│   ├── toolchain/                  可选 clang-cl 与 Ninja 配置入口
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
├── 编译.cmd                        调用 MSVC 参考构建入口
└── 启动.cmd                        使用本机数据启动游戏
```

`tests/fixtures/` 和 `samples/` 中的 INI、MAP 是自编测试输入，用于验证格式、规则和地图逻辑；它们不是完整原版游戏数据包，不能替代运行素材。

源码包不包含 `RedAlert2/`、`local_data/`、`resources/`、原项目 Git 历史、历史备份、截图或已编译程序。

## 编译

需要：

- Windows x64。
- 已有 Visual Studio 2022 或更新系列的 C++ 构建工具、Windows SDK，以及 x64 和 Win32 工具链。完整 IDE 不是源码必需项；脚本也可发现符合要求的 Build Tools 实例。
- CMake 3.20 或更新版本。构建脚本优先使用 Visual Studio 自带的 CMake；所选 CMake 还须支持对应的 Visual Studio 生成器。

在仓库根目录双击 `编译.cmd`，或在 PowerShell 中执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Game.ps1
```

脚本在 `build/msvc-reference/` 中串行构建 Release 的 x64 游戏核心 `concord_runtime`，并通过独立 Win32 子构建生成项目自编的视频桥接工具 `BinkFrameExtractor`，随后部署到 `bin/`。核心编译不需要 Python，也不需要官方游戏素材。

已有缓存必须属于当前源码目录，并符合所选工具实例、架构、编译参数、CRT 与 ABI 约定；不一致时入口会拒绝继续。需要独立构建时可传 `-BuildDirectory build/developer`，使用新的项目内目录，避免混用不同工具链或配置。`-ValidateOnly` 可检查已有配置，不配置、编译或部署。

部署完成后，构建目录和 `bin/` 下的 `build-provenance.json` 记录工具、参数及部署文件 SHA-256。这些记录、程序与运行库均留在本机，源码包不包含它们。

`bin/` 内的运行库由本机 Visual Studio 复制，属于本机构建产物，不属于本仓库发布内容。视频桥接源码不包含 `Binkw32.dll` 或影片数据；如需视频播放，应由使用者自行提供其本地游戏所需文件。

### 可选 Clang 配置

[Configure-WindowsClang.ps1](tools/toolchain/Configure-WindowsClang.ps1) 提供 Windows `clang-cl + Ninja` 的显式配置入口，工具链声明见 [windows-clang-cl.cmake](cmake/toolchains/windows-clang-cl.cmake)。使用者需传入自己已核验的 CMake、Ninja、LLVM、Microsoft C++ 库和 Windows SDK 路径及版本；配置入口不安装工具，也不更改持久 PATH。

Clang 使用独立的构建目录，按 `Release` / `Debug` 与 x64 / x86 分开配置。x64 游戏目标依赖单独构建的 x86 视频助手；它仍使用 Microsoft ABI、标准库、CRT 和 Windows SDK。当前常规构建与 `bin/` 部署入口仍是 `Build-Game.ps1` 的 MSVC 流程。

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

完成 MSVC 参考目录的配置后，可使用 PowerShell 7 运行明确的本地测试集合：

```powershell
pwsh -NoProfile -File .\tools\testing\Run-LocalTests.ps1 -BuildDirectory build/msvc-reference -Configuration Release -Profile windows-minimal
```

`windows-minimal` 包含 12 项不需要原游戏素材的检查；`windows-local-assets` 包含 4 项需要本机规则素材的检查。入口先校验 MSVC 参考缓存，再串行构建所选目标并精确执行对应测试；它目前不用于 Clang 缓存，也不代表完整游戏或其他平台的验收。

结果默认保存在 `build/local-tests/` 的独立目录中。`results.json` 分别记录 `passed`、`failed`、`skipped`、`unsupported`、`not-built` 和 `not-run`，跳过、未构建或未执行不能计为通过。清单与条件见 [test-profiles.json](tools/testing/test-profiles.json)。

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
