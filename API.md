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
| 席位角色 | 独立判断观战角色与席位条件 | [PlayerSlotRole.h](src/core/players/PlayerSlotRole.h) |
| 比赛计分 | 建造、损失与击杀记账，保存和哈希 | [RuntimeMatchScore.h](src/core/results/RuntimeMatchScore.h) |
| CSF 文字 | 字节解析与显式回车投影 | [CsfCodec.h](src/assets/text/CsfCodec.h) |
| PAL 调色板 | 原始通道解析与颜色映射策略 | [PalDecoder.h](src/assets/codecs/PalDecoder.h) |
| SHP 图像 | 索引像素帧与两种兼容策略 | [ShpCodec.h](src/assets/codecs/ShpCodec.h) |
| TMP 地形资源 | 瓦片解码、资源定位与缓存 | [RuntimeTmpCodec.h](src/assets/codecs/RuntimeTmpCodec.h)、[RuntimeTerrainTmpQuery.h](src/assets/terrain/RuntimeTerrainTmpQuery.h) |
| 导航地形查询 | 通行类型、坡面与桥面判断 | [RuntimeNavigationTerrainQuery.h](src/simulation/navigation/terrain/RuntimeNavigationTerrainQuery.h) |
| 输入值与控制器 | 像素值、键盘准入及键鼠交互 | [RuntimeInputPixelValues.h](src/ui/input/RuntimeInputPixelValues.h)、[RuntimeKeyboardIngressController.h](src/ui/input/RuntimeKeyboardIngressController.h) |
| UI 时钟 | 同步借用毫秒时间与原计时策略 | [RuntimeUiClock.h](src/ui/time/RuntimeUiClock.h) |
| 预览资源会话 | 资源上下文、缓存和显式刷新 | [MapPreviewResourceContext.h](src/assets/preview/MapPreviewResourceContext.h)、[MapPreviewExporter.h](src/assets/preview/MapPreviewExporter.h) |
| 回放事件类型 | 独立事件数据及读取、汇总入口 | [RuntimeReplayEventSnapshot.h](src/runtime/contracts/RuntimeReplayEventSnapshot.h)、[ReplaySandbox.h](src/analysis/sandbox/ReplaySandbox.h) |
| 户级经济 | 同步借用单个阵营的余额与储矿记录 | [HouseEconomyContext.h](src/simulation/economy/HouseEconomyContext.h) |
| 采矿状态保存 | ComputerMining 读写与哈希贡献 | [RuntimeComputerMiningCodec.h](src/simulation/ai/RuntimeComputerMiningCodec.h) |
| 经济与生产固定步 | 收入、扣款及生产队列事务 | [RuntimeEconomyFixedStep.h](src/simulation/economy/RuntimeEconomyFixedStep.h)、[RuntimeProductionFixedStep.h](src/simulation/production/RuntimeProductionFixedStep.h) |
| 传送位置提交 | 超时空传送的坐标写入与通知 | [RuntimeChronosphereRelocation.h](src/simulation/superweapons/RuntimeChronosphereRelocation.h) |
| 生产规则解析 | 类型化生产参数与有序诊断 | [ProductionRules.h](src/data/rules/ProductionRules.h)、[ProductionRulesLoader.h](src/data/rules/ProductionRulesLoader.h) |
| 定时炸弹 | C4、Ivan 状态与值类型动作 | [RuntimeTimedBomb.h](src/simulation/abilities/unit_special/RuntimeTimedBomb.h)、[RuntimeSpecialMechanismEvents.h](src/simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h) |
| 寻路目标候选 | 同步生成普通与环形回退候选 | [PathGoalCandidates.h](src/simulation/navigation/pathfinding/PathGoalCandidates.h)、[PathGoalTypes.h](src/simulation/navigation/pathfinding/PathGoalTypes.h) |
| 地图地形帧 | 资源、光照与视野的短期只读输入 | [RuntimeMapTerrainFrame.h](src/render/core/RuntimeMapTerrainFrame.h)、[RuntimeMapTerrainFrameRenderer.h](src/render/core/RuntimeMapTerrainFrameRenderer.h) |
| 视频播放服务 | 解码子进程、IPC 与自有 BGRA 帧 | [BinkPlaybackService.h](src/assets/video/BinkPlaybackService.h) |
| LAN 地图传输 | 大厅地图请求、分块与应答状态 | [RuntimeMultiplayerLobbyMapTransfer.h](src/network/lobby/RuntimeMultiplayerLobbyMapTransfer.h) |
| 回放封包与恢复 | 流式记录、检查点与宿主恢复事务 | [RuntimeMatchReplay.h](src/runtime/replay/RuntimeMatchReplay.h) |
| 离线报告证据 | 来源、输入、覆盖与未知项 | [OfflineReportEvidence.h](src/analysis/reports/OfflineReportEvidence.h)、[EngineStatus.h](src/analysis/reports/EngineStatus.h) |
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

## 生产规则解析

[ProductionRulesLoader.h](src/data/rules/ProductionRulesLoader.h) 从已叠加的 `IniFile` 同步读取生产参数：

```cpp
ProductionRulesLoadResult loadProductionRules(const IniFile& ini);
double readProductionRuleCoefficient(
    const IniFile& ini, const std::string& section, const std::string& key,
    double defaultValue, double maximumValue,
    std::vector<std::string>& diagnostics);
```

返回值拥有 `ProductionRules values` 和两组诊断字符串，不保留 INI 引用。`buildupParseDiagnostics` 保存 BuildupTime 数字解析诊断，`diagnostics` 保存其余生产诊断；原数据库在两组之间处理采矿装卸字段，以维持既有警告顺序。

[ProductionRules.h](src/data/rules/ProductionRules.h) 的参数对应 `[General]`：

