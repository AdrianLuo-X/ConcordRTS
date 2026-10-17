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
| 预览资源会话 | 资源上下文、缓存和显式刷新 | [MapPreviewResourceContext.h](src/assets/preview/MapPreviewResourceContext.h)、[MapPreviewExporter.h](src/assets/preview/MapPreviewExporter.h) |
| 回放事件类型 | 独立事件数据及读取、汇总入口 | [RuntimeReplayEventSnapshot.h](src/runtime/contracts/RuntimeReplayEventSnapshot.h)、[ReplaySandbox.h](src/analysis/sandbox/ReplaySandbox.h) |
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

角色、计分、解码、预览会话和回放类型的独立检查见 [TestsUnitAndNetwork.cmake](cmake/TestsUnitAndNetwork.cmake)；回放测试目标定义见 [ReplaySnapshotContracts.cmake](cmake/ReplaySnapshotContracts.cmake)。新增检查按具体 CMake 目标和 CTest 名称选择，未自动并入 `windows-minimal` 的 12 项或 `windows-local-assets` 的 4 项集合。

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