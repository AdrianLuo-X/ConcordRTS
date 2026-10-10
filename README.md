# ConcordRTS

ConcordRTS 是使用 C++20 开发的 RTS 引擎工程，面向《命令与征服：红色警戒 2》及《尤里的复仇》（RA2/YR）的规则、地图和资源格式。

本仓库公开作者自行编写的引擎源码、构建工具、测试代码与示例，不附带原版游戏程序或官方素材。运行所需的游戏数据由使用者自行准备。

主要 C++ 接口、配置入口和命令行工具说明见 [API 文档](API.md)。

## 功能与开发状态

源码包含遭遇战、AI、战役、局域网与互联网联机、观战、存档与联机恢复，以及规则分析、地图解析、资源读取和导出工具。

项目仍在持续开发，不保证原版所有细节、全部战役或任意第三方 MOD 均已完整兼容。

## 仓库内容

```text
ConcordRTS/                                源码仓库根目录
├── src/                                   引擎与游戏源码
│   ├── analysis/                          离线分析、规则报告与命令行工具
│   ├── assets/                            资源读取、解码、检查与预览
│   ├── audio/                             声音播放、空间混音与声音事件
│   ├── core/                              文件读写、INI 解析与基础工具
│   ├── data/                              规则、美术、地图与战役数据库
│   ├── network/                           联机协议、锁步同步与互联网服务
│   ├── render/                            地形、单位、光照与画面特效渲染
│   ├── runtime/                           游戏宿主、计时、持久化与运行诊断
│   ├── simulation/                        AI、移动、战斗与其他玩法系统
│   ├── ui/                                菜单、输入、侧边栏与战局界面
│   └── world/                             世界对象、玩家与地形状态
├── tests/                                 专项测试代码
│   ├── audio/                             音频测试
│   ├── campaign/                          战役测试
│   ├── fixtures/                          自编规则、地图等测试输入
│   ├── gameplay/                          游戏逻辑测试
│   ├── infrastructure/                    基础设施与诊断测试
│   ├── internet/                          互联网联机测试
│   ├── maps/                              地图与随机地图测试
│   ├── multiplayer/                       多人联机测试
│   ├── pathfinding/                       寻路与导航测试
│   ├── rendering/                         渲染测试
│   └── rules/                             规则解析与校验测试
├── tools/                                 构建与开发工具源码
│   ├── assets/                            资源处理与模型调整工具
│   ├── benchmarks/                        性能检查工具
│   ├── legacy/                            历史源码集成脚本
│   ├── maps/                              地图处理工具
│   ├── playability/                       玩法功能检查工具
│   ├── BinkFrameExtractor.cpp             视频助手源码
│   ├── Build-Game.ps1                     游戏与视频助手的构建入口
│   ├── Check-SourceRepository.ps1         源码仓库文件检查
│   ├── CMakeLists.txt                     视频助手构建配置
│   └── Get-ProjectInventory.ps1           项目文件清单与目录统计
├── samples/                               自编示例与配置模板
│   ├── mod-extensions/                    MOD 扩展示例
│   ├── screen-shake/                      屏幕震动效果示例
│   ├── artmd.ini                          精简美术规则示例
│   ├── internet_client.example.ini        互联网客户端配置模板
│   ├── internet_service.example.ini       互联网服务端配置模板
│   ├── map_basic.map                      精简地图示例
│   ├── rulesmd.ini                        精简游戏规则示例
│   └── runtime_config.example.ini         运行配置模板
├── cmake/                                 游戏、平台、视频助手与测试的构建配置
├── .gitattributes                         文本换行与文件类型规则
├── .gitignore                             私人数据与构建产物的忽略规则
├── API.md                                 主要接口、配置与命令行文档
├── CMakeLists.txt                         根构建入口，按序包含 cmake 配置
├── LICENSE                                GPL-3.0-only 许可全文
├── README.md                              项目介绍、目录说明与使用方法
├── 启动.cmd                               本机游戏启动入口
└── 编译.cmd                               本机游戏构建入口
```

`tests/fixtures/` 和 `samples/` 中的 INI、MAP 是自编测试输入，用于验证格式、规则和地图逻辑；它们不是完整原版游戏数据包，不能替代运行素材。

源码包不包含 `RedAlert2/`、`local_data/`、`resources/`、原项目 Git 历史、历史备份、截图或已编译程序。

## 编译

需要：

- Windows x64。
- Visual Studio 2022 或更新版本，安装“使用 C++ 的桌面开发”、Windows SDK，以及 x64 和 Win32 工具链。
- CMake 3.20 或更新版本。构建脚本优先使用 Visual Studio 自带的 CMake；所选 CMake 还须支持对应的 Visual Studio 生成器。

在仓库根目录双击 `编译.cmd`，或在 PowerShell 中执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Build-Game.ps1
```

脚本串行构建 Release 游戏核心 `concord_runtime`，并构建项目自编的 Win32 视频桥接工具 `BinkFrameExtractor`。输出位于 `bin/`，构建目录为 `build/`。核心编译不需要 Python，也不需要官方游戏素材。

`bin/` 内的运行库由本机 Visual Studio 复制，属于本机构建产物，不属于本仓库发布内容。视频桥接源码不包含 `Binkw32.dll` 或影片数据；如需视频播放，应由使用者自行提供其本地游戏所需文件。

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

CMake 保留专项测试目标。部分测试只使用自编夹具，部分需要本地原游戏数据；资源相关测试与调试工具需要按自己的环境配置路径。不能将无素材环境下的编译成功视为全部游戏功能或全部测试通过。

普通构建只编译游戏核心与视频助手，避免一次构建全部历史测试目标。`samples/` 内的演示准备脚本与部分开发工具需要先具备相应本地数据；Python 工具的解释器和依赖应自行准备。

## 上传 GitHub

仅上传这个源码目录的文件。不要上传原私人开发包、父目录、构建验证目录或旧 `.git` 历史。

如果使用 Git 命令上传，先进入这个源码目录，执行 `git init -b main` 创建全新独立仓库，再提交文件、配置你自己的 GitHub 远程地址。不要直接通过父目录的现有仓库推送，以免带入旧历史。若使用 GitHub 网页上传，请先解压源码 ZIP，再上传解压后的源码文件。

本仓库的 `.gitignore` 排除了原版数据目录、游戏素材格式、个人配置、缓存、编译产物和压缩包。源码公开包中的自编 INI/MAP 测试夹具有明确例外；不要用原版文件覆盖这些示例后提交。

游戏图像、模型、调色板、音频、视频、字体、官方地图、完整原版规则文件、游戏 EXE/DLL 以及从 MIX 提取的内容，均不属于本仓库发布范围。

## 版权与源码许可

RA2/YR 的名称、商标、原版程序及素材归各自权利人所有。本项目为非官方工程，与原游戏权利人无官方关联；公开引擎源码不授予原版游戏或其素材的使用、复制或再分发许可。

本仓库中由作者编写并有权许可的引擎源码、构建与开发工具、测试代码、自编测试夹具及示例，采用 **GNU General Public License 第 3 版（仅第 3 版）**，SPDX 标识为 `GPL-3.0-only`。完整许可证见 [LICENSE](LICENSE)。

你可以依照该许可证使用、修改和再分发这些内容；再分发时须遵守 GPL 第 3 版的要求。本项目按现状提供，不附带任何保证，具体以许可证全文为准。

上述源码许可不包含原版 RA2/YR 的程序、素材或商标授权，也不改变任何第三方内容本身的权利归属。