| INI 键 | 默认值 | 有效范围 |
| --- | --- | --- |
| `BuildSpeed` | 0.7 | 大于 0，不大于 100 |
| `BuildupTime` | 0.05 | 0 至 100，包含端点 |
| `MultipleFactory` | 0.8 | 大于 0，不大于 1 |
| `WallBuildSpeedCoefficient` | 3.0 | 大于 0，不大于 100 |
| `LowPowerPenaltyModifier` | 1.0 | 大于 0，不大于 100 |
| `MinLowPowerProductionSpeed` | 0.5 | 大于 0，不大于 1 |
| `MaxLowPowerProductionSpeed` | 0.8 | 大于 0，不大于 1 |

数值须为完整、有限的浮点文本；空值采用默认值，非法或越界值回退并记录诊断。最小低电量速度大于最大值时，两者同时回退为 0.5 和 0.8。对象的 `BuildTimeMultiplier` 使用同一系数读取接口，由对象加载器传入默认值 1 和上限 100。

配置叠加仍由 `IniFile::appendFrom()` 完成。后层提供空值或非法值时，解析器使用默认值，不恢复前层旧值。这里的严格浮点规则不替换其他字段已有的宽松整数或百分比规则；正常游戏加载继续从 `RulesDatabase::loadFromIni()` 进入。

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

## 视频播放服务

[BinkPlaybackService.h](src/assets/video/BinkPlaybackService.h) 将视频解码子进程及进程间通信集中在一个服务对象中：

```cpp
static bool supported() noexcept;
bool start(const BinkPlaybackRequest& request);
bool poll(BinkVideoFrame& output, std::uint32_t waitMilliseconds = 0);
void stop() noexcept;
const std::string& failure() const;
std::int32_t sequence() const;
bool finished() const;
```

以上为 `BinkPlaybackService` 成员。`BinkPlaybackRequest` 提供资源根、导出根、影片路径、Loop/Once 模式和声音设置，路径在 `start()` 中复制。`BinkVideoFrame` 拥有宽高、从上至下排列的 BGRA 字节和来源字符串；成功轮询替换完整帧，失败轮询保留调用方原帧。

服务不可复制或移动，须在所属线程同步调用；接口没有并发启动、轮询或停止保证。`stop()` 与析构只清理本会话的解码子进程及 IPC；停止后序列重置，完成与错误状态保留到下一次启动。公开接口不暴露共享映射或原生句柄，Windows 视频助手继续提供原 Bink 播放时钟与最后一帧保留行为。

使用前调用 `supported()`；当前非 Windows 服务返回不支持。实际播放仍需要使用者自己的影片、Bink 运行库及已构建的视频助手。缺失素材合同通过不表示影片或音频设备播放通过。

## LAN 大厅地图传输

[RuntimeMultiplayerLobbyMapTransfer.h](src/network/lobby/RuntimeMultiplayerLobbyMapTransfer.h) 管理大厅阶段的地图传输状态。对象不拥有 socket、文件系统、时钟或回调；会话负责解码消息、维护 peer 并按返回动作顺序发送或断开连接。

- `publish()` 接收并持有待发布地图及字节；每个主机 peer 使用独立的 `HostPeer` 保存传输进度。
- `receiveHost()` 与 `pollHost()` 接收明确的 peer、当前大厅状态、地图和单调时间，返回 `Action`。发送成功排队后调用 `sentHost()`；发送失败时停止该 peer 本轮轮询，避免错误推进进度。
- 客户端先调用 `prepareRequest()`。只有 Ready=false 和请求都成功排队后才调用 `requested()` 提交状态；`receiveClient()`、`pollClient()` 处理接收与期限。
- `Action` 包含 peer ID、transfer ID、可选发送消息、完成标志及协议或传输失败原因。`offer()` 和 `bytes()` 返回状态内的借用视图，重置或推进状态后须重新取得。

`Input::currentMap` 仅在本次调用内借用。`TimePoint` 使用 `std::chrono::steady_clock`，由调用方传入，保留原 250 毫秒重传与 180 秒期限。缓存文件的完整性检查及原子提交仍在原会话适配层；此接口不接管认证、快照恢复或重连状态。

## 保存、回放封包与恢复

[RuntimeMatchReplay.h](src/runtime/replay/RuntimeMatchReplay.h) 保留版本 1 的回放封包：`RuntimeMatchReplayWriter` 与 `RuntimeMatchReplayReader` 借用调用方的输入输出流，按序读写 Initial、Command、Step、Reset、Finish 与 Selection 记录。流须在读写期间保持有效；完成标志和错误信息按各成员接口读取。

`validateRuntimeMatchReplay()` 读取至 Finish 并拒绝尾随数据。Initial、Reset 和 Finish 需要非零校验值；Step 的零值表示延后到下一检查点验证。需要再次读取时，调用方须回退流并创建新的读取过程。

宿主的共享恢复实现位于 [RuntimeMatchReplayState.inl](src/runtime/replay/RuntimeMatchReplayState.inl)。它是依赖宿主状态的私有实现，不能作为独立公共头文件使用。恢复先在临时状态中解析、重建并核对 tick 与完整运行时哈希，再通过原事务入口提交；非法 tick、零检查点或零恢复哈希会被拒绝。

本地保存、回放 Reset 和网络快照的检查覆盖计分、ComputerMining、经济生产与定时炸弹等已提取状态，继续沿用原保存与快照格式。对应诊断见 [RuntimeRecoveryConsistencySmoke.inl](src/runtime/persistence/diagnostics/RuntimeRecoveryConsistencySmoke.inl)。这些封包与恢复接口不表示已经提供录像界面或自动录制功能。

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

## 输入值、UI 时钟与交互

### 像素值

