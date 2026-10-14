# ConcordRTS API

本文介绍源码中的主要 C++ 接口、配置入口和命令行工具，供阅读、修改和集成引擎时参考。接口以当前头文件和实现为准；工程尚未提供独立发布的 SDK 或稳定的 ABI。

构建要求、私人资源准备方式和源码许可见 [README](README.md)。本文只包含接口说明与自编示例。

## 接口导航

| 模块                  | 主要用途             | 声明文件                                                                                                                                                                            |
| ------------------- | ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `IniFile`           | 读取 INI、查询键值、叠加配置 | [IniFile.h](src/core/io/IniFile.h)                                                                                                                                              |
| `RulesDatabase`     | 单位、建筑、武器、弹体和弹头规则 | [RulesDatabase.h](src/data/rules/RulesDatabase.h)                                                                                                                               |
| `ArtDatabase`       | 美术元数据及其与规则对象的关联  | [ArtDatabase.h](src/data/art/ArtDatabase.h)                                                                                                                                     |
| `MapDatabase`       | 地图对象、瓦片、覆盖层和地图信息 | [MapDatabase.h](src/data/maps/MapDatabase.h)                                                                                                                                    |
| `GameWorld`         | 从地图创建世界状态并查询对象   | [GameWorld.h](src/world/model/GameWorld.h)                                                                                                                                      |
| `MixArchive`        | MIX 索引、条目读取和提取   | [MixArchive.h](src/assets/archive/MixArchive.h)                                                                                                                                 |
| `GameResourceMount` | 扫描资源目录并生成挂载报告    | [GameResourceMount.h](src/assets/inspection/GameResourceMount.h)                                                                                                                |
| 运行时配置 | 配置类型、加载与偏好保存 | [RuntimeConfig.h](src/runtime/config/RuntimeConfig.h) |
| 运行时入口 | 启动检查与游戏窗口 | [RuntimeShell.h](src/runtime/host/RuntimeShell.h) |
| 互联网服务               | 服务配置、证书、连接和轮询    | [RuntimeInternetServiceHost.h](src/network/internet/RuntimeInternetServiceHost.h)、[RuntimeInternetServiceHostConfig.h](src/network/internet/RuntimeInternetServiceHostConfig.h) |

典型的数据加载顺序为 `IniFile → RulesDatabase / ArtDatabase / MapDatabase → GameWorld`。`ArtDatabase::buildLinks()` 可以将规则对象与美术定义关联。

## 返回值与对象生命周期

- `find*()` 查询未命中时返回 `nullptr`；使用前应检查。
- 数据库返回的指针和引用归数据库对象所有。重新加载或修改相关容器后，先前取得的指针、迭代器和元素引用可能失效。
- INI、规则、美术和地图加载采用容错解析。当前部分 `load*()` 实现即使遇到缺项仍会返回 `true`，应结合 `getWarnings()`、摘要及专项校验结果判断数据是否满足用途。
- MIX、服务配置和启动报告有各自的错误信息入口，具体见下文。
- 接口没有统一的并发访问约定。修改同一个世界或数据库时，应由调用方协调访问。

## INI 读取与叠加

`IniFile` 的常用接口：

```cpp
bool loadFromFile(const std::string& path);
bool loadFromString(const std::string& text);
void appendFrom(const IniFile& overlay);
const Section* getSection(const std::string& sectionName) const;
std::vector<std::string> getSectionNames() const;
std::string getValue(const std::string& sectionName,
                     const std::string& key,
                     const std::string& defaultValue = "") const;
```

`Section` 是 `std::vector<std::pair<std::string, std::string>>`。段名、键名忽略 ASCII 大小写；重复键查询时取最后一个值。`appendFrom()` 追加覆盖层，因此覆盖层中的同名键优先。

加载文本会清空当前内容。`loadFromFile()` 无法打开文件时返回 `false`，并向标准错误输出诊断；`loadFromString()` 当前不执行严格格式验证。缺少段时 `getSection()` 返回 `nullptr`，缺少键时 `getValue()` 返回传入的默认值。段名列表不保证排序。

