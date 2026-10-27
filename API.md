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
| 辅助动画 | 帧范围、往返循环与规则时间换算 | [RuntimeAuxiliaryAnimation.h](src/render/buildings/RuntimeAuxiliaryAnimation.h) |
| 视野与开火揭示 | 观察者视野、短期揭示与保存恢复 | [RuntimeVision.h](src/simulation/vision/RuntimeVision.h) |
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
| 确定性候选任务 | 自有输入、固定顺序消费与同步回退 | [PathGoalCandidateTask.h](src/simulation/navigation/pathfinding/PathGoalCandidateTask.h) |
| 软件窗口与事件 | 类型化窗口操作、像素事件与同步帧呈现 | [RuntimeSoftwareWindow.h](src/runtime/window/RuntimeSoftwareWindow.h) |
| 菜单绘制命令 | 有序命令、自有资源字节与同步帧借用 | [MenuDrawCommands.h](src/ui/drawing/MenuDrawCommands.h) |
| 呈现后端选项 | 本机战局呈现和菜单绘制的独立选择 | [FrameBackendOptions.h](src/runtime/presentation/FrameBackendOptions.h) |
| 完整软件帧 | 同步像素借用、场景版本与提交结果 | [SoftwareFrame.h](src/runtime/presentation/SoftwareFrame.h) |
| 地图地形帧 | 资源、光照与视野的短期只读输入 | [RuntimeMapTerrainFrame.h](src/render/core/RuntimeMapTerrainFrame.h)、[RuntimeMapTerrainFrameRenderer.h](src/render/core/RuntimeMapTerrainFrameRenderer.h) |
| 视频播放服务 | 解码子进程、IPC 与自有 BGRA 帧 | [BinkPlaybackService.h](src/assets/video/BinkPlaybackService.h) |
| LAN 地图传输 | 大厅地图请求、分块与应答状态 | [RuntimeMultiplayerLobbyMapTransfer.h](src/network/lobby/RuntimeMultiplayerLobbyMapTransfer.h) |
| 回放封包与恢复 | 流式记录、检查点与宿主恢复事务 | [RuntimeMatchReplay.h](src/runtime/replay/RuntimeMatchReplay.h) |
| 离线报告证据 | 来源、输入、覆盖与未知项 | [OfflineReportEvidence.h](src/analysis/reports/OfflineReportEvidence.h)、[EngineStatus.h](src/analysis/reports/EngineStatus.h) |
| 窗口生命周期 | 逻辑窗口标识与同步关闭、销毁动作 | [RuntimeWindowId.h](src/runtime/window/RuntimeWindowId.h)、[RuntimeWindowLifecycle.h](src/runtime/window/RuntimeWindowLifecycle.h) |
| 遭遇战菜单控制 | 菜单状态、输入动作与选择配置映射 | [RuntimeSkirmishController.h](src/runtime/menu/RuntimeSkirmishController.h)、[RuntimeSkirmishSetup.h](src/ui/skirmish/RuntimeSkirmishSetup.h) |
| 菜单图像与字体 | 自有 BGRA 图像、资源会话与只读字形 | [MenuImageResources.h](src/assets/menu/MenuImageResources.h)、[WestwoodFontCodec.h](src/assets/text/WestwoodFontCodec.h) |
| CPU 菜单绘图 | 像素缓冲、颜色值与位图文字 | [RuntimeMenuPixelSurface.h](src/ui/surface/RuntimeMenuPixelSurface.h)、[RuntimeSkirmishBitmapText.h](src/ui/text/RuntimeSkirmishBitmapText.h) |
| 菜单音乐 | UI 计时策略与设备能力接口 | [RuntimeSkirmishMusicPolicy.h](src/ui/audio/RuntimeSkirmishMusicPolicy.h)、[RuntimeMenuMusicDevice.h](src/audio/menu/RuntimeMenuMusicDevice.h) |
| 联机快照会话 | 传输、暂存、校验及提交动作 | [RuntimeMultiplayerSnapshotSession.h](src/network/snapshots/RuntimeMultiplayerSnapshotSession.h) |
| 有界任务执行器 | 独占字节输入、队列预算、代际与单次领取 | [BoundedTaskExecutor.h](src/core/tasks/BoundedTaskExecutor.h) |
| 巨炮预热 | 私有光栅任务、所有者准备与结果安装 | [GrandCannonRasterTask.h](src/render/voxel/GrandCannonRasterTask.h)、[RuntimeGrandCannonPrewarm.h](src/render/voxel/RuntimeGrandCannonPrewarm.h) |
| 性能计时 | 生产循环的 CPU 阶段与有界样本 | [RuntimeLiveFrameBenchmark.h](src/runtime/performance/diagnostics/RuntimeLiveFrameBenchmark.h) |
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

`saveRuntimeConfigPreferences()` 原子更新指定的用户偏好项，并保留其他段、键和注释；它不保存完整世界状态。`runRuntimeWindow()` 打开并运行游戏窗口，保留原有的配置入口和接收 `RuntimeMultiplayerRuntimeBridge` 的重载；新增第四参数为 `FrameBackendOptions` 的重载，用于显式选择本机呈现方式。

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

## 窗口生命周期与遭遇战菜单

[RuntimeWindowId](src/runtime/window/RuntimeWindowId.h) 是分配的 `uint64_t` 标识，0 表示没有宿主窗口。它不是原生句柄；后端只在相应父窗口租约有效期间解析标识。

[RuntimeWindowLifecycle.h](src/runtime/window/RuntimeWindowLifecycle.h) 将 `Hidden`、`Minimized`、`Close`、`Destroyed` 交给 `handleRuntimeWindowLifecycle(event, sessionAttached, port)`。返回 `true` 表示原生适配层已消费消息；隐藏和最小化在必要的帧计量重置后仍返回 `false`。有会话时关闭先报告离开，再请求销毁；销毁依次停止 EVA、停止音乐、释放图形资源，最后发送退出。调用同步发生在 UI 线程，销毁请求可在返回前重入 `Destroyed`，接入方必须保留这个顺序。