[RuntimeInputPixelValues.h](src/ui/input/RuntimeInputPixelValues.h) 声明 `RuntimeInputPixelPoint`、`RuntimeInputPixelSize` 和 `RuntimeInputPixelRect`，字段均为默认零的 `std::int32_t`。头文件还提供打包鼠标坐标解码、中点与尺寸、镜头偏移、地图视口与侧栏裁剪，以及矩形包含和相交判断。

`runtimeInputPixelPointFromPackedPosition()` 将低 32 位中的两个有符号 16 位坐标解码为点。矩形包含采用闭区间，相交使用四个严格比较；零面积或反向矩形不自动归一化。中点、尺寸、镜头与侧栏计算保留原有符号 32 位运算，调用方须保证相关运算可表示；接口不提供饱和或溢出保护。

### UI 时钟

[RuntimeUiClock.h](src/ui/time/RuntimeUiClock.h) 提供类型化同步依赖：

```cpp
using RuntimeUiTimeMs = std::uint64_t;

class RuntimeUiClock {
public:
    virtual ~RuntimeUiClock() = default;
    virtual RuntimeUiTimeMs nowMs() const noexcept = 0;
};
```

提供者必须比所有借用它的会话和临时载入状态活得更久。该依赖不保存、不参与状态哈希或权威命令提交；它服务于 UI 表现，不替代模拟 tick。Windows 宿主通过 [RuntimeUiClockWin32.h](src/runtime/host/RuntimeUiClockWin32.h) 的 `runtimeWin32UiClock()` 提供原 `GetTickCount64` 时间源。

同头中的双次选择、悬停、横幅、闪烁与计分时间函数分别保留原比较和无符号减法策略。它们没有统一增加回拨过滤或钳制，不能互换各消费者的边界语义。

### 类型化交互控制器

控制器接收明确的输入值，通过各自 `Host` 查询当前状态并同步提交效果。原生窗口、消息解释、资源句柄及生产系统适配仍由私有宿主承担；世界与手势状态的借用应在原同步表达式内使用，外部效果调用后重新查询。Host 的生命期覆盖当前调用，控制器不保留引用或回调。

| 范围 | 接口入口 |
| --- | --- |
| 战役输入准入、键盘外层分发与命令路由 | [RuntimeKeyboardIngressController.h](src/ui/input/RuntimeKeyboardIngressController.h)、[RuntimeKeyboardRouterController.h](src/ui/input/RuntimeKeyboardRouterController.h) |
| 停止、警戒、散开、部署与编组 | [RuntimeSelectionKeyController.h](src/ui/input/RuntimeSelectionKeyController.h)、[RuntimeTeamKeyController.h](src/ui/input/RuntimeTeamKeyController.h) |
| 镜头、对象循环与快捷选择 | [RuntimeCameraKeyController.h](src/ui/input/RuntimeCameraKeyController.h)、[RuntimeLocalNavigationController.h](src/ui/input/RuntimeLocalNavigationController.h) |
| 规划、工厂、通信与表现快捷键 | [RuntimePlanningFactoryKeyController.h](src/ui/input/RuntimePlanningFactoryKeyController.h)、[RuntimeReferenceCommunicationController.h](src/ui/input/RuntimeReferenceCommunicationController.h)、[RuntimeReferencePresentationController.h](src/ui/input/RuntimeReferencePresentationController.h) |
| 终局、选项、保存载入与菜单按键 | [RuntimeSessionKeyController.h](src/ui/input/RuntimeSessionKeyController.h)、[RuntimeMenuKeyController.h](src/ui/input/RuntimeMenuKeyController.h) |
| 聊天字符、按键与 UTF-16 转 UTF-8 | [RuntimeChatTextController.h](src/ui/input/RuntimeChatTextController.h) |
| 左右键手势与完成点击 | [RuntimeMapLeftPointerController.h](src/ui/input/RuntimeMapLeftPointerController.h)、[RuntimeMapRightPointerController.h](src/ui/input/RuntimeMapRightPointerController.h) |
| 公共指针准入、外交界面与侧栏滚轮 | [RuntimePointerIngressController.h](src/ui/input/RuntimePointerIngressController.h)、[RuntimeSidebarPageController.h](src/ui/sidebar/RuntimeSidebarPageController.h) |
| 光标语义与捕获变化 | [RuntimeCursorController.h](src/ui/input/RuntimeCursorController.h)、[RuntimeInputCancellationController.h](src/ui/input/RuntimeInputCancellationController.h) |

例如 `handleRuntimeSelectionKey(input, host)` 返回 `Unhandled`、`RepeatedConsumed` 或 `DispatchedConsumed`，保留普通与系统按键来源及重复键策略。各控制器的 `bool` 返回值应按对应头文件解释，不能统一理解为命令成功；部分返回值只决定消息是否继续交给原生默认处理。

`encodeRuntimeChatTextUtf8()` 严格处理 UTF-16，保留显式 NUL code unit；无效输入或编码长度超过现有通信标识上限时返回空字符串。光标模块返回 `RuntimeCursorRequest` 语义请求，实际光标资源由宿主设置。`handleRuntimeInputCaptureChanged()` 先调用原右键策略，再处理菜单滑条与偏好；它不新增读取或清除左键手势状态。

## 席位与比赛计分

### 席位角色

[PlayerSlotRole.h](src/core/players/PlayerSlotRole.h) 是仅使用标准库的头文件接口：

```cpp
bool runtimeObserverCountry(std::string_view country);
template <typename Slot>
bool runtimeObserverSlot(const Slot& slot);
```

`runtimeObserverCountry()` 裁剪两端的空格、TAB、CR、LF，忽略 ASCII 大小写，接受 `observer` 或 `obs`。`runtimeObserverSlot()` 要求对象提供 `enabled`、`human`、`country` 字段，仅在启用的人类席位符合观战标识时返回 `true`。玩家本地席位与配置回退判断仍由 [RuntimeObserver.h](src/ui/input/RuntimeObserver.h) 的应用层适配承担。

### 比赛计分