## 规则与美术数据库

`RulesDatabase` 从已经解析的 INI 导入规则：

```cpp
bool loadFromIni(const IniFile& ini);
const std::vector<std::string>& getWarnings() const;
const UnitType* findUnitType(const std::string& kind,
                             const std::string& id) const;
const BuildingType* findBuildingType(const std::string& id) const;
const WeaponType* findWeaponType(const std::string& id) const;
const ProjectileType* findProjectileType(const std::string& id) const;
const WarheadType* findWarheadType(const std::string& id) const;
WeaponWarheadValidationReport validateWeaponWarheads() const;
```

`findUnitType()` 的 `kind` 可使用 `InfantryType`、`UnitType`（车辆）或 `AircraftType`。遍历接口包括 `getInfantryTypes()`、`getVehicleTypes()`、`getAircraftTypes()`、`getBuildingTypes()`、`getWeaponTypes()`、`getProjectileTypes()` 和 `getWarheadTypes()`，均返回对应容器的常量引用。

`validateWeaponWarheads()` 返回武器、弹头关联的专项诊断报告，不覆盖全部规则兼容性。规则字段的完整定义见头文件中的 `UnitType`、`BuildingType`、`WeaponType` 等结构。

以下示例只使用自编规则文本，无需游戏素材。示例须将 `src/` 加入头文件搜索目录，并与工程中的相应实现一起构建：

```cpp
#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"
#include <iostream>

int main()
{
    IniFile ini;
    ini.loadFromString(
        "[VehicleTypes]\n"
        "0=MYTANK\n"
        "[MYTANK]\n"
        "Name=Example Tank\n"
        "Strength=200\n");

    RulesDatabase rules;
    rules.loadFromIni(ini);
    for (const auto& warning : rules.getWarnings()) {
        std::cerr << warning << '\n';
    }

    const UnitType* unit = rules.findUnitType("UnitType", "MYTANK");
    if (unit == nullptr) {
        return 1;
    }
    std::cout << unit->strength << '\n';
    return 0;
}
```

`ArtDatabase` 解析图像 ID、序列、动画及渲染配置等元数据：

```cpp
bool loadFromIni(const IniFile& ini);
const std::vector<ArtType>& getArtTypes() const;
const ArtType* findArtType(const std::string& id) const;
const std::vector<std::string>& getWarnings() const;
std::vector<ArtObjectLink> buildLinks(const RulesDatabase& rules) const;
```

`buildLinks()` 生成规则对象与美术定义的关联，结果中的 `hasArt` 表示是否找到对应定义。元数据解析和关联本身不加载图像像素，也不会准备运行素材。

## 地图与世界状态

`MapDatabase` 的加载入口：

```cpp
bool loadFromIni(const IniFile& ini, const IniFile* rulesIni = nullptr);
const MapSummary& getSummary() const;
const std::vector<std::string>& getWarnings() const;
MapFacts buildMapFacts(const TheaterDatabase* theaterDatabase = nullptr,
                       const std::string& tileRootPath = "") const;
```

`rulesIni` 是规则的原始 `IniFile` 对象。地图查询包括 `getLighting()`、`getWaypoints()`、`getStructures()`、`getUnits()`、`getInfantry()`、`getAircraft()`、`getIsoTiles()` 和 `getOverlayCells()`。

地图加载解析对象并解码打包瓦片与覆盖层。缺少必要段、尺寸或打包数据异常时应检查警告；打包数据的摘要还可通过 `getIsoMapPackSummary()`、`getOverlayPackSummary()` 和 `getOverlayDataPackSummary()` 查询。

`buildMapFacts()` 汇总地图信息。涉及详细地形、瓦片或建筑占地时，可传入 `TheaterDatabase`、私人瓦片目录，并使用带 `std::vector<MapStructureFootprintOverride>` 参数的重载补充占地信息。