[RuntimeSkirmishController](src/runtime/menu/RuntimeSkirmishController.h) 位于 `concord::skirmish`，保存菜单页、地图筛选、槽位、命中区域及交互状态；`RuntimeSkirmishServices` 提供重绘、音效、时间、偏好保存、预览等同步操作。`handleSkirmishMouseMove`、`handleSkirmishLeftDown`、`handleSkirmishLeftUp`、`handleSkirmishKey` 和 `handleSkirmishWheel` 接收已转换的坐标或按键值；原生消息转换仍由适配层负责。

选择数据见 [RuntimeSkirmishSelection.h](src/runtime/contracts/RuntimeSkirmishSelection.h)。调用方接受选择后，才通过 [applyRuntimeSkirmishSelection](src/ui/skirmish/RuntimeSkirmishSetup.h) 写入 `RuntimeConfig`；固定步、随机种子、联机身份及无关设置继续由调用方管理。

## 软件窗口与 SDL3 菜单入口

[RuntimeSoftwareWindow.h](src/runtime/window/RuntimeSoftwareWindow.h) 在 `concord::window` 中定义窗口端口和事件值，公共头文件不包含 SDL 或 Windows 类型。`SoftwareWindowOptions` 指定客户区尺寸及绘制能力；当前实现只接受 `BitmapFrame`、正尺寸、独立窗口，其他绘制方式、嵌入父窗口和原生随机地图编辑器由 `softwareWindowUnsupported()` 明确拒绝。

`createSdl3SoftwareWindow(options, events, statistics, error)` 返回独占的 `std::unique_ptr<SoftwareWindow>`。未编入 SDL3、能力不支持、依赖加载或窗口创建失败时返回空值并填写错误。调用方拥有事件接收器和 `FramePresentStatistics`，两者须存活到窗口销毁后；窗口、事件处理与呈现由同一个 UI 线程持有。同一时刻仅允许一个 SDL 视频会话，重复创建会被拒绝。

| 接口或值 | 约定 |
| --- | --- |
| `SoftwareWindowEvent` | 自有类型化事件，包含客户区像素点、尺寸、滚轮格数、键值与重复标记；不暴露原生事件指针 |
| `SoftwareWindowEvents::windowEvent` | 在窗口事件处理中同步回调；消费者沿用既有控制器与坐标命中逻辑 |
| `poll() / wait(milliseconds)` | 处理或等待窗口事件；UI 时钟由 `nowMs()` 提供，不推进战局模拟 |
| `focus() / capture() / releaseCapture()` | 请求焦点与鼠标捕获；`captured()` 查询状态，失焦和捕获丢失仍按独立事件处理 |
| `size() / resize() / minimize() / restore()` | 查询或调整窗口尺寸与显示状态；`alive() / drawable()` 区分生命周期和可绘制状态 |
| `presenter()` | 返回同步借用的 `FramePresenter`；必须先完成当前尺寸下的 CPU 画面，再调用 `present()` |
| `close() / error()` | 关闭窗口或读取错误；销毁时释放窗口、视频会话和动态库，并恢复光标状态 |

私有 [RuntimeSdl3Window.cpp](src/runtime/window/sdl3/RuntimeSdl3Window.cpp) 将 SDL 逻辑坐标转换为客户区像素，再交给既有视口转换。窗口缩放后重新获取软件 surface，并按实际格式、步长转换已完成帧；小端平台按 XRGB8888 的 BGRX 字节解释，不执行 alpha 混合。呈现仅同步借用像素，调用结束不保留帧指针。

[runRuntimeSkirmishMenuSdl3(config, error)](src/ui/skirmish/RuntimeSkirmishMenu.h) 是独立菜单入口；[软件菜单接入](src/ui/skirmish/RuntimeSkirmishSoftwareWindow.inl) 复用原控制器、位图文字、资源会话及选择数据。确认后仍调用既有 `applyRuntimeSkirmishSelection` 并进入 Win32 战局；菜单事件不修改战局 tick。此入口不替换主战局绘制或菜单音频设备，创建失败也不会自动换用另一窗口。

依赖由 [sdl3.lock.json](tools/dependencies/sdl3.lock.json) 固定为 3.4.16，通过 [Build-Sdl3.ps1](tools/dependencies/Build-Sdl3.ps1) 从已提供的源码构建；CMake 选项 `CONCORD_ENABLE_SDL3_WINDOW` 默认关闭。运行时仅加载程序旁的匹配版本 `SDL3.dll`。配置步骤见 [README](README.md)，所选真实窗口合同见 [RunSecondWindowNative.cmake](cmake/RunSecondWindowNative.cmake) 和 [Run-SecondWindowContract.ps1](tools/testing/Run-SecondWindowContract.ps1)；该合同需要本地游戏素材、可选依赖和可创建窗口的环境。

## 菜单图像、字体与 CPU 绘图

### 图像与资源

[MenuImage.h](src/assets/menu/MenuImage.h) 中的 `concord::assets::MenuImage` 持有尺寸、BGRA 像素及来源字符串。`makeMenuImage()` 在本次调用中借用索引帧和调色板，返回的图像拥有自己的像素。索引 0 是否透明由 `transparentZero` 显式指定。

[MenuImageResources](src/assets/menu/MenuImageResources.h) 是可移动、不可复制的菜单资源所有者，通过 `configureRoots()`、`loadPalette()`、`loadFrame()` 复用既有资源实现，输出图像和调色板均为独立数据。配置根路径不自动清空已有缓存；它复用预览根目录配置类型，不共用预览会话的九类缓存。实现位于 [MenuImageResources.cpp](src/render/resources/MenuImageResources.cpp)，接口不接收设备、世界状态或原生句柄。

`loadMenuLooseFrame()` 保留主菜单帧零、全局画布与偏移策略；`loadMenuFrameLocalShp()` 保留遭遇战的帧局部画布策略，并在尝试前重置输出。这两种原有兼容语义分别保留。

### 字体与颜色

[WestwoodFontCodec.h](src/assets/text/WestwoodFontCodec.h) 的 `decodeWestwoodFontHeader(bytes, metrics)` 只解析字节。输入过短或魔数错误时保持原 metrics；可识别的完整头即使语义校验失败，也按原结果页约定写入原始字段，因此失败不总是意味着输出未变。

`westwoodFontGlyph()` 接收一个 `uint16_t` 码元，返回借用的字形记录；字节所有者重载或释放后指针失效。`MissingReferenceOnly` 与 `AnyUnavailableRecord` 分别保留缺映射和无效宽度时的不同问号回退规则；编码组合、排版和资源选择由调用方负责。