[RuntimeMatchScore.h](src/core/results/RuntimeMatchScore.h) 声明六个计数值的 `RuntimeMatchScoreEntry`、按规范化 owner 排序的 `RuntimeMatchScoreState`，以及建造、销毁事件值。常用接口：

```cpp
void recordRuntimeMatchScoreBuild(
    RuntimeMatchScoreState& scores, const RuntimeMatchScoreBuildEvent& event);
void recordRuntimeMatchScoreDestruction(
    RuntimeMatchScoreState& scores, const RuntimeMatchScoreDestructionEvent& event);
void writeRuntimeMatchScore(const RuntimeMatchScoreState& scores, std::ostream& output);
bool loadRuntimeMatchScore(RuntimeMatchScoreState& scores,
                           const IniFile& ini, std::string& error);
void hashRuntimeMatchScore(const RuntimeMatchScoreState& scores, std::uint64_t& hash);
```

调用方先完成战局门控、归属与联盟判定，再提交事件。模块累计事件，重复提交会重复计入；对象退出去重仍由游戏宿主负责。销毁事件的空 `creditedOwner` 只记损失。计数保持饱和处理，加载支持原有 Schema 1/2；加载失败保留原状态，缺少保存段时清空状态。`hashRuntimeMatchScore()` 更新传入的哈希累加值。旧 [ui/results/RuntimeMatchScore.h](src/ui/results/RuntimeMatchScore.h) 保留对核心头的兼容包含。

## 户级经济与 AI 采矿

[HouseEconomyContext.h](src/simulation/economy/HouseEconomyContext.h) 定义一个阵营在同步调用期间使用的经济上下文。它按引用借用 owner、余额、储矿单位/价值/宝石数、运输次数、传送次数和容量，以及只读的矿车、精炼厂数量；`computerControlled` 为值标志。上下文不拥有这些对象，调用方须保证引用在调用期间有效。

`normalizeHouseResourceStorage()` 整理储矿记录，`recordHouseResourceDelivery()` 记入一次资源交付并返回容量、储存和溢出结果；`houseOrePurifierBonusValue()` 计算矿石精炼器加成。接口不包含科技树、生产目录或界面模型。

普通矿车、移动奴隶矿车及奴隶矿工使用同一显式户级上下文。AI 适配保留原 owner 顺序和本户工作账本，原同步反馈保护结束后提交净收入；采矿处理不再通过临时替换玩家经济、科技状态实现。相关消费者见 [RuntimeComputerMining.inl](src/simulation/ai/RuntimeComputerMining.inl)、[RuntimeHarvesterResourceAccess.inl](src/simulation/economy/RuntimeHarvesterResourceAccess.inl) 和 [RuntimeSlaveMinerEconomy.inl](src/simulation/economy/RuntimeSlaveMinerEconomy.inl)。

### ComputerMining 保存与哈希

[RuntimeComputerMiningCodec.h](src/simulation/ai/RuntimeComputerMiningCodec.h) 直接操作 `std::map<std::string, RuntimeComputerMiningState>`：

```cpp
void writeRuntimeComputerMining(
    std::ostream& output,
    const std::map<std::string, RuntimeComputerMiningState>& miningByOwner);

bool readRuntimeComputerMining(
    const IniFile& ini,
    std::map<std::string, RuntimeComputerMiningState>& output,
    std::string& error);

void mixRuntimeComputerMiningHash(
    std::uint64_t& hash,
    const std::map<std::string, RuntimeComputerMiningState>& miningByOwner);
```

保存使用原 `[ComputerMining]`、Schema 1 和 owner 排序。读取限制 `Count` 在 0 至 10000，先校验候选状态再提交；缺少该段时成功清空输出，失败时保留原输出状态。字段解析和错误文本沿用实现。哈希函数向调用方已有种子加入原贡献；空 map 不改变种子。完整存档格式编排和其他保存段仍由宿主负责。

## 经济与生产固定步

[RuntimeEconomyFixedStep.h](src/simulation/economy/RuntimeEconomyFixedStep.h) 提供余额存入、储矿价值缩放、扣款和每户收入处理：

```cpp
RuntimeEconomyFixedStepResult advanceRuntimeHouseEconomyFixedStep(
    HouseEconomyContext& house,
    bool simplifiedHarvesterIncomeEnabled,
    int& incomeCooldownTicks);
```

结果包含 `changed` 和状态文本；仅递减收入冷却时保持 `changed=false`。`spendRuntimeHouseCredits()` 在余额不足时返回 false，成功后同步调整余额和对应储矿记录。原收入处理位置保持在该户生产事务之前。

[RuntimeProductionFixedStep.h](src/simulation/production/RuntimeProductionFixedStep.h) 提供：

```cpp
RuntimeProductionFixedStepResult advanceRuntimeProductionFixedStep(
    RuntimeProductionLanesState& lanes,
    HouseEconomyContext& house,
    const RuntimeProductionAdvanceHooks& hooks);
```

函数先推进候选队列，核对队列扣款与全部事件扣款一致，再向该户经济账本扣款，最后提交队列。结果包含 `accepted`、`changed` 和事件列表；拒绝事务时不提交候选队列，返回的事件不能作为已接受的世界交付执行。`changed` 表示已提交队列的状态哈希是否变化。带类型的查询 hooks 保留原惰性调用顺序，界面、音频及单位实际交付仍由宿主按原位置处理。

这些接口提取收入与队列事务职责，完整固定步主循环的调用顺序、轻量路径和其他模拟阶段保持原有编排。

## 超时空传送的位置提交

[RuntimeChronosphereRelocation.h](src/simulation/superweapons/RuntimeChronosphereRelocation.h) 将一次位置写入和通知集中为：

```cpp
RuntimeChronosphereRelocationResult commitRuntimeChronosphereRelocation(
    WorldObjectInstance* object,
    const RuntimeChronosphereRelocationRequest& request,
    RuntimeChronosphereRelocationPort& port);
```