`GameWorld` 从地图建立供运行时系统使用的世界状态：

```cpp
bool loadFromMap(const MapDatabase& mapDatabase,
                 const RulesDatabase* rules = nullptr);
const GameWorldSummary& getSummary() const;
const std::vector<TerrainCell>& getTerrainCells() const;
const std::vector<WorldObjectInstance>& getObjects() const;
const std::vector<PlayerState>& getPlayers() const;
const std::vector<WorldWaypoint>& getWaypoints() const;
const std::vector<std::string>& getWarnings() const;
std::uint64_t deterministicSimulationStateHash() const;
```

导入后应结合警告和 `getSummary().countsMatchMap` 检查结果。加载世界本身不会推进战斗或打开渲染窗口。

`accessTerrainCells()`、`accessObjects()` 等接口返回可修改的容器引用，调用方负责维护状态一致性。`deterministicSimulationStateHash()` 计算规范化的仿真状态哈希；纯视觉缓存和诊断信息不参与，不能将它作为完整存档文件的哈希。

## MIX 与资源目录

`MixArchive` 提供归档索引和条目读取：

```cpp
bool loadFromPath(const std::filesystem::path& path);
bool loadFromFile(const std::string& path);
const std::string& getErrorMessage() const;
bool isIndexReadable() const;
const std::vector<MixEntry>& getEntries() const;
const MixEntry* findEntryById(std::uint32_t id) const;
const MixEntry* findEntryByName(const std::string& fileName) const;
bool readEntryData(const MixEntry& entry,
                    std::vector<std::uint8_t>& data,
                    std::string& errorMessage) const;
bool extractEntryToFile(const MixEntry& entry,
                        const std::string& outputPath,
                        std::string& errorMessage) const;
bool extractFileByName(const std::string& fileName,
                       const std::string& outputPath,
                       std::string& errorMessage) const;
```

Windows 路径可通过 `std::filesystem::path` 传入。加载失败时读取 `getErrorMessage()`；条目读取或提取失败时读取输出参数 `errorMessage`。提取接口会写入指定输出文件。

索引以 ID 标识条目，`findEntryByName()` 使用 `mixFileIdFromName()` 计算文件名 ID；条目列表不能直接恢复全部原始文件名。实现包含加密索引处理，可用 `hasEncryptedIndex()`、`hasChecksum()` 查询归档标记。

目录扫描入口为：

```cpp
ResourceMountReport buildResourceMountReport(
    const std::vector<ResourceMountRequest>& requests);
```

`ResourceMountRequest` 包含 `kind`、`name`、`rootPath` 和 `recursive`，资源层类型为 `OriginalGame`、`LocalData` 或 `Mod`。报告包含 `success`、资源层统计及 `warnings`。该接口生成扫描报告，完整运行仍需按 [README](README.md) 准备本地资源。

## 运行时配置与启动

`RuntimeConfig`、`RuntimeConfigOverrides` 及以下配置接口声明位于 [RuntimeConfig.h](src/runtime/config/RuntimeConfig.h)。配置类型头文件不依赖游戏窗口、世界状态或渲染器；加载实现见 [RuntimeConfig.cpp](src/runtime/config/RuntimeConfig.cpp)，偏好保存实现见 [RuntimeSettings.cpp](src/ui/settings/RuntimeSettings.cpp)。

```cpp
RuntimeConfig loadRuntimeConfig(const RuntimeConfigOverrides& overrides);
bool saveRuntimeConfigPreferences(const RuntimeConfig& config,
                                  std::string& errorMessage);
```

启动报告和窗口接口声明位于 [RuntimeShell.h](src/runtime/host/RuntimeShell.h)：

```cpp
RuntimeStartupReport buildRuntimeStartupReport(
    const RuntimeConfig& config,
    GameWorld* worldOutput = nullptr,
    TheaterDatabase* theaterOutput = nullptr,
    ArtDatabase* artOutput = nullptr,
    RulesDatabase* rulesOutput = nullptr);
int runRuntimeWindow(const RuntimeConfig& config,
                      bool exitAfterLoadingSmoke = false);
```