[RuntimeRgbColorValue](src/ui/values/RuntimeColorValues.h) 只保存 `0x00BBGGRR` 颜色数据，可用 `fromChannels()`、`fromPackedRgb()` 构造，高位标志被忽略，alpha 由绘图操作决定。

### 像素缓冲与文字

[RuntimeMenuPixelSurface.h](src/ui/surface/RuntimeMenuPixelSurface.h) 提供 `concord::ui::MenuPixelBuffer`、可写及只读视图。像素为自上而下的 BGRA8，`rowStride` 可包含行尾填充，最后一行至少需要 `width * 4` 字节。`reset()`、`release()` 会使旧视图失效；借用仅适用于同步调用，`owns()` 用于核对缓冲所有权。

`clearMenuPixels()`、`fillMenuPixels()`、`darkenMenuPixels()`、`frameMenuPixels()`、`blitMenuPixels()` 和 `scaleMenuPixels()` 保留现有菜单绘图规则。混合沿用除以 255 的运算，中间透明度保留目标 alpha；缩放采用最近邻采样，并将复制像素的 alpha 置为 255。

[RuntimeSkirmishBitmapText.h](src/ui/text/RuntimeSkirmishBitmapText.h) 的 `SkirmishBitmapFontView` 借用字体字节和 metrics。`measureSkirmishBitmapText()`、`drawSkirmishBitmapText()` 分别接收 `wstring_view` 或 `u16string_view`；按 UTF-16 码元保留原遭遇战排版，不合并代理项。绘制目标是本次调用借用的连续 BGRA 缓冲，字体无效或缓冲尺寸不足时不绘制。

Windows DIB、DC 和 GDI 对象由 [RuntimeSkirmishBackBufferWin32](src/ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h) 私有管理。CPU 表面只提供存储与绘图输入，不代表一个通用的完整帧呈现接口。

## 菜单音乐策略与设备

[SkirmishMusicPolicy](src/ui/audio/RuntimeSkirmishMusicPolicy.h) 同步借用 `RuntimeUiClock` 和文件存在检查函数，二者须按接口约定保持有效。`start()`、`poll()`、`stop()` 接收同一所有者管理的 [MenuMusicDevice](src/audio/menu/RuntimeMenuMusicDevice.h)，保留原启停、约一秒轮询、循环重播与失败重开顺序。

设备提供 `supported()`、`open()`、`playFromStart()`、`isPlaying()`、`stop()`、`close()`。`isPlaying()` 的空可选值表示无法取得播放状态，不能当成已播放；`makeUnsupportedMenuMusicDevice()` 明确报告不支持，打开和播放返回失败。关闭或停用偏好需要调用方按原流程调用 `stop()`，单独传入 `enabled=false` 的轮询不会自动关闭设备。

实际 Windows 实现见 [RuntimeSkirmishMusicWin32.h](src/audio/menu/win32/RuntimeSkirmishMusicWin32.h)，构造不打开设备，所有操作在 UI 线程同步执行。设备所有权用于避免旧实例析构关闭已经替换的播放会话；Bink 视频服务保持自己的独立生命周期。

## TCP 边界与联机快照会话

[RuntimeLanTransport.h](src/network/session/RuntimeLanTransport.h) 保持标准类型和私有实现边界，所选消费者通过 [LanTransportProviders.cmake](cmake/LanTransportProviders.cmake) 链接对应编译约定的生产实现。Winsock 依赖保留在实际 Windows 消费者的私有链接中；该整理没有改变网络协议，也不表示所有联机职责均已隔离。

[RuntimeMultiplayerSnapshotSession](src/network/snapshots/RuntimeMultiplayerSnapshotSession.h) 管理分块传输、已校验候选及提交状态，同步借用会话、快照 hooks、本地 peer 字符串和当前阶段；这些引用的所有者必须比组件长寿。组件不拥有规范会话状态或原生传输，状态视图不得跨越 reset 使用。

`stageSnapshotBytes()`、`commitSnapshot()`、`postSnapshotSessionCommit()` 对接原暂存与提交流程；`handleClientSnapshotChunk()`、`handleClientSnapshotCommit()`、`issueHostSnapshotCommit()` 和主机轮询接口继续使用原消息、完整性校验及应答状态。`RuntimeMultiplayerSnapshotSessionPort` 同步提供发包、广播、协议失败、提交事件及中止动作；它不向组件暴露套接字、文件系统或窗口状态。

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

实现先读取指定的帧记录，再通过有界字节视图解码像素。`Runtime` 使用有符号 16 位尺寸与偏移，以及单字节压缩标记；`Preview` 保留无符号字段投影和额外格式字节的既有处理。两者均支持原始像素与格式 3 透明游程；格式 2 逐行像素仅由 `Runtime` 接受。

行长度保留完整的 16 位值，包含长度字段本身。短行补透明像素，长行完整消费后裁剪到可见宽度；格式 3 在可见宽度之外仍检查游程是否截断。运行时的合法空白停顿帧通过 `authoredPause` 标记，不能当成缺失资源。相关自编边界输入见 [ShpCodecCorpus.h](tests/assets/ShpCodecCorpus.h)。

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

## 确定性候选任务

[PathGoalCandidateTask.h](src/simulation/navigation/pathfinding/PathGoalCandidateTask.h) 将初始候选计算封装为数值输入与自有输出：

```cpp
std::optional<PathGoalTaskInput> capturePathGoalTaskInput(
    const PathGoalCandidateInput&, const PathGoalTerrainView&, PathGoalTaskStamp);
PathGoalTaskOutput computePathGoalTask(
    const PathGoalTaskInput&, std::span<PathCell> privateScratch);
concord::tasks::Task makePathGoalTask(const PathGoalTaskInput&);
std::optional<PathGoalTaskOutput> takePathGoalTask(
    concord::tasks::BoundedTaskExecutor&, concord::tasks::JobKey,
    const PathGoalTaskStamp& expected);
PathGoalCandidateSet adoptPathGoalTask(
    const PathGoalTaskInput&, const std::optional<PathGoalTaskOutput>&,
    bool& usedWorker);
```