调用方先完成原合法性、占用、存活和生物类型策略。请求携带原坐标、目标坐标与 cell 编码，并区分普通编队到达和致命水域到达；模块依次写入坐标、按需查询高度、处理桥面/绘制/任务标志，再通知 `onRelocated()`。

空指针不产生效果；同格到达仍提交并通知一次。对象与 port 只在当前同步调用内借用，回调可查询或嵌套提交，但不能替换、扩容或重分配当前借用对象的存储。异常保留此前已发生的写入，不自动回滚。返回值拥有通知前捕获的旧/新姿态和通知时生命值，可在之后存储重建后继续读取，但不代表对象的实时状态。

## 定时炸弹与特殊机制事件

[RuntimeTimedBomb.h](src/simulation/abilities/unit_special/RuntimeTimedBomb.h) 声明 `RuntimeTimedBombState`，按目标对象 ID 持有有序炸弹记录。`TimedBombRules` 只传入 C4 与 Ivan 所需参数；`TimedBombObjectQuery`、`TimedBombEligibilityQuery` 和 `TimedBombDisarmQuery` 提供对象事实、放置能力与拆除条件，模块不取得完整世界或宿主对象。

- `tryPlantC4()`、`tryAttachIvanBomb()` 和 `tryDisarmTimedBomb()` 返回是否成功，并填写原因字符串；调用方传入原模拟 tick、适用的固定步频率与事件接收器。
- `tickTimedBombs()` 返回到期动作；`onObjectRemoved()`、`onOwnerChanged()` 和 `reconcile()` 处理对象及归属变化，按接口参数返回动作或发出事件。
- `records()` 返回内部有序记录的常量引用；其寿命依赖状态对象，修改、重置、交换或加载状态后应重新查询。

[RuntimeSpecialMechanismEvents.h](src/simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h) 提供 `SpecialCell`、`SpecialMechanismEvent`、`SpecialMechanismAction` 与 `SpecialEventSink`。动作只描述效果，仍由宿主在原世界更新位置消费；模块不直接销毁对象或扣减生命。

原 `RuntimeSpecialMechanismState` 继续作为游戏入口，并负责整个 SpecialMechanisms 的保存、哈希和原子读取事务。新模块没有另立一套独立保存格式。[独立诊断源码](src/simulation/abilities/unit_special/diagnostics/RuntimeSpecialMechanismsSmoke.cpp) 保留原诊断入口与断言。

## 共享资源解码

这些接口接收字节并返回解析数据。素材选择、文件读取、资源优先级及各消费者的显示行为由调用方承担。

### CSF 文字

[CsfCodec.h](src/assets/text/CsfCodec.h) 的接口位于 `concord::csf`：

```cpp
std::vector<Entry> decodeEntries(std::span<const unsigned char> bytes);
std::u16string projectCarriageReturns(std::u16string value, CarriageReturnPolicy policy);
```

`Entry` 保存原 label 和首个值，按文件顺序返回；值为 UTF-16 code units，保留 NUL 和 surrogate。坏尾保留此前完整条目及当前已完整的首值，空 label 或空首值不输出。`CarriageReturnPolicy::Space` 与 `LineFeed` 仅决定 CR 的投影；键名整理、重复项覆盖和查找回退仍由菜单适配器处理。

### PAL 调色板

[PalDecoder.h](src/assets/codecs/PalDecoder.h) 的接口位于 `concord::assets`：

```cpp
bool decodePalBytes(std::span<const std::uint8_t> bytes,
                    PalRawPalette& output, PalLengthPolicy lengthPolicy);
std::array<PalRgbChannels, 256> mapPalColors(
    const PalRawPalette& raw, PalColorPolicy colorPolicy);
```

`Exact768` 要求恰好 768 字节，`Prefix768` 接受至少 768 字节并忽略尾部。解析失败保留原输出；原始通道中任一值超过 63 时，整张调色板按八位处理。颜色投影须明确选择 `RoundedSixBit`、`DacShiftTwo` 或 `ReplicatedSixBit`；八位通道保持原值。接口不推断透明索引、remap、光照或像素格式。

### SHP 图像

[ShpCodec.h](src/assets/codecs/ShpCodec.h) 提供全局接口：

```cpp
bool decodeShpIndexedFrame(
    const std::vector<unsigned char>& bytes, const std::string& source,
    int frameIndex, ShpDecodePolicy policy,
    ShpIndexedFrame& frame, std::string& error);
```

`ShpIndexedFrame` 返回尺寸、偏移、格式元数据和索引像素，不包含调色板颜色。`Preview` 与 `Runtime` 保留原消费者在字段符号、格式接受范围和空帧上的差异，调用方须按用途选择。失败保留原 `frame`，成功保留调用方已有的 `error`；资源路径选择、帧回退、阴影处理与显示缓存留在原消费者。

### TMP 瓦片

[RuntimeTmpTypes.h](src/assets/codecs/RuntimeTmpTypes.h) 声明 `RuntimeTmpTileFile`、`RuntimeTmpSubTileImage` 与颜色数据；[RuntimeTmpCodec.h](src/assets/codecs/RuntimeTmpCodec.h) 提供：

```cpp
bool loadRuntimeTmpTileBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    RuntimeTmpTileFile& tileFile,
    std::string& errorMessage);
```

接口负责普通 TMP 与 raw format 3 的原完整解码，不执行文件或 MIX 读取。输出包含索引像素、附加图像、Z 数据、地形与雷达元数据。普通瓦片保留地形网格尺寸；不编码该网格的 raw format 3 保持未知。

解码失败可能已经修改 `tileFile` 的部分状态，不能按事务式输出处理；错误和部分成功的警告沿用原语义，调用方应同时检查返回值、文件状态和错误文本。文件选择、调色板投影及实际绘制由资源查询或消费者负责。