配置按“内置默认值 → INI → `RuntimeConfigOverrides`”合并。覆盖对象中的非空字符串和正整数覆盖对应设置；配置文件缺失时保留默认值，加载接口本身不返回配置警告列表。资源或地图缺失仍可能使启动失败，应检查启动报告中的诊断。

| C++ 字段                         | `[Runtime]` INI 键              | 作用          |
| ------------------------------ | ------------------------------ | ----------- |
| `configPath`                   | 由调用方或 `--config` 指定            | 配置文件路径      |
| `resourceRootPath`             | `ResourceRoot`                 | 原游戏资源目录     |
| `localDataRootPath`            | `LocalDataRoot`                | 本地规则及导出资源目录 |
| `modRootPath`                  | `ModRoot`                      | MOD 目录      |
| `defaultMapPath`               | `DefaultMap`                   | 默认地图        |
| `windowWidth` / `windowHeight` | `WindowWidth` / `WindowHeight` | 窗口尺寸        |
| `fixedTickHz`                  | `FixedTickHz`                  | 固定逻辑更新频率    |
| `targetFps`                    | `TargetFps`                    | 目标画面刷新率     |

菜单设置包括 `[Runtime] ShowMainMenu` 和 `[Skirmish] ShowSetup`。完整配置示例见 [runtime_config.example.ini](samples/runtime_config.example.ini)。相对路径按启动工作目录及发现的工程根解析，不按配置文件所在目录解析；外部私人数据建议使用绝对路径。

`buildRuntimeStartupReport()` 检查配置、资源、规则和地图，结果应检查 `success`、`errorMessage`、`warnings`。可选输出参数用于取得加载后的数据库和世界；另有输出 `MapFacts` 的重载。

`saveRuntimeConfigPreferences()` 原子更新指定的用户偏好项，并保留其他段、键和注释；它不保存完整世界状态。`runRuntimeWindow()` 打开并运行游戏窗口，另有接收 `RuntimeMultiplayerRuntimeBridge` 的重载。

## 命令行接口

以下命令是供使用者构建后调用的示例。源码发布包不包含这些 EXE。`Build-Game.ps1` 默认在 `build/msvc-reference/` 中生成游戏核心与视频助手，再部署到 `bin/`；规则工具和互联网服务是另行选择的 CMake 目标，MSVC 参考目录的 Release 输出位于 `build/msvc-reference/Release/`。使用其他构建目录或单配置 Ninja 时，须按实际目标输出位置调用。

### 游戏运行程序

```powershell
.\bin\concord_runtime.exe --help
.\bin\concord_runtime.exe --config "D:\MyPrivatePack\local_data\runtime_config.ini" --resource-root "D:\MyPrivatePack\RedAlert2" --local-data-root "D:\MyPrivatePack\local_data"
```

常用参数：

| 参数                                                    | 作用                |
| ----------------------------------------------------- | ----------------- |
| `--config <path>`                                     | 指定运行配置            |
| `--resource-root <path>` / `--local-data-root <path>` | 覆盖资源目录            |
| `--mod-root <path>` / `--map <path>`                  | 覆盖 MOD 或地图        |
| `--width <n>` / `--height <n>`                        | 覆盖窗口尺寸            |
| `--tick-hz <n>` / `--target-fps <n>`                  | 覆盖逻辑频率和目标刷新率      |
| `--runtime-events <path>`                             | 指定运行时事件记录输出       |
| `--skip-main-menu` / `--skip-skirmish-menu`           | 跳过对应菜单            |
| `--smoke-test`                                        | 加载启动数据并输出报告，不打开窗口 |

`--smoke-test` 仍需要对应的私人数据，退出码 `0` 表示启动报告成功，`1` 表示失败。该结果只反映启动检查。参数处理见 [RuntimeMain.cpp](src/runtime/host/RuntimeMain.cpp)。