`PathGoalTaskInput` 最多保存 64 个目标和 25 个邻域节点，保留原输入索引顺序，只复制格子值与通行标志。`PathGoalTaskStamp` 含代际、序号、地图与占用身份、模拟 tick、队列 tick、请求编号及搜索预算。当前地图与占用身份均取自完整合并地形内容指纹，不是两个独立递增计数。任务不保留世界、地形视图、队列、随机数或可变缓存的指针。

`computePathGoalTask()` 需要至少 64 个 `PathCell` 的独占工作区，返回固定容量的输出，报告候选数量、接近点使用情况和是否需要环形回退；它不消耗搜索节点。无效计数或工作区不足抛出 `std::invalid_argument`。`takePathGoalTask()` 核对任务身份、状态及输出后领取一次结果；`adoptPathGoalTask()` 再次核对全部身份字段、数量和零搜索消耗，缺失或拒绝结果时用相同自有输入及独立工作区同步计算。

`PathGoalCandidateRunner` 归会话所有。`generate()` 在原决策点完整领取当前任务，再继续处理下一项依赖请求；析构停止并回收工作线程。`Shadow` 模式返回原同步候选，`Parallel` 模式采用经核对的任务结果或同步回退结果。`PathRequestQueue::pumpWithCandidates()` 是显式接入入口，原同步入口保留。环形候选、连通块缓存、A*、FIFO、预算和世界提交继续在原所有者执行。

运行时设置通过 `CONCORD_PATH_CANDIDATE_MODE=reference|shadow|parallel` 与 `CONCORD_PATH_CANDIDATE_WORKERS=0|1|2|4` 读取。默认 `reference` 不创建任务池，启用其他模式时工作数缺省为 1；0 在调用线程执行。统计中的计算与等待耗时只用于诊断，不作为玩法决策输入。所选内核并行尚无足够收益证据支持改变普通运行默认值。

## 辅助动画帧选择

[RuntimeAuxiliaryAnimation.h](src/render/buildings/RuntimeAuxiliaryAnimation.h) 提供无需资源读取或窗口状态的帧索引计算：

```cpp
int runtimeAuxiliaryAnimationFrame(
    const ArtType* art, int frameCount, bool mainFramesOnlyFallback,
    int animationTick, int fixedTickHz);
```

`art` 为同步借用的可空指针，函数不修改美术配置。帧数小于等于 1 时返回 0；其余输入返回有效帧索引。起点优先使用 `LoopStart`，其次是 `Start`；`LoopEnd` 为排他的结束索引，`End` 则按既有约定表示相对于 `Start`（缺省时相对于已解析起点）的帧数。未指定终点时，`mainFramesOnlyFallback` 仅在总帧数为偶数时取前半段。

`PingPong` 的返回段不重复两个端点，播放速率通过 [RuntimeRuleTime.h](src/runtime/timing/RuntimeRuleTime.h) 换算为模拟 tick。零或负动画 tick 保留既有的首次推进行为；范围加法和往返周期使用较宽整数中间值。渲染器通过这一接口选择辅助动画帧。输入与预期序列见 [辅助动画合同](tests/rendering/RuntimeAuxiliaryAnimationContract.cpp)。

## 视野与战斗反应

[RuntimeVisionSystem](src/simulation/vision/RuntimeVision.h) 管理观察者视野，开火揭示接口为：

```cpp
bool notifyWeaponFired(const WorldObjectInstance& attacker,
    const WorldObjectInstance& target, const WeaponType& weapon,
    int tick, int fixedTickHz);
std::string serializeFireReveals(int tick) const;
bool restoreFireReveals(const std::string& text, int tick, int fixedTickHz);
```

调用前需已建立观察者与地图视野。符合 `RevealOnFire` 条件的开火，在受击方视野中临时显示射击位置周围半径 3 格的圆形区域，持续 5 个模拟秒；不会写入永久探索记录，也不提供隐形探测。相同观察者、相同中心的记录只延长期限，不因较早的重复通知缩短；不同中心各自到期。非法坐标、无效对象、未启用 `RevealOnFire` 等情况返回 `false`。返回 `true` 表示通知符合登记条件；同一 tick 的重复通知也可能返回成功，不保证像素或期限发生变化。具体实现见 [RuntimeFireReveal.inl](src/simulation/combat/targeting/RuntimeFireReveal.inl)。

恢复先校验整个文本，再替换揭示集合；失败保留原集合。成功后使视野缓存失效，下一次完整 `update()` 清除被替换记录留下的旧覆盖。等级视野只读取当前等级对应的能力集合，`Sight` 倍率应用一次；重复能力不会叠乘，结果保留缺省、整数截断及半径上限策略。

恐慌逻辑仍是宿主内部接口，见 [RuntimePanic.inl](src/simulation/combat/damage/RuntimePanic.inl)：`beginRuntimePanic()` 记录或刷新反应，`advanceRuntimePanics()` 在模拟边界处理计时和命令，`finishRuntimePanicMovementLeg()` 处理移动段结束。它们同步访问宿主状态，不是独立线程安全服务。临时反应保留 5 个模拟秒时长，永久反应不会被普通伤害降级；暂停条件、确定性移动游标、姿态和等级速度倍率沿用现有规则。

对应运行时检查位于 [开火揭示诊断](src/simulation/combat/targeting/diagnostics/RuntimeFireRevealSmoke.inl)、[恐慌诊断](src/simulation/combat/damage/diagnostics/RuntimePanicSmoke.inl) 和 [视野合同实现](src/simulation/vision/RuntimeVision.cpp)。

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

## 有界任务执行器

[BoundedTaskExecutor.h](src/core/tasks/BoundedTaskExecutor.h) 位于 `concord::tasks`，只依赖标准库。`OwnedBytes` 是精确长度、仅可移动的独占存储；`Task` 携带输入、输出和临时工作区字节数、非零 generation 及无捕获 `JobKernel` 函数指针。

内核签名为 `std::size_t (*)(const JobContext&)`，返回实际输出长度。`JobContext` 提供只读输入、私有输出与 scratch span、任务键及 `stopRequested()`。这些借用只在本次内核调用期间有效；内核必须遵守不外部分配、不访问外部可写状态、不等待同池任务的约定。接口不接收 live World、共享缓存或可捕获的上下文，但它不是对任意内核的操作系统内存隔离。