## 地形资源与导航查询

[RuntimeTerrainTmpQuery.h](src/assets/terrain/RuntimeTerrainTmpQuery.h) 将地形资源管理与绘图调用分开：

- `RuntimeTerrainTmpContext` 同步借用剧场及瓦片、素材、建筑根路径；它不是拥有这些对象的快照。
- `RuntimeTerrainTmpCache` 保存瓦片、最近错误、定位根、索引路径与缺失索引缓存，由原渲染器持有。
- `RuntimeTerrainTmpByteSource` 提供 loose/MIX 字节读取和资源根查询；`RuntimeTerrainTmpMemoPort` 仅桥接原私有帧、上下文和缓存匹配。
- `loadRuntimeTerrainTileForIndex()` 定位并取得缓存瓦片；`resolveRuntimeTerrainMetadata()`、`resolveRuntimeTerrainSubTileLandType()`、`resolveRuntimeTerrainSubTileRampType()` 和 `resolveRuntimeTerrainTmpTileGridShape()` 查询元数据。

模块保留原资源候选顺序、按需加载、负缓存及错误重放。返回的瓦片和子图像指针借用缓存，调用方须保持所属缓存和上下文有效，并遵守原作用域内的修改约定；清除、替换缓存或恢复世界状态后应重新取得当前对象。此接口没有统一并发访问保证，也不拥有几何帧、原生窗口或全部绘图资源。

[RuntimeNavigationTerrainQuery.h](src/simulation/navigation/terrain/RuntimeNavigationTerrainQuery.h) 提供导航判断：

```cpp
RuntimeNavigationTerrainInfo queryRuntimeNavigationTerrainInfo(
    RuntimeNavigationTerrainQueryPort& port,
    const TerrainCell& terrain);
```

`RuntimeNavigationTerrainQueryPort` 只公开剧场、墙体、桥面与子瓦片地形的同步只读查询，私有适配器连接当前资源生产者。查询保留墙体短路、sentinel 地形复制、land/ramp/岸线再次查询及断桥处理顺序。

[RuntimeTerrainTypes.h](src/simulation/navigation/terrain/RuntimeTerrainTypes.h) 中的返回值拥有全部 11 个字段：移动地形、坡型、地形种类、原始地形、可通行、水域、岸线、悬崖、坡面、桥面和代价。结果不借用渲染器或世界容器；查询端口和输入地形仍只在同步调用内借用。这一接口边界不表示整个导航或渲染系统已无平台依赖。

## 寻路目标候选

[PathGoalCandidates.h](src/simulation/navigation/pathfinding/PathGoalCandidates.h) 将目标候选计算与队列搜索分开；基础 `PathCell`、`PathMovementMode` 和 `PathTerrainCell` 见 [PathGoalTypes.h](src/simulation/navigation/pathfinding/PathGoalTypes.h)。

```cpp
PathGoalCandidateSet generatePathGoalCandidates(
    const PathGoalCandidateInput& request, const PathGoalTerrainView& terrain);
PathGoalFallbackCandidates generatePathGoalFallbackCandidates(
    const PathGoalCandidateInput& request, const PathGoalTerrainView& terrain,
    const PathBlockedGoalView& blockedComponent);
```

`PathGoalCandidateInput` 包含起终点、移动方式、目标槽位数、地图查询标志及借用的候选 span。输入由原队列提交入口先做规范化、去重和数量限制；`PathGoalTerrainView` 同步读取原地形与占用快照，索引顺序须保留原始顺序。`PathBlockedGoalView` 提供阻挡连通块的边界与格子集合。

返回值拥有候选向量，不保存输入、view 或 span。普通结果同时报告是否使用地图接近点及是否需要环形回退；回退结果附带距离查询和连通块检查计数。候选顺序与截断仍遵循原规则，邻近目标总数上限 16，环形回退上限 32。生成函数不执行 A* 搜索、更新队列或缓存、轮转槽位，也不消耗搜索预算；这些职责继续由 `PathRequestQueue` 持有。

## 地图与地形绘制帧

[RuntimeMapTerrainFrame.h](src/render/core/RuntimeMapTerrainFrame.h) 定义本次同步绘制使用的短期只读输入：

```cpp
RuntimeMapTerrainFrame prepareRuntimeMapTerrainFrame(
    const RuntimeMapTerrainFrameSources& sources,
    const RuntimeLightingSystemState& lighting);
```

`RuntimeMapTerrainFrameSources` 显式提供剧场、美术、规则与地图信息指针及各自就绪标志，以及视野限制、观察者、战役状态、离子风暴截止 tick 和当前 tick。未就绪的资源保留空指针策略；战役离子风暴尚未截止且地图信息可用时，选择地图的离子风暴光照。

返回帧拥有计算后的 `lightingTint` 与观察者字符串，只借用数据库、光照 profile 和视野对象。调用方须让帧及借用对象保持有效且不变，直到地图绘制、交互绘制和几何借用全部结束；不能跨资源重载、光照或视野修改、模拟 tick 保留该帧。

[RuntimeMapTerrainFrameRenderer.h](src/render/core/RuntimeMapTerrainFrameRenderer.h) 提供旧绘图上下文的适配器：

```cpp
RuntimeMapRenderContext runtimeMapRenderContextForTerrainFrame(
    const RuntimeMapTerrainFrame& frame);
```

应传入具名的常量帧；右值重载已禁用，避免上下文借用临时帧中的 tint。准备接口不查询几何或视野网格，不改变缓存版本检查时机。离屏入口共用资源与光照准备，同时保留原先稍后调用 `applyVision` 的顺序。窗口、绘图表面和呈现器仍由各自原调用方管理。

## 预览资源会话