### 规则与资源工具

```powershell
.\build\msvc-reference\Release\concord_rules_dump.exe --rules .\samples\rulesmd.ini --summary
.\build\msvc-reference\Release\concord_rules_dump.exe --rules .\samples\rulesmd.ini --validate
.\build\msvc-reference\Release\concord_rules_dump.exe --rules .\samples\rulesmd.ini --export-json .\output\rules.json
.\build\msvc-reference\Release\concord_rules_dump.exe --map .\samples\map_basic.map --map-summary
.\build\msvc-reference\Release\concord_rules_dump.exe --mix "D:\MyPrivatePack\example.mix" --list-mix
```

一次调用只能选择一个动作。其他入口包括 `--dump-units`、`--dump-weapons`；使用 `--export-json <path>` 时可附加 `--art <ini>` 导出美术定义及关联。JSON 导出创建所需父目录，并覆盖同名输出文件。

`--validate` 输出诊断，但诊断问题本身不保证产生非零退出码，应阅读输出结果。完整参数和执行流程见 [CommandLineOptions.cpp](src/analysis/cli/CommandLineOptions.cpp)、[main.cpp](src/analysis/cli/main.cpp)，JSON 字段见 [JsonExporter.cpp](src/analysis/reports/JsonExporter.cpp)。

### 互联网服务

```powershell
.\build\msvc-reference\Release\concord_internet_service.exe --config "D:\MyPrivatePack\internet_service.ini"
```

服务入口仅接受上述 `--config <path>` 调用形式，运行于 Windows，TLS 使用 Schannel。服务承担 TCP/TLS 协调与中继。配置示例见 [internet_service.example.ini](samples/internet_service.example.ini) 和 [internet_client.example.ini](samples/internet_client.example.ini)。

服务端配置要求各一个 `[Service]` 和 `[Credentials]` 段，示例中的七个服务键均为必填，凭据段至少包含一个客户端。服务端保存原始凭据字节的 SHA-256 摘要，客户端保存原始 `CredentialHex`；所有公开示例值均须在实际部署时替换。社区状态保存在 `<配置路径>.community.state`。

服务端 C++ 入口包括：

```cpp
bool loadRuntimeInternetServiceHostConfig(
    const std::filesystem::path& path,
    RuntimeInternetServiceHostDeploymentConfig& config,
    std::string& errorMessage);

// RuntimeInternetServiceHost 的成员函数
bool open(RuntimeInternetServiceHostConfig config, std::string& errorMessage);
RuntimeInternetServiceHostPollResult poll();
RuntimeInternetServiceHostStats stats() const;
bool isOpen() const;
std::uint16_t port() const;
void close() noexcept;
```

部署配置、证书加载和运行时 host 配置是不同步骤。直接使用 host 时须按配置结构提供时钟、鉴权及 TLS 信息；传入的 Windows 证书上下文须在 `open()` 调用期间有效，TLS 层会复制并持有它，调用方的原始引用可在返回后释放。完整初始化顺序见 [RuntimeInternetServiceMain.cpp](src/network/internet/RuntimeInternetServiceMain.cpp)。轮询结果中的 `succeeded` 和 `error` 用于判断运行故障。

服务退出码为：`0` 正常停止，`1` 用法或平台错误，`2` 配置错误，`3` 证书错误，`4` 打开服务失败，`5` 控制处理器设置失败，`6` 轮询故障。

网络接口为项目内部二进制协议，当前服务传输协议版本为 `1`，匹配协议版本为 `2`；跨版本使用须核对两端源码。协议声明见 [RuntimeInternetServiceTransport.h](src/network/transport/RuntimeInternetServiceTransport.h)、[RuntimeInternetMatchmakingProtocol.h](src/network/transport/RuntimeInternetMatchmakingProtocol.h) 和 [RuntimeInternetTlsTransport.h](src/network/transport/RuntimeInternetTlsTransport.h)。当前没有对外的 HTTP REST API。