| 操作 | 约定 |
| --- | --- |
| `submit(Task&)` | 接纳时才移动输入并预留 input、output、scratch 的全部预算；拒绝保留调用方输入。`workers=0` 在提交线程内执行完才返回。 |
| `tryTake(JobKey)`、`waitTake(JobKey)` | 返回 `Taken`、`NotReady`、`UnknownJob`、`StaleGeneration` 或 `WouldDeadlock`。原任务键只能成功领取一次，成功输出转为调用方所有。 |
| `cancel(JobKey)` | 合作取消排队、运行或已完成但未领取的任务；不强行终止没有返回的内核。 |
| `advanceGeneration()` | 只接受严格递增的非零代号；旧代结果失效，运行中的存储保留到内核实际返回。 |
| `requestStop()`、`stopAndJoin()` | 停止接纳并取消未领取结果；join 完成后才能释放运行存储。自身内核调用 `stopAndJoin()` 返回 false，不阻塞或 detach。 |
| `stats()` | 查询工作数、队列、在途与峰值、预留字节和固定元数据大小。 |

默认配置为 0 个 worker、队列 16、在途 20、payload 16 MiB。硬上限分别为 32 个 worker、4,096 个队列或任务槽和 1 GiB payload；队列不能超过在途上限。已完成但未领取的结果仍占预算。调用方未提交的输入、已领取结果、线程栈、CRT 和分配器支撑开销不计入 payload，不能把它当作整个进程的内存上限。

异常及非法输出长度转换为有界 `Failed` 结果，不发布部分输出或保留 `exception_ptr`。同池内核递归提交返回 `Reentrant`，等待返回 `WouldDeadlock`；所有者必须保证执行器比内核和外部调用线程长寿，内核销毁自己的执行器属于违反所有权约定。

## 巨炮光栅任务与预热所有权

[GrandCannonRasterTask.h](src/render/voxel/GrandCannonRasterTask.h) 的 `concord::grand_cannon::raster(const tasks::JobContext&)` 处理巨炮炮塔已经排序、投影的面数据，生成材质、像素、阴影、深度等平面。`Input`、`Face`、`Shadow` 是内部数值输入；`Output` 后接材质平面及声明顺序中的九个字节平面。该布局用于同一实现的内部任务交换，不是存档或网络协议。

资源选择、VXL/HVA 解码、投影和炮管 point 算法仍在渲染所有者执行。[RuntimeGrandCannonPreparedFacing](src/render/voxel/RuntimeGrandCannonPreparedFacing.h) 在所有者线程准备私有数据、交出 `Task`，并在核对结果后接收和安装；资源、规则、美术、剧场与共享缓存指针不进入任务内核。

[RuntimeGrandCannonPrewarmJob](src/render/voxel/RuntimeGrandCannonPrewarm.h) 管理一次预热的完整生命周期：

- `prepare(context, cache, error)` 在创建任务的所有者上准备和提交；输入借用仅在同步准备时有效。
- `merge(cache, generation)` 领取已完成结果，`finish(cache, generation)` 等待并完成收集；所有者、代号、状态和输出长度都需匹配。
- `stop()` 取消并回收工作，`done()`、`error()`、`metrics()` 提供状态及本次预热统计；销毁前执行停止和 join。

`done()` 在已停止时也返回 true，不能单凭该值判断所有朝向已安装；应结合错误信息和安装计数核对结果。

默认朝向先同步完成；其余朝向的炮塔光栅使用有界任务，两个部件都可用时才发布对应 ready bit。错误所有者、过期或重复结果不会安装，准备、启动或工作失败时保留已经验证的默认朝向；穷尽诊断仍要求完整输出，不能以回退替代成功。

执行器使用最多 4 个 worker、队列 4、在途 4、64 MiB 输入/输出/scratch 预算；准备输入另有 128 MiB 上限，图集及其缓存另预留 128 MiB。安装后缓存和工作区分别计费，线程支撑开销不算作任务 payload。

环境变量 `CONCORD_GRAND_CANNON_PREWARM_WORKERS` 接受 `0|1|2|4|auto`；非法值会报错。`workerBudget(requested, hardwareThreads)` 用于本消费者的预算，负数请求自动预算，预留两个逻辑处理器后夹到 1–4，未知数量取 1。普通运行默认仍为 1，穷尽预热诊断默认仍为 4；`0` 是实际同步参考模式。该选项不是全局模拟工作数，分析 CLI 的 `--workers` 也不用于控制它。

## 完整软件帧呈现

[SoftwareFrame.h](src/runtime/presentation/SoftwareFrame.h) 在 `concord::presentation` 命名空间定义 `SoftwareFrameView`、`FramePresenter` 及其结果和统计类型。公共接口复用 `ConstMenuPixelSurfaceView`，不包含原生窗口或设备句柄。

`SoftwareFrameView` 是已完成的客户区图像：顶向 BGRA8，包含尺寸、字节步长、容量和像素代际，以及 `sequence`、`sceneVersion`、`simulationTick`、`content`。第四字节完整保留，由软件后端忽略，不执行预乘或 alpha 混合。像素仅在 `present()` 调用期间同步借用；调用前必须完成 CPU 与原生绘制，下一次写入、缩放或释放结束旧视图寿命。

每次提交使用新的非零 `sequence`。`Redrawn` 更新场景版本及其模拟 tick，`ReusedScene` 保留场景身份，界面叠加仍可更新。版本字段不推进模拟，也不构成保存状态。`FramePresenter::present()` 返回：

| 结果 | 含义 |
| --- | --- |
| `Presented` | 后端完成本次提交，增加成功计数 |
| `InvalidFrame` | 像素或帧元数据无效 |
| `Rejected` | 所属线程检查失败、序号重复或倒退，或计数已达上限 |
| `Failed` | 有效提交已尝试，但后端未成功完成 |

所属线程、有效性或序号检查拒绝时，不增加尝试数；进入后端后会记录尝试，即使提交失败。同一失败序号不能再次使用。`FramePresentStatistics` 分开保存尝试与成功次数及最后序号，本身不是屏幕扫描输出 FPS。