[MapPreviewResourceContext.h](src/assets/preview/MapPreviewResourceContext.h) 声明 `concord::assets::MapPreviewResourceContext` 与 `MapPreviewResourceSession`。上下文绑定包根、瓦片/素材根、普通和建筑调色板路径、配置根顺序策略以及 `mountGeneration`。会话可移动、不可复制，由一个消费者串行持有和刷新。

类成员节选：

```cpp
const MapPreviewResourceContext& context() const;
void refresh();
std::size_t cachedDomainCount() const;
template <typename Value>
std::unordered_map<std::string, Value>& cache(MapPreviewCacheDomain domain);
bool readLooseBytes(MapPreviewCacheDomain domain, const std::string& path,
                    std::vector<unsigned char>& bytes, std::string& error);
```

会话拥有九个明确缓存域，包括覆盖层 TMP/SHP、解码错误、资源字节和诊断结果。同一缓存域须使用一致的值类型。`readLooseBytes()` 用于 `OverlayLooseBytes`、`DiagnosticLooseBytes`；相对路径按会话包根解析。成功读取的字节含空文件会缓存，打开、尺寸和读取失败不缓存；解码错误是否缓存由各缓存域的原策略决定。

文件改动不会自动使缓存失效。`refresh()` 清除全部九域并递增代际，旧缓存引用随即失效；会话销毁释放缓存。`context()` 返回的路径供 [MapPreviewExporter.h](src/assets/preview/MapPreviewExporter.h) 的 `exportMapTmpPreviewToHtml(session, ...)` 重载使用，其瓦片、素材与调色板路径必须与调用参数匹配。旧导出重载保留一次性会话适配。资源候选、loose/MIX 优先级及版别/MOD 根顺序沿用现有配置。

## 回放事件类型

[RuntimeReplayEventSnapshot.h](src/runtime/contracts/RuntimeReplayEventSnapshot.h) 独立声明 `RuntimeSnapshotCell` 与 `RuntimeReplayEventSnapshot`，仅依赖必要标准头。事件保留原字段类型、顺序和默认值；旧 [RuntimeShell.h](src/runtime/host/RuntimeShell.h) 继续兼容包含该头。只需要事件类型的消费者可直接包含窄头。

读取和汇总函数仍在 [ReplaySandbox.h](src/analysis/sandbox/ReplaySandbox.h)：

```cpp
bool loadRuntimeReplayEventSnapshotsFromFile(
    const std::string& path, std::vector<RuntimeReplayEventSnapshot>& runtimeEvents,
    std::string& errorMessage);
ReplayRuntimeEventSourceSummary summarizeRuntimeReplayEventSnapshots(
    const RuntimeReplayEventSnapshot* events, std::size_t count);
```

读取函数沿用现有事件文本解析和错误语义。`superWeaponId`、`superWeaponEvent`、`secondTargetCell` 当前未从文件恢复，保持默认值。完整对象容器需要包含窄头，以获得完整类型定义。本次布局对照限实测 Clang Release，不构成跨编译器、配置或版本的稳定 ABI 保证。

## 离线报告与证据来源

[OfflineReportEvidence.h](src/analysis/reports/OfflineReportEvidence.h) 为离线分析提供统一来源信息：`kind`、`conclusionSource`、`coverage`、输入方式与路径、输入状态与错误、事件数量和 `unknowns`。这些字段描述本次分析使用了什么材料，不验证材料的产生者。

[EngineStatus.h](src/analysis/reports/EngineStatus.h) 在 `EngineStatusReport` 中增加 `provenance`，并以 `EngineSubsystemStatus::evidenceSources` 对应原 evidence 行。原 Ready/Partial/Missing 及 evidence/gaps 含义保留：

```cpp
EngineStatusReport buildEngineStatusReport(const EngineStatusInput& input);
void writeEngineStatusReportJson(
    std::ostream& output, const EngineStatusReport& report);
void writeReplaySandboxReportJson(
    std::ostream& output, const ReplaySandboxReport& report);
```

回放报告声明见 [ReplaySandbox.h](src/analysis/sandbox/ReplaySandbox.h)。它保留原 runtime、runtime-partial 和 fallback 字段，同时说明显式文件、环境变量、自动发现或内存输入的来源及覆盖情况。旧文本导出保留原前缀，并追加证据来源段；独立 JSON 不替换旧文本输出。

JSON 中 `productionRuntimeValidation` 为 `not-run`，`productionRuntimeValidated` 与 `producerVerified` 为 false，`producerUtc` 为 null。输入具备完整字段也不代表真实游戏已运行、录像来源已核实或生产行为验收通过。输入路径会进入报告，生成报告应保存在本机。

## 模块库与独立合同

[LeafModuleLibraries.cmake](cmake/LeafModuleLibraries.cmake) 为 CSF 与 INI 两份实现建立五个单源 `STATIC` 变体：

| CMake 目标 | 实现 | 消费者编译约定 |
| --- | --- | --- |
| `concord_csf_runtime_leaf` | `CsfCodec.cpp` | 游戏运行时 |
| `concord_csf_contract_leaf` | `CsfCodec.cpp` | CSF 独立合同测试 |
| `concord_ini_runtime_leaf` | `IniFile.cpp` | 游戏运行时 |
| `concord_ini_tools_leaf` | `IniFile.cpp` | 规则与分析工具 |
| `concord_ini_contract_leaf` | `IniFile.cpp` | 普通独立合同测试 |

编译选项、定义和 CRT 设置均为 `PRIVATE`，各变体继承对应消费者约定；运行时 Debug 的 `/MD`、iterator level 0 与工具/普通合同 Debug 的 `/MDd`、默认 iterator level 2 分开保留。消费者仍配置自己的 `src/` 头文件搜索目录。现有静态库范围为上述两份实现，整体产品构建继续按 [README](README.md) 的 Windows 流程执行。