私有 [DibFramePresenterWin32](src/runtime/presentation/win32/FramePresenterWin32.h) 在 UI 所有者线程借用窗口 DC 和 [MainBackBufferWin32](src/ui/surface/win32/RuntimeMainBackBufferWin32.h)，检查像素存储身份与代际，并在原生复制及刷新成功后返回成功。主缓冲复用既有 [SkirmishBackBufferWin32](src/ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h) 所有权；写入、尺寸变化、交换及释放会使旧借用失效。

产品完整帧包含地图、UI、FPS 和软件鼠标。默认 GDI 路径通过私有窗口字体适配组成最终画面；D3D11 路径使用独立、不可激活且输入穿透的私有字体目标，只将对应 HUD 区域复制进最终 CPU 帧，GPU 活跃时不向同一游戏窗口做 GDI 绘制。两者都在完整帧准备结束后才同步借出像素。资源不足或后端失败时由宿主执行软件回退，回退单独计数，不伪造成功帧视图。可选 SDL3 菜单使用同一公共帧协议，通过位图文字与 CPU 绘图形成菜单画面，再交给其私有软件呈现实现。

## 本机后端选项与 D3D11 呈现

[FrameBackendOptions.h](src/runtime/presentation/FrameBackendOptions.h) 在 `concord::presentation` 定义纯值类型，不依赖 Windows、D3D 或 SDL 头文件：

```cpp
enum class FrameBackend { Software, D3D11 };
enum class FrameDevice { Hardware, Warp };
struct FrameBackendOptions {
    FrameBackend backend = FrameBackend::Software;
    FrameDevice device = FrameDevice::Hardware;
    FrameBackend menuBackend = FrameBackend::Software;
    FrameDevice menuDevice = FrameDevice::Hardware;
};
```

选项只控制当前进程的显示路径，不写入 `RuntimeConfig`、存档、网络协议或哈希。`backend / device` 选择战局完整 CPU 帧呈现，`menuBackend / menuDevice` 独立选择 Win32 遭遇战菜单绘制，二者都默认使用软件路径。原 `runRuntimeWindow` 两种签名保持；新的四参数重载接收配置、加载后退出标记、联机桥接所有权与呈现选项。软件路径为默认，硬件请求不会自动改为 WARP；SDL3 菜单与 D3D11／WARP 的组合由启动入口明确拒绝。

私有 [D3D11FramePresenterWin32](src/runtime/presentation/win32/D3D11FramePresenterWin32.h) 实现同一 `FramePresenter` 协议。其工厂接收原生窗口、选项、统计对象和可选原生测试探针；只有 Windows 组装层可使用此头文件。`create()` 返回的对象可能已经处于软件回退状态，调用方必须检查 `active()` 和 `report()`，不能仅凭指针非空判断 D3D11 可用；分配或异常路径也可能返回空值。

设备、即时上下文、交换链、查询及 `Present` 均归单一 UI 线程。初始化请求最低功能级别 11_0，并检查 BGRA 格式与 timestamp/disjoint 查询资源。每帧使用调用方的真实行距上传到与客户区同尺寸的 BGRA8_UNORM 缓冲，再调用 `Present(1, 0)`；第四字节原样上传，显示时忽略。上述完整 CPU 帧上传路径不创建着色器、采样器或混合状态，不做 GPU 缩放、预乘或颜色空间转换；菜单直接绘制通过单独的 `presentMenu()` 入口使用离线着色器。

缩放先释放后台缓冲引用，再重建匹配尺寸；最小化时不提交，恢复后重新核对尺寸。设备、资源、上传或呈现失败会记录错误并释放全部 GPU 引用，宿主在当前窗口会话中转回原 GDI，不重新尝试 D3D11。`fallbackToSoftware()` 是这一单向清理入口；字体组成等原生互操作也留在此私有所有者内，公共帧与软件窗口接口不传递设备句柄。

`report()` 返回请求／实际后端、设备能力、回退原因、成功呈现和上传字节统计。`timings() / timingCount()` 提供最多 4,096 条有界样本；通过 8 组异步 timestamp/disjoint 查询单独记录 GPU 上传时间，并保留有效、无效、未就绪及丢弃情况。`recordCpuWork()` 补充 CPU 绘制与组成时间，`enableTimingOutput()` 控制逐条输出。数据借用不应超过呈现器生命周期，Present 的 CPU 调用时间不等于 GPU 时间或实际屏幕帧率。

`D3D11FrameProbeWin32` 仅用于原生合同的故障注入和上传读回，不通过用户配置或命令行公开；正常呈现不执行诊断 staging／Map 读回。构建入口见 [D3D11FramePresenter.cmake](cmake/D3D11FramePresenter.cmake)，测试源码见 [D3D11FramePresenterWin32Contract.cpp](tests/infrastructure/D3D11FramePresenterWin32Contract.cpp)。该测试使用自编像素，不需要游戏素材，但需要真实 Windows 窗口和图形设备环境。

## 菜单绘制命令与 GPU 提交

[MenuDrawCommands.h](src/ui/drawing/MenuDrawCommands.h) 在 `concord::ui` 中定义有序纯值命令，不包含原生窗口、设备句柄或活动世界对象。`MenuDrawKind` 包括 `Fill / Darken / Bgra / Scale / Indexed / Glyph`，每条 `MenuDrawCommand` 为 64 字节，保存矩形、资源偏移、步长、尺寸、调色板与标志等值。

`MenuDrawList` 拥有命令数组和资源字节，限定最多 16,384 条命令与 32 MiB 资源。`begin()` 开始新一帧命令并更新命令修订号；`reset()` 清空资源和命令并更新代际。`intern()` 同时检查输入地址、长度及已复制的字节，原地等长更新不会继续使用旧内容；空资源、预算超限等情况会抛出相应标准异常。`fill() / darken() / bgra() / image()` 记录命令，`valid()` 检查命令类型、范围和资源边界。

`MenuDrawFrame` 是对列表的同步只读借用，携带序号、客户区尺寸、视口和捕获时的 `generation / resourceRevision / commandRevision`。列表及其资源必须存活到 `presentMenu()` 返回，期间不得修改；借用后修改命令或资源会使旧帧失效。该接口不转移资源所有权，也不用于跨线程共享实时菜单状态。

[MenuImage](src/assets/menu/MenuImage.h) 在保留原 BGRA 软件参考数据的同时，持有已有解码器生成的局部索引、画布偏移、调色板和透明索引策略。[recordSkirmishBitmapText()](src/ui/text/RuntimeSkirmishBitmapText.h) 沿用原字体布局、回退、裁剪及整段阴影先于正文的顺序，将 packed glyph 字节记录为菜单命令；设备端不引入另一套资源解码或系统字体实现。

私有 [D3D11FramePresenterWin32::presentMenu()](src/runtime/presentation/win32/D3D11FramePresenterWin32.h) 复用现有窗口所属线程的 device、context、交换链及 Present。它拒绝错线程、无效或陈旧资源、失效修订号、重复或倒退序号等输入；返回 `bool` 表示本次提交是否成功，具体回退和计数由 `report()` 读取。此入口独立于接收 `SoftwareFrameView` 的完整 CPU 帧提交。

[MenuRasterD3D11.inl](src/runtime/presentation/win32/MenuRasterD3D11.inl) 准备稳定有序的 tile 命令索引和资源；[MenuRaster.hlsl](src/runtime/presentation/win32/MenuRaster.hlsl) 通过全屏三角形的像素着色器按原次序光栅。索引 0 的透明判断发生在 remap 之前，255 仍是有效索引；图片、字形、暗化与混色使用原整数与舍入规则，客户区映射沿用既有像素行为。正常路径不生成完整 CPU 菜单帧，不做诊断 staging／Map 读回。

计时样本的 `menuRaster` 区分菜单光栅与完整帧上传；菜单 GPU 查询覆盖 Draw 光栅区间，CPU 提交包含资源、命令和 tile 准备及上传，不能按战局上传的 GPU 时间解释。`menuDraws / menuRejected` 单独记录菜单成功与拒绝情况。故障清理遵循现有软件回退规则，释放绑定与资源后才转回 GDI。

[runRuntimeSkirmishMenu()](src/ui/skirmish/RuntimeSkirmishMenu.h) 保留原两参数形式，并提供带 `FrameBackendOptions` 的三参数重载。选定大厅、选图及转场可使用 GPU；进入其他页面会在当前窗口会话中回到原软件路径。关闭时释放 GPU 所有者，再保存菜单缓存；菜单选择仍由原控制器与配置映射提交，关闭下拉后的命中检测会忽略已失效的旧弹出项。