角色、计分、解码、预览会话、回放、输入、UI 时钟及地形标准检查见 [TestsUnitAndNetwork.cmake](cmake/TestsUnitAndNetwork.cmake)；回放测试目标定义见 [ReplaySnapshotContracts.cmake](cmake/ReplaySnapshotContracts.cmake)。地形与 TMP 的独立构建配置见 [NavigationTerrainQueryContracts.cmake](cmake/NavigationTerrainQueryContracts.cmake)、[TmpTerrainContracts.cmake](cmake/TmpTerrainContracts.cmake)，资源运行检查见 [TestsRuntimeRegression.cmake](cmake/TestsRuntimeRegression.cmake)。

指针、光标、取消捕获及相应 Windows 集成检查见 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake)。这些检查按具体 CMake 目标和 CTest 名称选择，未自动并入 `windows-minimal` 的 12 项或 `windows-local-assets` 的 4 项集合；标准合同、素材检查、原生适配与真实网络运行的结果须分别判断，不能相互替代。

经济与传送的标准目标定义见 [RuntimeSmokeTargets.cmake](cmake/RuntimeSmokeTargets.cmake)，它们直接编译相应生产实现。`chronosphere_relocation_contract_test`、`house_economy_context_contract_test` 在 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake) 注册；`computer_mining_codec_contract_test` 和 `economy_production_fixed_step_contract_test` 在 [TestsUnitAndNetwork.cmake](cmake/TestsUnitAndNetwork.cmake) 注册。后两项读取 [采矿保存夹具](tests/fixtures/runtime_computer_mining_codec) 和 [固定步夹具](tests/fixtures/runtime_economy_production_fixed_step)，比较完整预期文本；公开构建入口只读取这些夹具。

定时炸弹、寻路候选、生产规则和地形帧的新增独立与对照测试源码见 [README 的测试说明](README.md#测试与开发)。这六份测试目前未注册为工程的 CMake/CTest 目标；开发时使用的私人验证脚本、旧版对照副本和执行记录不随源码发布。

视频服务、LAN 地图传输、回放封包与恢复一致性分别注册为 `bink_playback_service_contract_test`、`runtime_lobby_map_transfer_contract_test`、`match_replay_envelope_contract_test`、`runtime_recovery_consistency_contract_test`。前者在不支持的平台使用跳过返回码；跳过不能计作通过。这些检查未并入固定的 12＋4 测试集合。

[结构检查入口](tools/testing/Run-StructureChecks.ps1) 接收已有 Release 构建目录和明确的 Python 路径；[检查器](tools/structure/structure_guard.py)、[精确例外基线](tools/structure/structure-baseline.json) 与 [合成夹具](tools/structure/test_structure_guard.py) 共同检查依赖、平台类型、目标归属和测试注册。接口与运行条件见 [README](README.md#测试与开发)，无需复制私人开发记录。

## 命令行接口

以下命令是供使用者构建后调用的示例。源码发布包不包含这些 EXE。`Build-Game.ps1` 默认采用 `clang-cl + Ninja`，在 `build/clang-cl-x64-release/` 中生成游戏核心和独立 x86 视频助手，再部署到 `bin/`。规则工具和互联网服务是另行选择的 CMake 目标，Ninja 的默认 Release 配置输出位于 `build/clang-cl-x64-release/`，没有额外的 `Release/` 子目录。显式 MSVC 参考配置的输出仍位于 `build/msvc-reference/Release/`；其他目录须按实际目标输出位置调用。

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
.\build\clang-cl-x64-release\concord_rules_dump.exe --rules .\samples\rulesmd.ini --summary
.\build\clang-cl-x64-release\concord_rules_dump.exe --rules .\samples\rulesmd.ini --validate
.\build\clang-cl-x64-release\concord_rules_dump.exe --rules .\samples\rulesmd.ini --export-json .\output\rules.json
.\build\clang-cl-x64-release\concord_rules_dump.exe --map .\samples\map_basic.map --map-summary
.\build\clang-cl-x64-release\concord_rules_dump.exe --mix "D:\MyPrivatePack\example.mix" --list-mix
```

一次调用只能选择一个动作。其他入口包括 `--dump-units`、`--dump-weapons`；使用 `--export-json <path>` 时可附加 `--art <ini>` 导出美术定义及关联。JSON 导出创建所需父目录，并覆盖同名输出文件。

`--validate` 输出诊断，但诊断问题本身不保证产生非零退出码，应阅读输出结果。完整参数和执行流程见 [CommandLineOptions.cpp](src/analysis/cli/CommandLineOptions.cpp)、[main.cpp](src/analysis/cli/main.cpp)，JSON 字段见 [JsonExporter.cpp](src/analysis/reports/JsonExporter.cpp)。

离线能力或回放报告可附加 `--report-json <path>`。以下例子使用自编测试规则；工具需先按构建说明准备，输出父目录须已存在：

```powershell
.\build\clang-cl-x64-release\concord_rules_dump.exe --rules .\tests\fixtures\offline_report_evidence\rules.ini --engine-status --report-json .\build\offline-engine-status.json
.\build\clang-cl-x64-release\concord_rules_dump.exe --rules .\tests\fixtures\offline_report_evidence\rules.ini --replay-sandbox --runtime-events .\tests\fixtures\offline_report_evidence\complete-events.txt --report-json .\build\offline-replay-report.json
```

`--report-json` 只允许与 `--engine-status` 或 `--replay-sandbox` 一起使用。默认拒绝覆盖已有 JSON，明确传 `--overwrite` 才允许替换；若同时指定旧回放文本的 `--output`，两者必须指向不同文件。报告中的完整事件与成功状态仅表达所选离线分析结果，生产运行验收仍为 `not-run`。

### 互联网服务

```powershell
.\build\clang-cl-x64-release\concord_internet_service.exe --config "D:\MyPrivatePack\internet_service.ini"
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