着色器由 [D3D11FramePresenter.cmake](cmake/D3D11FramePresenter.cmake) 使用所选 SDK 的 FXC 离线生成，采用 Shader Model 5 与 `/WX /Ges /O3`，生成头位于构建目录并私有嵌入运行程序。源码包只提供 HLSL 和生成规则，不提供编译后的字节码或着色器编译器 DLL。原生合同见 [MenuRasterD3D11Contract.cpp](tests/infrastructure/MenuRasterD3D11Contract.cpp)，构建及显式启用方法见 [README](README.md#遭遇战菜单-gpu-绘制)。

## CPU 性能采样与隔离设备诊断

[RuntimeLiveFrameBenchmark.h](src/runtime/performance/diagnostics/RuntimeLiveFrameBenchmark.h) 提供生产循环中的可选测量，不用墙钟时间决定模拟 tick 数量。`CONCORD_LIVE_BENCHMARK=1` 开启测量，`CONCORD_LIVE_BENCHMARK_VISIBLE=1` 请求可见运行，`CONCORD_LIVE_BENCHMARK_RAW_SAMPLES=1` 追加有界原始样本。`CONCORD_LIVE_BENCHMARK_TICKS` 默认 600，合法数值限制在 60–216,000；测量之前保留原 60 tick 暖场。

计时分别记录模拟、CPU paint、完整帧准备与实际后端的 CPU 提交、提交间隔和会话进入到 ready；报告记录实际 software、D3D11 或发生过切换的 mixed 路径。当前提交计时覆盖完整帧准备与最终提交，比旧版仅统计原生复制的范围更大。整段时间样本最多 4,096 个、分段最多 512 个，分位数与样本是否截断必须一起理解。CPU 提交或窗口观察结果不等同于屏幕扫描输出帧率，也不等同于游戏 GPU 时间。

[性能工具](tools/benchmarks) 中新增的 ES 模块脚本使用 Node.js 内置模块记录输入身份、分轮采样并从原始流复算分位数；基线和诊断脚本按公开默认 Release 构建目录校验部署来源，输出必须使用新的工程内目录。工具的实际资源输入仍需自行准备，建议输出统一放在 `build/`。

[设备探针](tools/diagnostics) 是独立程序，检查 CPU/图形设备、纹理格式、上传读回与 timestamp 能力。游戏产品中的 D3D11、DXGI、DWM 依赖由私有呈现后端接入，不进入独立核心或公共接口。环境采集、进程路径及原始输出只供本机分析，不随源码包发布。设备探针只反映其检查范围；生产菜单的 GPU 绘制和战局完整帧呈现分别通过各自入口选择与验证。

## 模块库与独立合同

[LeafModuleLibraries.cmake](cmake/LeafModuleLibraries.cmake) 为 CSF 与 INI 两份实现建立五个单源 `STATIC` 变体：

| CMake 目标 | 实现 | 消费者编译约定 |
| --- | --- | --- |
| `concord_csf_runtime_leaf` | `CsfCodec.cpp` | 游戏运行时 |
| `concord_csf_contract_leaf` | `CsfCodec.cpp` | CSF 独立合同测试 |
| `concord_ini_runtime_leaf` | `IniFile.cpp` | 游戏运行时 |
| `concord_ini_tools_leaf` | `IniFile.cpp` | 规则与分析工具 |
| `concord_ini_contract_leaf` | `IniFile.cpp` | 普通独立合同测试 |

编译选项、定义和 CRT 设置均为 `PRIVATE`，各变体继承对应消费者约定；运行时 Debug 的 `/MD`、iterator level 0 与工具/普通合同 Debug 的 `/MDd`、默认 iterator level 2 分开保留。消费者仍配置自己的 `src/` 头文件搜索目录。上述五个变体继续保留；[ProfiledLeaf.cmake](cmake/ProfiledLeaf.cmake) 提供共用工厂，窗口、菜单、资源、传输及核心模块按各自实际消费者建立静态库。

[ProductionCoreLibraries.cmake](cmake/ProductionCoreLibraries.cmake) 复用实际世界、地图、规则、INI、游戏命令、比赛设置、会话与锁步实现，配合 [SnapshotTransferLibraries.cmake](cmake/SnapshotTransferLibraries.cmake)、[CompatibilityFingerprintLibraries.cmake](cmake/CompatibilityFingerprintLibraries.cmake) 和 [LanTransportProviders.cmake](cmake/LanTransportProviders.cmake) 管理共有实现。完整产品使用 `concord_runtime` 作为运行时编译约定所有者；独立核心使用真实的生产世界哈希入口，不创建空游戏目标。

根配置选项 `CONCORD_PORTABLE_CORE` 控制所选核心目标与测试，完整产品和核心缓存不得混用。`concord_core_check` 构建并串行执行 10 项必需测试，具体名称以 [CoreTestSuite.cmake](cmake/CoreTestSuite.cmake) 为准。当前已验证 Windows Release、Debug 配置；核心不包含窗口和设备后端，但原子文件与 LAN 保留私有系统实现。完整产品及核心操作示例见 [README](README.md#编译)。

角色、计分、解码、预览会话、回放、输入、UI 时钟及地形标准检查见 [TestsUnitAndNetwork.cmake](cmake/TestsUnitAndNetwork.cmake)；回放测试目标定义见 [ReplaySnapshotContracts.cmake](cmake/ReplaySnapshotContracts.cmake)。地形与 TMP 的独立构建配置见 [NavigationTerrainQueryContracts.cmake](cmake/NavigationTerrainQueryContracts.cmake)、[TmpTerrainContracts.cmake](cmake/TmpTerrainContracts.cmake)，资源运行检查见 [TestsRuntimeRegression.cmake](cmake/TestsRuntimeRegression.cmake)。

指针、光标、取消捕获及相应 Windows 集成检查见 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake)。这些检查按具体 CMake 目标和 CTest 名称选择，未自动并入 `windows-minimal` 的 12 项或 `windows-local-assets` 的 4 项集合；标准合同、素材检查、原生适配与真实网络运行的结果须分别判断，不能相互替代。

经济与传送的标准目标定义见 [RuntimeSmokeTargets.cmake](cmake/RuntimeSmokeTargets.cmake)，它们直接编译相应生产实现。`chronosphere_relocation_contract_test`、`house_economy_context_contract_test` 在 [TestsAdditionalRegression.cmake](cmake/TestsAdditionalRegression.cmake) 注册；`computer_mining_codec_contract_test` 和 `economy_production_fixed_step_contract_test` 在 [TestsUnitAndNetwork.cmake](cmake/TestsUnitAndNetwork.cmake) 注册。后两项读取 [采矿保存夹具](tests/fixtures/runtime_computer_mining_codec) 和 [固定步夹具](tests/fixtures/runtime_economy_production_fixed_step)，比较完整预期文本；公开构建入口只读取这些夹具。

定时炸弹、寻路候选、生产规则和地形帧的新增独立与对照测试源码见 [README 的测试说明](README.md#测试与开发)。其中寻路队列对照已注册为 `path_goal_candidate_queue_differential_test`，其余五份测试仍未注册为工程的 CMake/CTest 目标；开发时使用的私人验证脚本、旧版对照副本和执行记录不随源码发布。

视频服务、LAN 地图传输、回放封包与恢复一致性分别注册为 `bink_playback_service_contract_test`、`runtime_lobby_map_transfer_contract_test`、`match_replay_envelope_contract_test`、`runtime_recovery_consistency_contract_test`。前者在不支持的平台使用跳过返回码；跳过不能计作通过。这些检查未并入固定的 12＋4 测试集合。

[TaskExecutorContracts.cmake](cmake/TaskExecutorContracts.cmake) 与 [GrandCannonPrewarmContracts.cmake](cmake/GrandCannonPrewarmContracts.cmake) 接入执行器、光栅与预热生产库，保留原根目录 include 顺序。新执行器和光栅合同在独立子目录作用域中使用相同生产源码；完整产品继承对应 Runtime 编译约定。`bounded_task_executor_contract_test` 和 `grand_cannon_raster_contract_test` 不需要游戏素材，`runtime_grand_cannon_prewarm_contract_test` 使用本机素材进行集成核对，注册条件及运行方法见 [README](README.md#测试与开发)。

[结构检查入口](tools/testing/Run-StructureChecks.ps1) 接收已有 Release 构建目录和明确的 Python 路径；[检查器](tools/structure/structure_guard.py)、[精确例外基线](tools/structure/structure-baseline.json) 与 [合成夹具](tools/structure/test_structure_guard.py) 共同检查依赖、平台类型、目标归属和测试注册。接口与运行条件见 [README](README.md#测试与开发)，无需复制私人开发记录。

[PathGoalCandidateTaskContracts.cmake](cmake/PathGoalCandidateTaskContracts.cmake) 接入候选任务库、纯任务合同与完整队列对照，保留各消费者的编译约定。[SoftwareFrameContracts.cmake](cmake/SoftwareFrameContracts.cmake) 接入公共软件帧合同及私有 Windows 呈现库；[SkirmishBackBufferWin32Contracts.cmake](cmake/SkirmishBackBufferWin32Contracts.cmake) 同时接入主缓冲合同。相关五项 CTest 名称与平台要求见 [README 的测试说明](README.md#测试与开发)，均未自动并入 12＋4 集合或核心固定 10 项集合。

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
| `--skirmish-window <win32\|sdl3>` | 选择独立遭遇战菜单窗口，默认 Win32；战局窗口保持 Win32 |
| `--frame-backend <software\|d3d11\|d3d11-warp>` | 选择主 Win32 窗口的完整 CPU 帧呈现方式，默认 software |
| `--skirmish-drawing <software\|d3d11\|d3d11-warp>` | 独立选择 Win32 大厅与选图页的绘制方式，默认 software |
| `--smoke-test`                                        | 加载启动数据并输出报告，不打开窗口 |

`--frame-backend` 的硬件与 WARP 选项需要匹配的 Windows 图形能力；失败时记录原因并回退软件路径。`--skirmish-window sdl3` 与任一 D3D11／WARP 战局呈现或菜单绘制选项组合时直接报错。用法见 [README](README.md#主战局呈现后端)。

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