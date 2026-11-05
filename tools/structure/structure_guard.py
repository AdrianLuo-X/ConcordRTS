"""Local structural regression guard. Static evidence only; no CI or source writes.

A reviewed exact exception ledger is input, never enlarged by this program.
The graph is lexical (all conditional branches), not a C++ preprocessor.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict, deque
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys

CPP_EXT = {".h", ".hpp", ".cpp", ".inl", ".inc"}
TOKEN = re.compile(
    r'(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\([\s\S]*?\)(?P=delimiter)")'
    r'|(?P<string>"(?:\\[\s\S]|[^"\\])*")'
    r"|(?P<char>'(?:\\[\s\S]|[^'\\])*')"
    r'|(?P<comment>//[^\n]*|/\*[\s\S]*?\*/)')
INCLUDE = re.compile(r'(?m)^[ \t]*#[ \t]*include[ \t]*(?:"([^"\n]+)"|<([^>\n]+)>)')
PLATFORM_HEADER = re.compile(
    r'(?i)^(windows|windowsx|wingdi|winuser|windef|winnt|winbase|objbase|ole2|'
    r'commctrl|commdlg|shellapi|shlobj|mmsystem|mmdeviceapi|audioclient|'
    r'xaudio2|xapobase|xapo|d3d\w*|dxgi\w*|d2d\w*|dwrite|wincodec|'
    r'winsock2?|ws2tcpip|schannel|security|sspi|wincrypt|bcrypt|psapi|dbghelp|'
    r'processthreadsapi|synchapi|profileapi|intrin)\.h$')
PLATFORM_TYPE = re.compile(
    r'\b(?:HWND|HDC|HBITMAP|HFONT|HBRUSH|HPEN|HGDIOBJ|HICON|HCURSOR|HMODULE|'
    r'HANDLE|HINSTANCE|HMONITOR|HPALETTE|HGLOBAL|HKEY|HRESULT|WPARAM|LPARAM|'
    r'LRESULT|WNDPROC|WNDCLASSEXW|WNDCLASSW|MSG|PAINTSTRUCT|BITMAPINFO|'
    r'POINT|RECT|SIZE|COLORREF|DWORD|DWORD_PTR|UINT_PTR|LONG_PTR|ULONGLONG|'
    r'LARGE_INTEGER|SOCKET|WSADATA|PCCERT_CONTEXT|HCERTCHAINENGINE|'
    r'CredHandle|CtxtHandle|IXAudio2\w*|ID3D\w*|IDXGI\w*)\b')
OPAQUE = re.compile(r'\b(void\s*\*|(?:std::)?uintptr_t)\s*(?:const\s+)?([A-Za-z_]\w*)')
BUSINESS_LIB = re.compile(
    r'(?i)\b(user32|gdi32|gdi32full|msimg32|comdlg32|comctl32|winmm|'
    r'xaudio2(?:_9)?|xapobase|d3d11|dxgi|dwrite|windowscodecs|ws2_32|'
    r'secur32|crypt32|bcrypt|shell32|shlwapi|dwmapi)\.lib\b')
FRONTEND_FLAG = re.compile(r'(?<!\w)(/[A-Za-z][A-Za-z0-9:,+-]*|-W[\w=-]+|-f[\w=-]+)')
VS_PATH = re.compile(r'(?i)[A-Z]:[\\/][^"\r\n]*(?:Visual[ /]Studio|Microsoft[ /]Visual[ /]Studio)[^"\r\n]*')
FORBIDDEN_PREFIXES = ("src/runtime/host/", "src/ui/")
SOFTWARE_FRAME_VALUES = {
    "src/ui/surface/RuntimeMenuPixelSurface.h",
    "src/ui/input/RuntimeInputPixelValues.h",
    "src/ui/values/RuntimeColorValues.h",
}
MENU_DRAW_VALUES = SOFTWARE_FRAME_VALUES | {
    "src/ui/drawing/MenuDrawCommands.h",
    "src/assets/menu/MenuImage.h",
    "src/assets/codecs/RuntimeTmpTypes.h",
}
MAP_DRAW_VALUES = {"src/render/core/MapDrawCommands.h", "src/render/core/RuntimeMapPixelRules.h",
                   "src/render/core/MapSpriteMaterial.h",
                   "src/ui/input/RuntimeInputPixelValues.h", "src/ui/values/RuntimeColorValues.h"}
DEFAULT_MODULES = {
    "superweapon-production": ["src/simulation/superweapons/SuperWeaponSystem.h", "src/simulation/superweapons/SuperWeaponSystem.cpp", "src/simulation/superweapons/RuntimeChronosphereRelocation.cpp", "src/simulation/superweapons/private/SuperWeaponObjectCellKey.h"],
    "computer-tactics-codec": ["src/simulation/ai/RuntimeComputerTacticsCodec.h", "src/simulation/ai/RuntimeComputerTacticsCodec.cpp", "src/core/io/RuntimeSaveTextFields.h"],
    "service-console": ["src/network/internet/RuntimeInternetServiceProcess.h", "src/network/internet/private/RuntimeInternetServiceConsoleControl.h"],
    "lan-nonce-noise-source": ["src/network/session/private/RuntimeLanNonceNoiseSource.h"],
    "service-main-random": ["src/network/internet/private/RuntimeInternetServiceRandomBytes.h"],
    "tunnel-prelude-source": ["src/network/transport/private/RuntimeTunnelPreludeSource.h"],
    "service-nonce-source": ["src/network/transport/private/RuntimeInternetServiceNonceSource.h"],
    "lan-menu-secure-source": ["src/ui/network/private/RuntimeLanMenuSecureSource.h"],
    "internet-menu-entropy-source": ["src/ui/network/private/RuntimeInternetMenuEntropySource.h"],
    "system-secure-bytes": ["src/core/security/RuntimeSecureBytes.h"],
    "credential-certificate-private": [],
    "tls-certificate-contract": ["src/network/transport/RuntimeInternetTlsCredentials.h", "src/network/transport/RuntimeInternetTlsTransport.h", "src/network/internet/RuntimeInternetServiceHostConfig.h"],
    "video-caller-private": [],
    "bink-video-parent": ["src/ui/main_menu/RuntimeMainMenu.h"],
    "async-sound-device": ["src/audio/backend/RuntimeAsyncSoundDevice.h", "src/audio/backend/RuntimeUnsupportedAsyncSoundDevice.cpp"],
    "menu-sound-device": ["src/audio/events/RuntimeMenuSoundDevice.h", "src/audio/events/RuntimeUnsupportedMenuSoundDevice.cpp"],
    "campaign-map-audio-device": ["src/audio/menu/RuntimeCampaignMapAudioDevice.h", "src/audio/menu/RuntimeUnsupportedCampaignMapAudioDevice.cpp"],
    "main-music-device": ["src/audio/menu/RuntimeMainMenuMusicDevice.h", "src/audio/menu/RuntimeUnsupportedMainMenuMusicDevice.cpp"],
    "legacy-wave-device": ["src/audio/backend/RuntimeLegacyWaveDevice.h", "src/audio/backend/RuntimeUnsupportedLegacyWaveDevice.cpp"],
    "map-drawing-interface": ["src/render/core/RuntimeMapRenderer.h"],
    "radar-pixel-store": ["src/ui/radar/RuntimeRadarPixelStore.h"],
    "internet-skin-drawing": ["src/ui/network/RuntimeInternetMenuSkin.h"],
    "runtime-mci-device-private": [],
    "world-audio-device-private": [],
    "runtime-voice-path": ["src/audio/backend/RuntimeVoicePath.h"],
    "campaign-menu-surface-owner": [],
    "main-menu-surface-owner": [],
    "hud-private-drawing": [],  # Existing Win32 backend headers are not public frame contracts.
    "hud-system-font": ["src/ui/text/RuntimeHudSystemFont.h"],
    "campaign-score-system-font": ["src/ui/text/RuntimeCampaignScoreSystemFont.h"],
    "match-result-system-font": ["src/ui/text/RuntimeMatchResultSystemFont.h"],
    "campaign-system-font": ["src/ui/text/RuntimeCampaignSystemFont.h"],
    "skirmish-system-font-values": ["src/ui/text/RuntimeSkirmishSystemFont.h"],
    "main-system-font": ["src/ui/text/RuntimeMainSystemFont.h"],
    "menu-bmp-reader": ["src/assets/menu/MenuBmpReader.h"],
    "result-bitmap-text": ["src/ui/text/RuntimeResultBitmapText.h", "src/ui/text/RuntimeResultBitmapText.cpp"],
    "main-bitmap-text": ["src/ui/text/RuntimeMainBitmapText.h", "src/ui/text/RuntimeMainBitmapText.cpp"],
    "persistent-menu-parent": ["src/runtime/window/RuntimeMenuParentHost.h", "src/runtime/window/RuntimeMenuParentHost.cpp"],
    "internet-menu-controller": ["src/runtime/menu/RuntimeInternetMenuController.h", "src/runtime/menu/RuntimeInternetMenuController.cpp", "src/runtime/contracts/RuntimeInternetMenuData.h"],
    "lan-menu-controller": ["src/runtime/menu/RuntimeLanMenuController.h", "src/runtime/menu/RuntimeLanMenuController.cpp", "src/runtime/contracts/RuntimeMultiplayerMenuTypes.h"],
    "campaign-menu-controller": ["src/runtime/menu/RuntimeCampaignMenuController.h", "src/runtime/menu/RuntimeCampaignMenuController.cpp", "src/runtime/menu/RuntimeCampaignPresentationData.h", "src/runtime/contracts/RuntimeCampaignSelection.h"],
    "main-menu-controller": ["src/runtime/menu/RuntimeMainMenuController.h", "src/runtime/menu/RuntimeMainMenuController.cpp", "src/runtime/contracts/RuntimeMainMenuResult.h"],
    "startup-recovery-prompt": ["src/runtime/window/RuntimeStartupRecoveryPrompt.h"],
    "load-transaction": ["src/runtime/persistence/RuntimeLoadTransaction.h", "src/runtime/persistence/RuntimeLoadTransaction.cpp"],
    "trigger-domain-scheduler": ["src/world/values/RuntimeCellPoint.h", "src/simulation/triggers/RuntimeTriggerState.h", "src/simulation/triggers/RuntimeTriggerScheduler.h", "src/simulation/triggers/RuntimeTriggerScheduler.cpp"],
    "vision-authority-phase": ["src/simulation/vision/RuntimeVisionPhase.h", "src/simulation/vision/RuntimeVisionPhase.cpp"],
    "save-identity": ["src/runtime/persistence/RuntimeSaveIdentity.h", "src/runtime/persistence/RuntimeSaveIdentity.cpp"],
    "trigger-persistence": ["src/simulation/triggers/RuntimeTriggerPersistence.h", "src/simulation/triggers/RuntimeTriggerPersistence.cpp"],
    "save-document": ["src/runtime/persistence/RuntimeSaveDocument.h", "src/runtime/persistence/RuntimeSaveDocument.cpp"],
    "committed-ground-motion": ["src/simulation/movement/RuntimeObjectBehavior.h", "src/simulation/movement/ground/RuntimeCommittedGroundMotion.h", "src/simulation/movement/ground/RuntimeCommittedGroundMotion.cpp"],
    "open-transport-source": ["src/simulation/combat/targeting/RuntimeWeaponFireFlh.h", "src/simulation/combat/targeting/RuntimeOpenTransportMuzzle.h", "src/simulation/combat/targeting/RuntimeOpenTransportMuzzle.cpp"],
    "ai-trigger-definition-hash": ["src/simulation/triggers/RuntimeCampaignAiTriggerDefinition.h", "src/core/determinism/RuntimeAuthorityHashPrimitives.h", "src/simulation/ai/RuntimeComputerAiTriggerHash.h", "src/simulation/ai/RuntimeComputerAiTriggerHash.cpp", "src/simulation/ai/RuntimeComputerTacticsHash.inl"],
    "radar-cell-colour-values": ["src/ui/radar/RuntimeRadarCellColors.h"],
    "mobile-sprite-bounds-query": ["src/render/geometry/RuntimeMobileSpriteBoundsQuery.h"],
    "object-shp-bounds-query": ["src/render/geometry/RuntimeObjectShpBoundsQuery.h"],
    "spawned-missile-bounds-query": ["src/render/geometry/RuntimeSpawnedMissileBoundsQuery.h"],
    "voxel-muzzle-query": ["src/render/geometry/RuntimeVoxelMuzzleQuery.h"],
    "structure-muzzle-query": ["src/render/geometry/RuntimeStructureMuzzleQuery.h"],
    "marine-muzzle-query": ["src/render/geometry/RuntimeMarineMuzzleQuery.h"],
    "infantry-muzzle-query": ["src/render/geometry/RuntimeInfantryMuzzleQuery.h"],
    "map-surface-pick": ["src/render/geometry/RuntimeMapSurfacePick.h"],
    "turret-offset-return": ["src/render/geometry/RuntimeStructureTurretScreenOffset.h"],
    "structure-query-point": ["src/render/geometry/RuntimeStructureSpriteQuery.h"],
    "building-visible-viewport": ["src/render/geometry/RuntimeBuildingVisibleHitSpans.h"],
    "projectile-screen-query": ["src/render/geometry/RuntimeProjectileScreenPoint.h"],
    "body-anchor-query": ["src/render/geometry/RuntimeObjectBodyAnchor.h"],
    "geometry-scope-viewport": ["src/render/geometry/RuntimeGeometryQueryScope.h"],
    "hit-span-point-query": ["src/render/geometry/RuntimeSpriteHitSpanQueries.h"],
    "mobile-probe-values": ["src/render/geometry/RuntimeMobileSpriteBoundsProbe.h"],
    "render-placement-values": ["src/render/geometry/RuntimeObjectWorldRenderPlacement.h"],
    "render-colour-values": ["src/render/core/RuntimeMapColorValues.h"],
    "structure-geometry-values": ["src/render/geometry/RuntimeStructureSpriteGeometry.h"],
    "sprite-frame-values": ["src/render/core/RuntimeSpriteFrameImage.h"],
    "sprite-hit-span-value": ["src/render/geometry/RuntimeSpriteHitSpan.h"],
    "input-controllers": ['src/ui/input/RuntimeCameraKeyController.cpp', 'src/ui/input/RuntimeCameraKeyController.h', 'src/ui/input/RuntimeChatTextController.cpp', 'src/ui/input/RuntimeChatTextController.h', 'src/ui/input/RuntimeCursorController.cpp', 'src/ui/input/RuntimeCursorController.h', 'src/ui/input/RuntimeInputCancellationController.cpp', 'src/ui/input/RuntimeInputCancellationController.h', 'src/ui/input/RuntimeInputPixelValues.h', 'src/ui/input/RuntimeKeyboardIngressController.cpp', 'src/ui/input/RuntimeKeyboardIngressController.h', 'src/ui/input/RuntimeKeyboardRouterController.cpp', 'src/ui/input/RuntimeKeyboardRouterController.h', 'src/ui/input/RuntimeLocalNavigationController.cpp', 'src/ui/input/RuntimeLocalNavigationController.h', 'src/ui/input/RuntimeMapLeftPointerController.cpp', 'src/ui/input/RuntimeMapLeftPointerController.h', 'src/ui/input/RuntimeMapRightPointerController.cpp', 'src/ui/input/RuntimeMapRightPointerController.h', 'src/ui/input/RuntimeMenuKeyController.cpp', 'src/ui/input/RuntimeMenuKeyController.h', 'src/ui/input/RuntimeMenuValues.h', 'src/ui/input/RuntimeObserver.h', 'src/ui/input/RuntimePlanningFactoryKeyController.cpp', 'src/ui/input/RuntimePlanningFactoryKeyController.h', 'src/ui/input/RuntimePlayerCommands.h', 'src/ui/input/RuntimePlayerCommunication.h', 'src/ui/input/RuntimePlayerDefeatNotice.h', 'src/ui/input/RuntimePlayerWaypoints.h', 'src/ui/input/RuntimePointerIngressController.cpp', 'src/ui/input/RuntimePointerIngressController.h', 'src/ui/input/RuntimeReferenceCommunicationController.cpp', 'src/ui/input/RuntimeReferenceCommunicationController.h', 'src/ui/input/RuntimeReferencePresentationController.cpp', 'src/ui/input/RuntimeReferencePresentationController.h', 'src/ui/input/RuntimeSelectionKeyController.cpp', 'src/ui/input/RuntimeSelectionKeyController.h', 'src/ui/input/RuntimeSessionKeyController.cpp', 'src/ui/input/RuntimeSessionKeyController.h', 'src/ui/input/RuntimeTeamKeyController.cpp', 'src/ui/input/RuntimeTeamKeyController.h'],
    "audio-diagnostic-interface": ["src/audio/backend/RuntimeVoiceAudio.h"],
    "host-loop-port": ["src/runtime/timing/RuntimeHostLoopPort.h"],
    "timer-resolution-scope": ["src/runtime/timing/RuntimeTimerResolutionScope.h"],
    "process-performance-policy": ["src/runtime/performance/RuntimeProcessPerformanceScope.h"],
    "presentation-clock": ["src/ui/time/RuntimeUiClock.h"],
    "atomic-file-interface": ["src/core/io/RuntimeAtomicFile.h"],
    "menu-resource-watch": ["src/ui/resources/RuntimeMenuResourceWatch.h"],
    "movie-path": ["src/runtime/campaign/RuntimeCampaignMovieResolver.h", "src/runtime/campaign/RuntimeCampaignMovieResolver.cpp",
                   "src/assets/video/BinkPlaybackPaths.h", "src/assets/video/BinkPlaybackPaths.cpp",
                   "src/assets/archive/MixArchive.h", "src/assets/archive/MixArchive.cpp",
                   "src/assets/archive/WestwoodCrypto.h", "src/assets/archive/WestwoodCrypto.cpp",
                   "src/assets/archive/WestwoodBlowfishConstants.inc"],
    "audio-path": ["src/assets/audio/AudioSampleResolver.h", "src/assets/audio/AudioSampleResolver.cpp",
                   "src/assets/audio/AudioDecoder.h", "src/assets/audio/AudioDecoder.cpp",
                   "src/assets/archive/MixArchive.h", "src/assets/archive/MixArchive.cpp",
                   "src/assets/archive/WestwoodCrypto.h", "src/assets/archive/WestwoodCrypto.cpp",
                   "src/assets/archive/WestwoodBlowfishConstants.inc",
                   "src/core/io/IniFile.h", "src/core/io/IniFile.cpp", "src/core/io/RuntimeIniTextReadBuffer.h"],
    "resource-path": ["src/runtime/campaign/RuntimeCampaignDataSource.h", "src/runtime/campaign/RuntimeCampaignDataSource.cpp",
                      "src/assets/archive/MixArchive.h", "src/assets/archive/MixArchive.cpp",
                      "src/assets/archive/WestwoodCrypto.h", "src/assets/archive/WestwoodCrypto.cpp",
                      "src/assets/archive/WestwoodBlowfishConstants.inc",
                      "src/core/io/IniFile.h", "src/core/io/IniFile.cpp", "src/core/io/RuntimeIniTextReadBuffer.h"],
    "file-path": ["src/core/io/IniFile.h", "src/core/io/IniFile.cpp",
                  "src/runtime/config/RuntimeConfigPaths.h", "src/runtime/config/RuntimeConfigPaths.cpp"],
    "map-tile-preparation": ["src/render/preparation/MapTilePreparation.h", "src/render/preparation/MapTilePreparation.cpp"],
    "map-draw": ["src/render/core/MapDrawCommands.h", "src/render/core/RuntimeMapPixelRules.h"],
    "menu-draw": ["src/ui/drawing/MenuDrawCommands.h"],
    "software-window": ["src/runtime/window/RuntimeSoftwareWindow.h"],
    "software-frame": ["src/runtime/presentation/SoftwareFrame.h", "src/runtime/presentation/FrameBackendOptions.h"],
    "path-candidate-task": ["src/simulation/navigation/pathfinding/PathGoalCandidateTask.h","src/simulation/navigation/pathfinding/PathGoalCandidateTask.cpp","src/simulation/navigation/pathfinding/PathGoalCandidates.h","src/simulation/navigation/pathfinding/PathGoalCandidates.cpp"],
    "bounded-tasks": ["src/core/tasks/BoundedTaskExecutor.h",
                      "src/core/tasks/BoundedTaskExecutor.cpp"],
    "menu-image": ["src/assets/menu/MenuImage.h", "src/assets/menu/MenuImage.cpp"],
    "menu-image-port": ["src/assets/menu/MenuImageResources.h"],
    "Westwood-font": ["src/assets/text/WestwoodFontCodec.h",
                      "src/assets/text/WestwoodFontCodec.cpp"],
    "window-lifecycle": ["src/runtime/window/RuntimeWindowLifecycle.h",
                         "src/runtime/window/RuntimeWindowLifecycle.cpp"],
    "CSF": ["src/assets/text/CsfCodec.h", "src/assets/text/CsfCodec.cpp"],
    "PAL": ["src/assets/codecs/PalDecoder.h", "src/assets/codecs/PalDecoder.cpp"],
    "SHP": ["src/assets/codecs/ShpCodec.h", "src/assets/codecs/ShpCodec.cpp"],
    "seat-role": ["src/core/players/PlayerSlotRole.h"],
    "score": ["src/core/results/RuntimeMatchScore.h", "src/core/results/RuntimeMatchScore.cpp"],
    "replay-DTO": ["src/runtime/contracts/RuntimeReplayEventSnapshot.h"],
    "economy-step": ["src/simulation/economy/RuntimeEconomyFixedStep.h",
                     "src/simulation/economy/RuntimeEconomyFixedStep.cpp"],
    "production-step": ["src/simulation/production/RuntimeProductionFixedStep.h",
                        "src/simulation/production/RuntimeProductionFixedStep.cpp"],
    "terrain-frame": ["src/render/core/RuntimeMapTerrainFrame.h",
                      "src/render/core/RuntimeMapTerrainFrame.cpp"],
}

def blank(value):
    return re.sub(r'[^\n]', ' ', value)

def cpp_views(text):
    """Preserve newlines/offsets; comments and literals cannot invent dependencies."""
    no_comments = TOKEN.sub(lambda m: blank(m[0]) if m.lastgroup == "comment" else m[0], text)
    code = TOKEN.sub(lambda m: blank(m[0]), text)
    return no_comments, code

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()

def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))

def relative(path, root):
    return Path(path).resolve().relative_to(root).as_posix()

def closure(graph, start):
    paths = {start: [start]}
    todo = deque([start])
    while todo:
        source = todo.popleft()
        for target in graph.get(source, ()):
            if target not in paths:
                paths[target] = paths[source] + [target]
                todo.append(target)
    return paths

def cmake_commands(text):
    """Read balanced commands, quoted/bracket arguments and CMake comments."""
    # Preserve quoted hashes; bracket comments are removed as one token.
    lex = re.compile(r'(?P<quoted>"(?:\\[\s\S]|[^"\\])*")|'
                     r'(?P<bracket>\[(?P<eq>=*)\[[\s\S]*?\](?P=eq)\])|'
                     r'(?P<block>#\[(?P<ceq>=*)\[[\s\S]*?\](?P=ceq)\])|'
                     r'(?P<comment>#[^\n]*)')
    clean = lex.sub(lambda m: blank(m[0]) if m.lastgroup in ("comment", "block") else m[0], text)
    spans = {m.start(): m.end() for m in lex.finditer(clean)
             if m.lastgroup in ("quoted", "bracket")}
    result = []
    pattern = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
    pos = 0
    while match := pattern.search(clean, pos):
        i, depth = match.end(), 1
        while i < len(clean) and depth:
            if i in spans:
                i = spans[i]
                continue
            if clean[i] == '(':
                depth += 1
            elif clean[i] == ')':
                depth -= 1
            i += 1
        if depth:
            raise ValueError("Unbalanced CMake command: " + match[1])
        result.append((match[1].lower(), clean[match.end():i-1],
                       clean.count("\n", 0, match.start()) + 1))
        pos = i
    return result

def target_model(inventory):
    model_file = Path(inventory["fileApi"]["codemodelFile"])
    model = read_json(model_file)
    config = next(c for c in model["configurations"]
                  if c["name"] == inventory["configuration"])
    targets = {}
    for item in config["targets"]:
        target = read_json(model_file.parent / item["jsonFile"])
        targets[target["name"]] = target
    return targets

WORLD_AUDIO_CLOCK_INPUTS = {
    "src/simulation/superweapons/RuntimeSupportPowers.inl",
    "src/ui/selection/RuntimeChronosphereSelection.inl",
    "src/audio/events/RuntimeGattlingAudio.inl",
    "src/ui/selection/RuntimeChronosphereSelectionAudio.inl",
    "src/ui/selection/RuntimeChronosphereSelectionClockDefaults.inl",
    "src/audio/events/RuntimeWorldAudioClock.inl",
    "src/audio/events/RuntimeWorldAudioClockInput.inl",
}
WORLD_AUDIO_CLOCK_BYPASS = re.compile(
    r'\b(?:GetTickCount64|GetTickCount|QueryPerformanceCounter|timeGetTime|'
    r'GetSystemTimeAsFileTime|clock_gettime|runtimeWin32UiClock|runtimeUiClockNow)\s*\('
    r'|\bstd::chrono::(?:steady_clock|system_clock|high_resolution_clock)::now\s*\(')
WORLD_AUDIO_PHASE_CALL = re.compile(
    r'\b(?:advanceRuntime(?:Credit|SuperWeaponBuilding|ChronosphereSelection|DiskDrain|'
    r'DiskMove|GattlingWeapon|ObjectAmbient|AircraftAux|TurretSound)Audio|'
    r'startRuntimeChronosphereSelectionAudio|manageRuntimeGattlingWeaponReport)\s*\(')

def skirmish_presentation_clock_bypasses(name, code):
    if name != "src/ui/skirmish/RuntimeSkirmishMenu.cpp":
        return []
    results=[]
    pattern=r'(?m)^(?:std::uint64_t\s+MenuState::nowMs|LRESULT\s+CALLBACK\s+shellSkirmishWindowProc|RuntimeSkirmishSelection\s+runRuntimeSkirmishMenu)\s*\([^;{}]*\)\s*(?:const\s*)?\{'
    for method in re.finditer(pattern,code):
        cursor,depth=method.end(),1
        while cursor<len(code) and depth:
            if code[cursor]=="{":depth+=1
            elif code[cursor]=="}":depth-=1
            cursor+=1
        for match in WORLD_AUDIO_CLOCK_BYPASS.finditer(code,method.end(),cursor-1):
            if 'MenuState::nowMs' in method.group(0) and match.group(0).startswith('runtimeWin32UiClock'):
                continue
            results.append((match.group(0),code.count("\n",0,match.start())+1))
    return results

def campaign_presentation_clock_bypasses(name, code):
    if name not in ("src/ui/campaign/RuntimeCampaignMenu.cpp", "src/runtime/menu/RuntimeCampaignMenuController.cpp"):
        return []
    results=[]
    for method in re.finditer(r'(?m)^(?:bool|void|std::size_t|std::vector<unsigned char>)\s+(?:enterCampaignMapOverlayFade|refreshCampaignMapPresentation|updateCampaignMapPresentation|campaignMapAnimationFrameIndex|composeCampaignSurface|drawCampaignMapTargets|skipCampaignMapIntro)\s*\([^;{}]*\)\s*\{',code):
        cursor,depth=method.end(),1
        while cursor<len(code) and depth:
            if code[cursor]=="{":depth+=1
            elif code[cursor]=="}":depth-=1
            cursor+=1
        for match in WORLD_AUDIO_CLOCK_BYPASS.finditer(code,method.end(),cursor-1):
            results.append((match.group(0),code.count("\n",0,match.start())+1))
    return results

def main_presentation_clock_bypasses(name, code):
    if name not in ("src/ui/main_menu/RuntimeMainMenu.cpp", "src/runtime/menu/RuntimeMainMenuController.cpp"):
        return []
    results=[]
    for method in re.finditer(r'(?m)^(?:bool|void|RuntimeMainMenuResult)\s+(?:pollMainMenuVideo|startMainMenuVideo|initializeMenu|beginAction|updateDynamicState|runRuntimeMainMenu)\s*\([^;{}]*\)\s*\{',code):
        cursor,depth=method.end(),1
        while cursor<len(code) and depth:
            if code[cursor]=="{":depth+=1
            elif code[cursor]=="}":depth-=1
            cursor+=1
        for pattern in (WORLD_AUDIO_CLOCK_BYPASS,re.compile(r'\bstate\.audio\.clock\s*->\s*nowMs\s*\(')):
            for match in pattern.finditer(code,method.end(),cursor-1):
                results.append((match.group(0),code.count("\n",0,match.start())+1))
    return results

def internet_menu_clock_bypasses(name, code):
    if name != "src/ui/network/RuntimeInternetMultiplayerMenu.cpp":
        return []
    results=[]
    for method in re.finditer(r'\b(?:internetMenuNowMilliseconds|beginRoomRefresh|beginHost|joinSelectedRoom|beginQuickMatch|pollInternetMenu)\s*\([^;{}]*\)\s*\{',code):
        cursor,depth=method.end(),1
        while cursor<len(code) and depth:
            if code[cursor]=="{":depth+=1
            elif code[cursor]=="}":depth-=1
            cursor+=1
        for match in WORLD_AUDIO_CLOCK_BYPASS.finditer(code,method.end(),cursor-1):
            results.append((match.group(0),code.count("\n",0,match.start())+1))
    return results

def main_menu_music_clock_bypasses(name, code):
    if name != "src/ui/main_menu/RuntimeMainMenu.cpp":
        return []
    owner=re.search(r'\bstruct\s+MenuAudio\s*\{',code)
    if not owner:
        return []
    end,depth=owner.end(),1
    while end<len(code) and depth:
        if code[end]=="{":depth+=1
        elif code[end]=="}":depth-=1
        end+=1
    findings=[]
    for method in re.finditer(r'\bvoid\s+(?:startMusic|ensureMusicPlaying)\s*\([^)]*\)\s*\{',code[owner.end():end-1]):
        begin=owner.end()+method.end();cursor,depth=begin,1
        while cursor<end and depth:
            if code[cursor]=="{":depth+=1
            elif code[cursor]=="}":depth-=1
            cursor+=1
        for match in WORLD_AUDIO_CLOCK_BYPASS.finditer(code,begin,cursor-1):
            findings.append((match.group(0),code.count("\n",0,match.start())+1))
    return findings

def menu_sfx_clock_bypasses(name, code):
    if name != "src/audio/events/RuntimeMenuSoundMci.inl":
        return []
    findings=[]
    for method in re.finditer(r'\bvoid\s+collect\s*\(\s*\)\s*\{',code):
        cursor,depth=method.end(),1
        while cursor<len(code) and depth:
            if code[cursor]=="{":depth+=1
            elif code[cursor]=="}":depth-=1
            cursor+=1
        for match in WORLD_AUDIO_CLOCK_BYPASS.finditer(code,method.end(),cursor-1):
            findings.append((match.group(0),code.count("\n",0,match.start())+1))
    return findings

def world_audio_clock_bypasses(name, code):
    if name in WORLD_AUDIO_CLOCK_INPUTS:
        return [(m.group(0), code.count("\n",0,m.start())+1)
                for m in WORLD_AUDIO_CLOCK_BYPASS.finditer(code)]
    if name != "src/runtime/host/RuntimeShell.cpp":
        return []
    results=[]
    for call in WORLD_AUDIO_PHASE_CALL.finditer(code):
        cursor,depth=call.end(),1
        while cursor<len(code) and depth:
            if code[cursor]=="(":depth+=1
            elif code[cursor]==")":depth-=1
            cursor+=1
        for m in WORLD_AUDIO_CLOCK_BYPASS.finditer(code,call.end(),cursor-1):
            results.append((call.group(0).split("(")[0].strip()+":"+m.group(0),
                            code.count("\n",0,m.start())+1))
    return results

def host_loop_owner_bypasses(name, code):
    if name != "src/runtime/host/RuntimeShell.cpp":
        return []
    results=[]
    for method in re.finditer(r'\bint\s+runRuntimeWindowSession\s*\([^;{}]*\)\s*\{',code):
        cursor,depth=method.end(),1
        while cursor<len(code) and depth:
            if code[cursor]=="{":depth+=1
            elif code[cursor]=="}":depth-=1
            cursor+=1
        pattern=r'\b(?:PeekMessageW|TranslateMessage|DispatchMessageW|MsgWaitForMultipleObjectsEx)\s*\(|std::this_thread::sleep_for\s*\(|windowHost\.(?:drainMessages|sleepForIdleSlice)\s*\('
        for match in re.finditer(pattern,code[method.end():cursor-1]):
            results.append((match.group(0),code.count("\n",0,method.end()+match.start())+1))
    return results

def scan(root, build, inventory, trace, modules=None):
    root, build = Path(root).resolve(), Path(build).resolve()
    modules = DEFAULT_MODULES if modules is None else modules
    facts = {}
    def add(rule, path, detail, count=1, line=None, witness=None):
        key = rule + "\0" + path + "\0" + detail
        if key not in facts:
            facts[key] = dict(rule=rule, path=path, detail=detail,
                              occurrences=0, line=line, witness=witness)
        facts[key]["occurrences"] += count

    files = {}
    for directory in ("src", "tests", "tools"):
        for path in sorted((root / directory).rglob("*")):
            if path.suffix.lower() in CPP_EXT and path.is_file():
                if path.is_symlink():
                    raise ValueError("Linked source is not supported: " + str(path))
                files[relative(path, root)] = path
    graph = defaultdict(set)
    edges, unresolved, platform = [], [], defaultdict(list)
    code_views = {}
    for name, path in files.items():
        text = path.read_text(encoding="utf-8-sig")
        comments, code = cpp_views(text)
        code_views[name] = code
        if name == "src/audio/backend/RuntimeVoiceAudio.h" and not re.search(r"\bstd::int32_t\s+lastError\s*\(\s*\)\s*const\s*;", code):
            add("audio-diagnostic-width", name, "std::int32_t lastError() const")
        if name == "src/render/geometry/RuntimeGeometryQueryScope.h":
            if not re.search(r"RuntimeGeometryQueryScope\s*\(\s*const\s+GameWorld\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelRect\s*&\s*\w+", code):
                add("geometry-scope-viewport-value", name, "canonical signed32 viewport by const reference")
        if name == "src/render/geometry/RuntimeSpriteHitSpanQueries.h":
            if not re.search(r"\bbool\s+runtimeSpriteHitSpansContain\s*\(\s*const\s+std::vector\s*<\s*RuntimeSpriteHitSpan\s*>\s*&\s*\w+\s*,\s*RuntimeInputPixelPoint\s+\w+\s*\)\s*;", code):
                add("hit-span-query-point-value", name, "canonical signed32 pixel point by value and borrowed const spans")
        if name == "src/render/geometry/RuntimeMobileSpriteBoundsProbe.h":
            for value_type, field in (("RuntimeInputPixelRect", "spriteBounds"), ("RuntimeInputPixelRect", "renderedBounds"), ("RuntimeInputPixelPoint", "imageOrigin")):
                if not re.search(r"\b"+value_type+r"\s+"+field+r"\s*\{\s*\}\s*;", code):
                    add("mobile-probe-pixel-values", name, value_type+" "+field)
        if name == "src/render/geometry/RuntimeObjectBodyAnchor.h":
            if not re.search(r"\bRuntimeInputPixelPoint\s+runtimeObjectBodyAnchorForRender\s*\(\s*const\s+WorldObjectInstance\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelRect\s*&\s*\w+", code):
                add("body-anchor-pixel-values", name, "same body anchor query with canonical pixel return and borrowed viewport")
        if name == "src/render/geometry/RuntimeProjectileScreenPoint.h":
            if not re.search(r"\bRuntimeInputPixelPoint\s+runtimeProjectileActorScreenPoint\s*\(\s*const\s+RuntimeProjectileRenderActor\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelRect\s*&\s*\w+", code):
                add("projectile-screen-pixel-values", name, "same projectile screen query with canonical pixel return and borrowed viewport")
        if name == "src/render/geometry/RuntimeBuildingVisibleHitSpans.h":
            if not re.search(r"\bbool\s+resolveRuntimeBuildingVisibleHitSpans\s*\(\s*const\s+GameWorld\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelRect\s*&\s*\w+", code):
                add("visible-spans-viewport-value", name, "canonical borrowed viewport for the same visible-span producer")
            if not re.search(r"std::unordered_map\s*<\s*int\s*,\s*std::vector\s*<\s*RuntimeSpriteHitSpan\s*>\s*>\s*&\s*\w+", code):
                add("visible-spans-output-reference", name, "existing owned span map passed by mutable reference")
        if name == "src/render/geometry/RuntimeStructureSpriteQuery.h":
            if not re.search(r"\bbool\s+resolveRuntimeStructureSpriteGeometry\s*\(\s*const\s+WorldObjectInstance\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelPoint\s*&\s*\w+", code):
                add("structure-query-center-value", name, "canonical borrowed center for the same structure query")
            if not re.search(r"RuntimeStructureSpriteGeometry\s*&\s*\w+", code):
                add("structure-query-output-reference", name, "existing owned geometry passed by mutable reference")
        if name == "src/render/geometry/RuntimeStructureTurretScreenOffset.h":
            if not re.search(r"\bRuntimeInputPixelPoint\s+runtimeStructureTurretAnimScreenOffsetForRender\s*\(", code):
                add("turret-offset-return-value", name, "canonical pixel return from the same original producer")
            if not re.search(r"runtimeStructureTurretAnimScreenOffsetForRender\s*\(\s*const\s+BuildingType\s*&\s*\w+\s*,\s*const\s+std::string\s*&\s*\w+\s*,\s*int\s+\w+\s*,\s*int\s+\w+\s*,\s*const\s+RuntimeMapCamera\s*&\s*\w+\s*\)", code):
                add("turret-offset-source-signature", name, "unchanged original five borrowed/value source arguments")
        if name == "src/render/geometry/RuntimeMapSurfacePick.h":
            if not re.search(r"struct\s+RuntimeMapSurfaceCell\s*\{\s*std::int32_t\s+x\s*=\s*0\s*;\s*std::int32_t\s+y\s*=\s*0\s*;\s*\}", code):
                add("surface-pick-logical-cell-value", name, "distinct signed32 logical cell with zero defaults")
            if not re.search(r"pickRuntimeMapSurfaceCell\s*\(\s*const\s+GameWorld\s*&\s*\w+\s*,\s*const\s+RuntimeMapRenderContext\s*&\s*\w+\s*,\s*const\s+RuntimeMapCamera\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelRect\s*&\s*\w+\s*,\s*RuntimeInputPixelPoint\s+\w+\s*,\s*RuntimeMapRenderCache\s*&\s*\w+\s*,\s*RuntimeMapSurfaceCell\s*&\s*\w+\s*\)", code):
                add("surface-pick-unit-borrow-signature", name, "pixel viewport borrow, pixel input by value, distinct logical output borrow")
        if name == "src/render/geometry/RuntimeInfantryMuzzleQuery.h":
            if not re.search(r"\bbool\s+resolveRuntimeInfantryWeaponMuzzlePoint\s*\(\s*const\s+WorldObjectInstance\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelPoint\s*&\s*\w+\s*,\s*const\s+RuntimeMapRenderContext\s*&\s*\w+\s*,\s*const\s+RuntimeMapCamera\s*&\s*\w+\s*,\s*RuntimeMapRenderCache\s*&\s*\w+\s*,\s*RuntimeInputPixelPoint\s*&\s*\w+\s*\)", code):
                add("infantry-muzzle-pixel-borrow-signature", name, "original six borrowed arguments with canonical const center and mutable result")
        if name == "src/render/geometry/RuntimeMarineMuzzleQuery.h":
            if not re.search(r"\bbool\s+resolveRuntimeMarineWeaponMuzzlePoint\s*\(\s*const\s+WorldObjectInstance\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelPoint\s*&\s*\w+\s*,\s*const\s+RuntimeMapRenderContext\s*&\s*\w+\s*,\s*const\s+RuntimeMapCamera\s*&\s*\w+\s*,\s*RuntimeMapRenderCache\s*&\s*\w+\s*,\s*RuntimeInputPixelPoint\s*&\s*\w+\s*\)", code):
                add("infantry-muzzle-pixel-borrow-signature", name, "original six borrowed arguments with canonical const center and mutable result")
        if name == "src/render/geometry/RuntimeStructureMuzzleQuery.h":
            if not re.search(r"\bbool\s+resolveRuntimeStructureWeaponMuzzlePoint\s*\(\s*const\s+WorldObjectInstance\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelPoint\s*&\s*\w+\s*,\s*const\s+RuntimeMapRenderContext\s*&\s*\w+\s*,\s*const\s+RuntimeMapCamera\s*&\s*\w+\s*,\s*RuntimeMapRenderCache\s*&\s*\w+\s*,(?:\s*int\s+\w+\s*,){4}\s*bool\s+\w+\s*,\s*RuntimeInputPixelPoint\s*&\s*\w+\s*\)", code):
                add("infantry-muzzle-pixel-borrow-signature", name, "original six borrowed arguments with canonical const center and mutable result")
        if name == "src/render/geometry/RuntimeVoxelMuzzleQuery.h":
            if not re.search(r"\bbool\s+resolveRuntimeVoxelWeaponMuzzlePoint\s*\(\s*const\s+WorldObjectInstance\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelPoint\s*&\s*\w+\s*,\s*const\s+RuntimeMapRenderContext\s*&\s*\w+\s*,\s*const\s+RuntimeMapCamera\s*&\s*\w+\s*,\s*RuntimeMapRenderCache\s*&\s*\w+\s*,(?:\s*int\s+\w+\s*,){4}\s*RuntimeInputPixelPoint\s*&\s*\w+\s*,\s*bool\s+\w+\s*=\s*true\s*\)", code):
                add("voxel-muzzle-pixel-borrow-default-signature", name, "original eleven arguments, point before final bool default true")
        if name == "src/render/geometry/RuntimeSpawnedMissileBoundsQuery.h":
            if not re.search(r"\bbool\s+resolveRuntimeSpawnedMissileSpriteBounds\s*\(\s*const\s+WorldObjectInstance\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelPoint\s*&\s*\w+\s*,\s*const\s+RuntimeMapRenderContext\s*&\s*\w+\s*,\s*const\s+RuntimeMapCamera\s*&\s*\w+\s*,\s*RuntimeMapRenderCache\s*&\s*\w+\s*,\s*RuntimeInputPixelRect\s*&\s*\w+\s*\)", code):
                add("spawned-missile-bounds-pixel-borrow-signature", name, "original six borrowed arguments and canonical point/rectangle")
        if name == "src/render/geometry/RuntimeObjectShpBoundsQuery.h":
            if not re.search(r"\bbool\s+resolveRuntimeObjectShpSpriteBounds\s*\(\s*const\s+WorldObjectInstance\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelPoint\s*&\s*\w+\s*,\s*const\s+RuntimeMapRenderContext\s*&\s*\w+\s*,\s*const\s+RuntimeMapCamera\s*&\s*\w+\s*,\s*RuntimeMapRenderCache\s*&\s*\w+\s*,\s*RuntimeInputPixelRect\s*&\s*\w+\s*,\s*bool\s+\w+\s*=\s*false\s*\)", code):
                add("object-shp-bounds-pixel-borrow-default-signature", name, "original seven arguments, canonical point/rectangle and final bool default false")
        if name == "src/render/geometry/RuntimeMobileSpriteBoundsQuery.h":
            if not re.search(r"\bbool\s+resolveRuntimeMobileObjectSpriteBounds\s*\(\s*const\s+WorldObjectInstance\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelPoint\s*&\s*\w+\s*,\s*const\s+RuntimeMapRenderContext\s*&\s*\w+\s*,\s*const\s+RuntimeMapCamera\s*&\s*\w+\s*,\s*RuntimeMapRenderCache\s*&\s*\w+\s*,\s*RuntimeInputPixelRect\s*&\s*\w+\s*\)", code):
                add("mobile-sprite-bounds-pixel-borrow-signature", name, "original six borrowed arguments with canonical signed32 point/rectangle")
        if name == "src/render/geometry/RuntimeObjectWorldRenderPlacement.h":
            if not re.search(r"\bRuntimeObjectWorldRenderPlacement\s+runtimeObjectWorldRenderPlacement\s*\(\s*const\s+WorldObjectInstance\s*&\s*\w+\s*,\s*const\s+GameWorld\s*&\s*\w+\s*,\s*const\s+RuntimeInputPixelRect\s*&\s*\w+", code):
                add("world-placement-viewport-value", name, "same placement producer with canonical signed32 viewport by const reference")
            for value_type, field in (("RuntimeInputPixelPoint", "center"), ("RuntimeInputPixelPoint", "sortPoint"), ("RuntimeInputPixelRect", "bodyViewport")):
                if not re.search(r"\b"+value_type+r"\s+"+field+r"\s*\{\s*\}\s*;", code):
                    add("render-placement-pixel-values", name, value_type+" "+field)
        if name == "src/ui/radar/RuntimeRadarCellColors.h":
            for detail, pattern in (
                ("dense full32 colour storage", r"\bRuntimeMapColorValue\s+colors\s*\[\s*side\s*\*\s*side\s*\]\s*;"),
                ("set full32 colour by value", r"\bvoid\s+setColor\s*\(\s*int\s+\w+\s*,\s*RuntimeMapColorValue\s+\w+\s*\)"),
                ("find full32 colour by mutable reference", r"\bbool\s+findColor\s*\(\s*int\s+\w+\s*,\s*RuntimeMapColorValue\s*&\s*\w+\s*\)\s*const"),
                ("fallback full32 colour storage", r"std::unordered_map\s*<\s*int\s*,\s*RuntimeMapColorValue\s*>\s+fallbackColors_\s*;"),
            ):
                if not re.search(pattern, code):
                    add("radar-cell-colour-full32-signature", name, detail)
        if name == "src/ui/campaign/RuntimeCampaignMenu.h":
            for entry in re.finditer(r'\brunRuntimeCampaignMenu\s*\(([^;{}]*(?:\{\})?[^;{}]*)\)\s*;', code):
                if not re.search(r'\bRuntimeWindowId\s+hostWindow\b', entry.group(1)):
                    add("campaign-menu-parent-identity", name, "Campaign and LoadGame use existing allocated RuntimeWindowId")
        if name == "src/ui/network/RuntimeMultiplayerMenu.h":
            for entry in re.finditer(r'\b(runRuntimeMultiplayerMenu|runRuntimeMultiplayerMenuUiSmoke|selectRuntimeMultiplayerResumeSave)\s*\(([^;{}]*(?:\{\})?[^;{}]*)\)\s*;', code):
                if not re.search(r'\bRuntimeWindowId\s+(?:hostWindow|owner)\b', entry.group(2)):
                    add("lan-menu-parent-identity", name, entry.group(1) + " uses existing allocated RuntimeWindowId")
        if name == "src/ui/network/RuntimeInternetMultiplayerMenu.h":
            for entry in re.finditer(r'\b(runRuntimeInternetMultiplayerMenu|runRuntimeInternetMultiplayerMenuUiSmoke)\s*\(([^;{}]*(?:\{\})?[^;{}]*)\)\s*;', code):
                if not re.search(r'\bRuntimeWindowId\s+hostWindow\b', entry.group(2)):
                    add("internet-menu-parent-identity", name, entry.group(1) + " uses existing allocated RuntimeWindowId")
        if name == "src/runtime/window/RuntimeMenuParentHost.cpp":
            policy = re.search(r'\bcloseRuntimeMenuParent\s*\([^{}]*\)\s*noexcept\s*\{([^{}]*)\}', code)
            body = policy.group(1) if policy else ""
            if not re.fullmatch(r'\s*port\.revokeParentBorrow\(\);\s*port\.destroyParentWindow\(\);\s*', body):
                add("persistent-parent-teardown-order", name, "revoke synchronous parent borrow before native destruction")
        if name == "src/ui/main_menu/RuntimeMainMenu.h":
            for entry in re.finditer(r'\b(?:run|prewarm)RuntimeMainMenu\s*\(([^;{}]*(?:\{\})?[^;{}]*)\)\s*;', code):
                if not re.search(r'\bRuntimeWindowId\s+hostWindow\b', entry.group(1)):
                    add("main-menu-parent-identity", name, "Main run/prewarm use existing allocated RuntimeWindowId")
        if name == "src/runtime/window/RuntimeStartupRecoveryPrompt.h":
            if not re.search(r"\busing\s+Request\s*=\s*bool\s*\(\s*\*\s*\)\s*\(\s*const\s+std::string\s*&\s*\)\s*;", code):
                add("startup-recovery-prompt-sync-borrow", name, "original message synchronously borrowed by typed callback")
            if not re.search(r"\bRequest\s+requestRetry\s*=\s*nullptr\s*;", code):
                add("startup-recovery-prompt-null-default", name, "typed callback defaults to null, no device owner")
        if name == "src/runtime/host/RuntimeMain.cpp":
            for method in re.finditer(r'\bint\s+runRuntimeWindowWithMultiplayerFailureRecovery\s*\([^;{}]*\)\s*\{', code):
                cursor, depth = method.end(), 1
                while cursor < len(code) and depth:
                    if code[cursor] == "{": depth += 1
                    elif code[cursor] == "}": depth -= 1
                    cursor += 1
                for native in re.finditer(r'\bMessageBox[AW]?\s*\(', code[method.end():cursor-1]):
                    add("startup-recovery-prompt-native-bypass", name, native.group(0), line=code.count("\n",0,method.end()+native.start())+1)
        if name == "src/render/core/RuntimeMapColorValues.h":
            if not re.search(r"\busing\s+RuntimeMapColorValue\s*=\s*std::uint32_t\s*;", code):
                add("render-colour-full32-value", name, "using RuntimeMapColorValue = std::uint32_t")
        if name == "src/render/geometry/RuntimeStructureSpriteGeometry.h":
            if not re.search(r"\bRuntimeInputPixelRect\s+bounds\s*\{\s*\}\s*;", code):
                add("structure-geometry-pixel-bounds", name, "RuntimeInputPixelRect bounds{}")
        if name == "src/render/core/RuntimeSpriteFrameImage.h":
            expected = (("RuntimeInputPixelRect", "spriteOpaqueBounds"), ("RuntimeInputPixelPoint", "infantryContact"))
            for value_type, field in expected:
                if not re.search(r"\b" + value_type + r"\s+" + field + r"\s*\{\s*\}\s*;", code):
                    add("sprite-frame-pixel-values", name, value_type + " " + field)
            if not re.search(r"\bstd::uint32_t\s+packedColorRemap\s*=\s*0x00ffffffU\s*;", code):
                add("sprite-frame-full32-key", name, "std::uint32_t packedColorRemap = 0x00ffffffU")
        if name == "src/render/geometry/RuntimeSpriteHitSpan.h":
            for field in ("y", "left", "right"):
                if not re.search(r"\bstd::int32_t\s+" + field + r"\s*=\s*0\s*;", code):
                    add("sprite-hit-span-width", name, "std::int32_t " + field + " = 0")
        for bypass, line in skirmish_presentation_clock_bypasses(name, code):
            add("skirmish-presentation-clock-bypass", name, bypass, line=line)
        for bypass, line in campaign_presentation_clock_bypasses(name, code):
            add("campaign-presentation-clock-bypass", name, bypass, line=line)
        for bypass, line in main_presentation_clock_bypasses(name, code):
            add("main-presentation-clock-bypass", name, bypass, line=line)
        for bypass, line in internet_menu_clock_bypasses(name, code):
            add("internet-menu-clock-bypass", name, bypass, line=line)
        for bypass, line in main_menu_music_clock_bypasses(name, code):
            add("main-menu-music-clock-bypass", name, bypass, line=line)
        for bypass, line in menu_sfx_clock_bypasses(name, code):
            add("menu-sfx-clock-bypass", name, bypass, line=line)
        if name == "src/runtime/host/RuntimeShell.cpp":
            for m in re.finditer(r"\b(?:timeBeginPeriod|timeEndPeriod)\s*\(", code):
                add("timer-resolution-owner-bypass", name, m.group(0), line=code.count("\n",0,m.start())+1)
        if name == "src/simulation/superweapons/SuperWeaponSystem.cpp":
            for m in re.finditer(r'\bSuperWeaponSystem\s*::\s*(runSmokeTest|runOneShotSmokeTest)\s*\(', code):
                add("superweapon-production-diagnostic-definition", name, m[1], line=code.count("\n",0,m.start())+1)
        if name == "src/simulation/ai/RuntimeComputerTacticsHash.inl" and "ai-trigger-definition-hash" in modules:
            if not re.fullmatch(r"\s*mixRuntimeComputerAiTriggerDefinitions\s*\(\s*hash\s*,\s*triggers\.computerAiTriggers\s*\)\s*;\s*", code):
                add("ai-trigger-hash-compatibility-algorithm", name, "legacy fragment must delegate to the canonical typed producer")
        if name == "src/simulation/movement/ground/diagnostics/RuntimeMoveToShroudSmoke.inl":
            if re.search(r'\bcellIndex512\s*=\s*runtimeCellKey\s*\(', code):
                add("move-shroud-fixture-terrain-index-domain", name,
                    "terrain indices use 512, not the existing 1000-column object/save key")
        for bypass, line in host_loop_owner_bypasses(name, code):
            add("host-loop-owner-bypass", name, bypass, line=line)
        for bypass, line in world_audio_clock_bypasses(name, code):
            add("world-audio-clock-bypass", name, bypass, line=line)
        # C/C++ line splicing precedes comments; keep physical lines in evidence.
        spliced, spliced_code = cpp_views(re.sub(r'\\\r?\n', '', text))
        for match in INCLUDE.finditer(spliced):
            if not re.match(r'^[ \t]*#', spliced_code[match.start():]):
                continue
            token = (match[1] or match[2]).replace("\\", "/")
            local = match[1] is not None
            line = spliced.count("\n", 0, match.start()) + 1
            candidates = [path.parent / token, root / "src" / token, root / token]
            found = next((p.resolve() for p in candidates if p.is_file()
                          and p.resolve().is_relative_to(root)), None)
            if found:
                target = relative(found, root)
                graph[name].add(target)
                edges.append(dict(source=name, target=target, token=token, line=line))
                if target.endswith(".cpp"):
                    add("cpp-text-include", name, target, line=line)
                a, b = name.split("/")[1], target.split("/")[1]
                if (a in ("core", "assets", "data", "world", "simulation", "network")
                    and (target.startswith(FORBIDDEN_PREFIXES)
                         or (a in ("core", "assets", "simulation", "world")
                             and b == "render"))):
                    add("reverse-dependency", name, target, line=line)
            elif PLATFORM_HEADER.fullmatch(token.split("/")[-1]):
                platform[name].append("header:" + token.lower())
                add("platform-header", name, token.lower(), line=line)
            elif local:
                unresolved.append(dict(source=name, token=token, line=line))
                add("unresolved-local-include", name, token, line=line)
        for match in re.finditer(r'(?m)^[ \t]*#[ \t]*include[ \t]+([A-Za-z_]\w*)[^\n]*', spliced_code):
            add('macro-include-review', name, match[1])
        for token, count in Counter(PLATFORM_TYPE.findall(code)).items():
            platform[name].append("type:" + token)
            add("platform-type", name, token, count)
        for kind, variable in OPAQUE.findall(code):
            # Every untyped pointer is visible in the report. Public native
            # names also remain detectable when a void* is replaced by uintptr_t.
            if path.suffix.lower() in (".h", ".hpp") or re.search(
                    r'(?i)native|window|handle|certificate|context|hdc', variable):
                detail = re.sub(r'\s+', '', kind) + ":" + variable
                platform[name].append("opaque:" + detail)
                add("opaque-native", name, detail)
        for token, count in Counter(re.findall(r'\b(?:RuntimeWindowState|WindowState)\b', code)).items():
            add("host-state-token", name, token, count)
        if name == "src/analysis/sandbox/ReplaySandbox.cpp":
            for line in comments.splitlines():
                if "runtime_replay_events_command.txt" in line:
                    add("legacy-replay-discovery", name, line.strip())

    if "superweapon-production" in modules:
        production_reach = set(modules["superweapon-production"])
        for start in modules["superweapon-production"]:
            production_reach.update(closure(graph, start))
        for edge in edges:
            if (edge["source"] in production_reach
                    and edge["target"].startswith("src/simulation/superweapons/diagnostics/")):
                add("superweapon-production-diagnostic-include", edge["source"], edge["target"], line=edge["line"])

    transitive = []
    cpp_inventory = []
    for name in sorted(files):
        paths = closure(graph, name)
        reached_platform = [p for p in paths if p in platform]
        if name.endswith((".h", ".hpp")) and reached_platform:
            transitive.append(dict(header=name, platformPaths=[
                dict(path=p, witness=paths[p], facts=platform[p]) for p in reached_platform]))
        for target in graph.get(name, ()):
            if any(p in platform for p in closure(graph, target)):
                add("platform-edge", name, target, witness=paths[target])
        if name.endswith(".cpp"):
            diagnostics = [p for p in paths if "/diagnostics/" in p]
            business_inls = [p for p in paths if p.endswith(".inl") and "/diagnostics/" not in p]
            cpp_inventory.append(dict(cpp=name, dependencies=sorted(paths),
                                      diagnostics=sorted(diagnostics), businessInls=sorted(business_inls)))
            if name in ("src/runtime/host/RuntimeShell.cpp", "src/render/core/RuntimeMapRenderer.cpp"):
                for target in business_inls:
                    add("host-business-inl", name, target, witness=paths[target])
    for module, roots in modules.items():
        for start in roots:
            if start not in files:
                add("protected-module-missing", start, module)
                continue
            for target, witness in closure(graph, start).items():
                if module == "radar-cell-colour-values" and target not in set(roots) | {"src/render/core/RuntimeMapColorValues.h"}:
                    add("radar-cell-colour-dependency", start, target, witness=witness)
                if module == "lan-menu-controller":
                    allowed = set(roots) | {
                        "src/network/lobby/RuntimeMultiplayerLobbyFlow.h",
                        "src/network/lobby/RuntimeMultiplayerLobbyModel.h",
                        "src/network/lobby/RuntimeRoomPassword.h",
                        "src/network/lockstep/RuntimeMultiplayerGameplayCommand.h",
                        "src/network/lockstep/RuntimeMultiplayerLockstep.h",
                        "src/network/lockstep/RuntimeMultiplayerRuntimeBridge.h",
                        "src/network/maps/RuntimeMultiplayerMapTransfer.h",
                        "src/network/protocol/RuntimeMultiplayerLaunchConfig.h",
                        "src/network/protocol/RuntimeMultiplayerMatchSetup.h",
                        "src/network/protocol/RuntimeMultiplayerProtocol.h",
                        "src/network/session/RuntimeLanTransport.h",
                        "src/network/session/RuntimeMultiplayerLanSession.h",
                        "src/network/session/RuntimeMultiplayerSessionSystem.h",
                        "src/network/snapshots/RuntimeMultiplayerResumeSave.h",
                        "src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.h",
                        "src/runtime/config/RuntimeConfig.h",
                        "src/runtime/contracts/RuntimeSkirmishSelection.h"
                    }
                    if target not in allowed:
                        add("lan-menu-controller-dependency", start, target, witness=witness)
                    if target in ("src/runtime/menu/RuntimeLanMenuController.h", "src/runtime/menu/RuntimeLanMenuController.cpp") and re.search(r'\b(?:RuntimeMultiplayerMenuUiState|flowBackend|RuntimeLanMenuWidgetViewWin32)\b|\bRuntimeMultiplayerLobbyFlow\s+\w+\s*[;={]', code_views.get(target, "")):
                        add("lan-menu-controller-native-owner", start, target, witness=witness)
                if module == "internet-menu-controller":
                    allowed = set(roots) | {
                        "src/network/internet/RuntimeInternetCommunity.h",
                        "src/network/internet/RuntimeInternetLobbyFlow.h",
                        "src/network/internet/RuntimeInternetMatchmaking.h",
                        "src/network/internet/RuntimeInternetRankedMatch.h",
                        "src/network/lobby/RuntimeMultiplayerLobbyFlow.h",
                        "src/network/lobby/RuntimeMultiplayerLobbyModel.h",
                        "src/network/lockstep/RuntimeMultiplayerGameplayCommand.h",
                        "src/network/lockstep/RuntimeMultiplayerLockstep.h",
                        "src/network/lockstep/RuntimeMultiplayerRuntimeBridge.h",
                        "src/network/maps/RuntimeMultiplayerMapTransfer.h",
                        "src/network/protocol/RuntimeMultiplayerLaunchConfig.h",
                        "src/network/protocol/RuntimeMultiplayerMatchSetup.h",
                        "src/network/protocol/RuntimeMultiplayerProtocol.h",
                        "src/network/session/RuntimeLanTransport.h",
                        "src/network/session/RuntimeMultiplayerLanSession.h",
                        "src/network/session/RuntimeMultiplayerSessionSystem.h",
                        "src/network/snapshots/RuntimeMultiplayerResumeSave.h",
                        "src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.h",
                        "src/network/transport/RuntimeInternetConnectivity.h",
                        "src/network/transport/RuntimeInternetMatchmakingProtocol.h",
                        "src/network/transport/RuntimeInternetTcpTunnelProxy.h",
                        "src/runtime/config/RuntimeConfig.h",
                        "src/runtime/contracts/RuntimeMultiplayerMenuTypes.h",
                        "src/runtime/contracts/RuntimeSkirmishSelection.h",
                        "src/runtime/menu/RuntimeLanMenuController.h"
                    }
                    if target not in allowed:
                        add("internet-menu-controller-dependency", start, target, witness=witness)
                    if target in ("src/runtime/menu/RuntimeInternetMenuController.h", "src/runtime/menu/RuntimeInternetMenuController.cpp") and re.search(r'\b(?:RuntimeInternetMultiplayerMenuUiState|flowBackend|RuntimeInternetMenuWidgetViewWin32|RuntimeInternetMultiplayerServiceConfig|RuntimeInternetServiceTransport)\b|\b(?:RuntimeInternetLobbyFlow|RuntimeMultiplayerLobbyFlow)\s+\w+\s*[;={]', code_views.get(target, "")):
                        add("internet-menu-controller-native-owner", start, target, witness=witness)
                if module == "menu-bmp-reader":
                    allowed = set(roots) | {"src/assets/menu/MenuImage.h", "src/assets/codecs/RuntimeTmpTypes.h"}
                    if target not in allowed:
                        add("menu-bmp-reader-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:MenuState|NativeMenuState|RuntimeMapRenderContext|RuntimeMapRenderCache|GameWorld|WorldObjectInstance|MenuImageResources|Win32MenuBmpReader|thread_local)\b', code_views.get(target, "")):
                        add("menu-bmp-reader-owner-or-live-state", start, target, witness=witness)
                if module == "skirmish-system-font-values":
                    if target not in roots:
                        add("skirmish-system-font-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:MenuState|SkirmishSystemFontsWin32|GameWorld|RuntimeMapRenderContext|RuntimeMapRenderCache|thread_local)\b', code_views.get(target, "")):
                        add("skirmish-system-font-owner-or-live-state", start, target, witness=witness)
                if module == "map-drawing-interface":
                    if "/win32/" in target or target.startswith("src/runtime/host/"):
                        add("map-drawing-native-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:RuntimeMapDrawTargetWin32|drawRuntimeMapWin32|RuntimeAuxiliaryShpDrawTargetWin32|drawRuntimeAuxiliaryShpFrameWin32|RuntimeScreenShakeDrawTargetWin32|applyRuntimeScreenShakeWin32|RuntimeWindowState|RuntimeWindowHostWin32)\b', code_views.get(target, "")):
                        add("map-drawing-native-owner-or-host-state", start, target, witness=witness)
                if module == "radar-pixel-store":
                    allowed = set(roots) | SOFTWARE_FRAME_VALUES | {"src/render/core/RuntimeMapColorValues.h"}
                    if target not in allowed:
                        add("radar-pixel-store-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:RuntimeRadarPixelStore|RuntimeRadarPixelStoreGdiApi|RuntimeRadarPixelStoreObserver|GameWorld|RuntimeMapRenderContext|RuntimeMapRenderCache)\b', code_views.get(target, "")):
                        add("radar-pixel-store-native-owner-or-live-state", start, target, witness=witness)
                if module == "main-system-font":
                    allowed = set(roots) | {"src/ui/input/RuntimeInputPixelValues.h", "src/ui/values/RuntimeColorValues.h"}
                    if target not in allowed:
                        add("main-system-font-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:MenuState|NativeMenuState|RuntimeMapRenderContext|RuntimeMapRenderCache|GameWorld|WorldObjectInstance|RuntimeMainMenuController|MainSystemFontsWin32|MainVideoFooterFontWin32|drawMainStockVideoFooter|mainStockVideoFooterCapability|thread_local)\b', code_views.get(target, "")):
                        add("main-system-font-owner-or-live-state", start, target, witness=witness)
                if module == "campaign-system-font":
                    allowed = set(roots) | {"src/ui/input/RuntimeInputPixelValues.h", "src/ui/values/RuntimeColorValues.h"}
                    if target not in allowed:
                        add("campaign-system-font-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:MenuState|NativeMenuState|RuntimeMapRenderContext|RuntimeMapRenderCache|GameWorld|WorldObjectInstance|RuntimeCampaignMenuController|CampaignSystemFontsWin32|thread_local)\b', code_views.get(target, "")):
                        add("campaign-system-font-owner-or-live-state", start, target, witness=witness)
                if module == "match-result-system-font":
                    allowed = set(roots) | {"src/ui/input/RuntimeInputPixelValues.h", "src/ui/values/RuntimeColorValues.h"}
                    if target not in allowed:
                        add("match-result-system-font-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:MenuState|NativeMenuState|RuntimeMapRenderContext|RuntimeMapRenderCache|GameWorld|WorldObjectInstance|RuntimeWindowState|RuntimeResultBitFont|RuntimeCampaignMenuController|MatchResultSystemFontWin32|thread_local)\b', code_views.get(target, "")):
                        add("match-result-system-font-owner-or-live-state", start, target, witness=witness)
                if module == "hud-system-font":
                    allowed = set(roots) | {"src/ui/input/RuntimeInputPixelValues.h"}
                    if target not in allowed:
                        add("hud-system-font-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:RuntimeWindowState|GameWorld|WorldObjectInstance|RuntimeMapRenderContext|RuntimeMapRenderCache|HudSystemFontWin32|thread_local)\b', code_views.get(target, "")):
                        add("hud-system-font-owner-or-live-state", start, target, witness=witness)
                if module == "campaign-score-system-font":
                    allowed = set(roots) | {"src/ui/input/RuntimeInputPixelValues.h", "src/ui/values/RuntimeColorValues.h"}
                    if target not in allowed:
                        add("campaign-score-system-font-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:MenuState|NativeMenuState|RuntimeMapRenderContext|RuntimeMapRenderCache|GameWorld|WorldObjectInstance|RuntimeWindowState|RuntimeResultBitFont|RuntimeCampaignMenuController|CampaignScoreSystemFontsWin32|thread_local)\b', code_views.get(target, "")):
                        add("campaign-score-system-font-owner-or-live-state", start, target, witness=witness)
                if module == "result-bitmap-text":
                    allowed = set(roots) | {"src/ui/input/RuntimeInputPixelValues.h", "src/assets/text/WestwoodFontCodec.h"}
                    if target not in allowed:
                        add("result-bitmap-text-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:RuntimeWindowState|RuntimeResultBitFont|RuntimeMatchScoreState|RuntimeMapRenderCache|GameWorld|WorldObjectInstance|runtimeMatchScoreScaledRect|thread_local)\b|\bstd::(?:ifstream|ofstream|filesystem)\b', code_views.get(target, "")):
                        add("result-bitmap-text-owner-or-live-state", start, target, witness=witness)
                if module == "main-bitmap-text":
                    allowed = set(roots) | SOFTWARE_FRAME_VALUES | {"src/assets/text/WestwoodFontCodec.h"}
                    if target not in allowed:
                        add("main-bitmap-text-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:MenuState|WestwoodBitFont|RuntimeMainMenuController|RuntimeMapRenderCache|GameWorld|WorldObjectInstance|thread_local)\b', code_views.get(target, "")):
                        add("main-bitmap-text-owner-or-live-state", start, target, witness=witness)
                if module == "persistent-menu-parent":
                    if target not in set(roots) | {"src/runtime/window/RuntimeWindowId.h"}:
                        add("persistent-parent-standard-dependency", start, target, witness=witness)
                    if target in roots and re.search(r'\b(?:RuntimePersistentMenuHostWin32|RuntimeWindowParentWin32|nativeWindow|parentBorrow_)\b', code_views.get(target, "")):
                        add("persistent-parent-native-owner", start, target, witness=witness)
                if module == "campaign-menu-controller":
                    allowed = set(roots) | {
                        "src/assets/codecs/RuntimeTmpTypes.h",
                        "src/data/campaign/CampaignDatabase.h",
                        "src/data/campaign/CampaignPresentationDecoder.h",
                        "src/data/campaign/CampaignProgress.h",
                        "src/render/core/RuntimeRendererEnvironment.h",
                        "src/render/core/RuntimeSpriteFrameImage.h",
                        "src/render/geometry/RuntimeSpriteHitSpan.h",
                        "src/render/shadows/RuntimeShadowDibHalf.h",
                        "src/render/shadows/RuntimeShadowOpaqueSpan.h",
                        "src/runtime/config/RuntimeConfig.h",
                        "src/runtime/contracts/RuntimeSkirmishSelection.h",
                        "src/ui/input/RuntimeInputPixelValues.h",
                        "src/ui/time/RuntimeUiClock.h"}
                    if target not in allowed:
                        add("campaign-menu-controller-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:CampaignMenuState|CampaignCsfCatalog|CampaignResourceStore|RuntimeMapRenderContext|RuntimeMapRenderCache)\b', code_views.get(target, "")):
                        add("campaign-menu-controller-native-owner", start, target, witness=witness)
                if module == "bink-video-parent":
                    allowed = set(roots) | {"src/runtime/config/RuntimeConfig.h", "src/runtime/contracts/RuntimeMainMenuResult.h", "src/runtime/window/RuntimeWindowId.h"}
                    if target not in allowed:
                        add("bink-video-public-dependency", start, target, witness=witness)
                if module == "main-menu-controller":
                    allowed = set(roots) | {"src/runtime/config/RuntimeConfig.h", "src/ui/time/RuntimeUiClock.h", "src/ui/input/RuntimeInputPixelValues.h"}
                    if target not in allowed:
                        add("main-menu-controller-dependency", start, target, witness=witness)
                if module == "startup-recovery-prompt" and target not in roots:
                    add("startup-recovery-prompt-dependency", start, target, witness=witness)
                if module == "menu-resource-watch" and target not in roots:
                    add("menu-resource-watch-dependency", start, target, witness=witness)
                if module == "movie-path" and target not in roots:
                    add("movie-path-dependency", start, target, witness=witness)
                if module == "audio-path" and target not in roots:
                    add("audio-path-dependency", start, target, witness=witness)
                if module == "resource-path" and target not in roots:
                    add("resource-path-dependency", start, target, witness=witness)
                if module == "host-loop-port" and target not in roots:
                    add("host-loop-port-dependency", start, target, witness=witness)
                if module == "timer-resolution-scope" and target not in roots:
                    add("timer-resolution-scope-dependency", start, target, witness=witness)
                if module == "process-performance-policy" and target not in roots:
                    add("process-performance-policy-dependency", start, target, witness=witness)
                if module == "presentation-clock" and target not in roots:
                    add("presentation-clock-dependency", start, target, witness=witness)
                if module == "atomic-file-interface" and target not in roots:
                    add("atomic-file-interface-dependency", start, target, witness=witness)
                if module == "file-path" and target not in roots:
                    add("file-path-dependency", start, target, witness=witness)
                if module == "map-tile-preparation":
                    allowed = set(DEFAULT_MODULES["map-tile-preparation"]) | MAP_DRAW_VALUES | {"src/core/tasks/BoundedTaskExecutor.h"}
                    if target not in allowed:
                        add("map-tile-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:GameWorld|WorldObjectInstance|RuntimeMapRenderCache|renderCache|thread_local|SDL_Window|SDL_Event|SDL_Surface)\b', code_views.get(target, "")):
                        add("map-tile-live-or-device", start, target, witness=witness)
                if module == "map-draw":
                    if target not in MAP_DRAW_VALUES:
                        add("map-draw-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:GameWorld|WorldObjectInstance|RuntimeMapRenderCache|renderCache|SDL_Window|SDL_Event|SDL_Surface)\b',code_views.get(target,"")):
                        add("map-draw-live-or-device", start, target, witness=witness)
                if module == "menu-draw":
                    if target not in MENU_DRAW_VALUES:
                        add("menu-draw-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:GameWorld|WorldObjectInstance|RuntimeMapRenderCache|renderCache|SDL_Window|SDL_Event|SDL_Surface)\b',code_views.get(target,"")):
                        add("menu-draw-live-or-device", start, target, witness=witness)
                if module == "software-window":
                    allowed = set(roots) | SOFTWARE_FRAME_VALUES | {"src/runtime/presentation/SoftwareFrame.h"}
                    if target not in allowed:
                        add("software-window-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:SDL_Window|SDL_Event|SDL_Surface|GameWorld|WorldObjectInstance|RuntimeMapRenderCache|renderCache)\b', code_views.get(target, "")):
                        add("software-window-device-or-live-state", start, target, witness=witness)
                if module == "software-frame":
                    if target not in roots and target not in SOFTWARE_FRAME_VALUES:
                        add("software-frame-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:GameWorld|WorldObjectInstance|RuntimeMapRenderCache|renderCache)\b',
                                 code_views.get(target, "")):
                        add("software-frame-live-state", start, target, witness=witness)
                if module == "path-candidate-task":
                    allowed = set(DEFAULT_MODULES["path-candidate-task"]) | {
                        "src/simulation/navigation/pathfinding/PathGoalTypes.h",
                        "src/core/tasks/BoundedTaskExecutor.h"}
                    if target not in allowed:
                        add("path-candidate-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:PathRequestQueue|GameWorld|WorldObjectInstance|RuntimeMapRenderCache|renderCache)\b',
                                 code_views.get(target, "")):
                        add("path-candidate-live-state", start, target, witness=witness)
                if module == "bounded-tasks":
                    if target not in roots:
                        add("task-executor-dependency", start, target, witness=witness)
                    if re.search(r'\b(?:GameWorld|WorldObjectInstance|RuntimeMapRenderCache|renderCache)\b',
                                 code_views.get(target, "")):
                        add("task-executor-live-state", start, target, witness=witness)
                if target.startswith(FORBIDDEN_PREFIXES) and not (
                        (module in ("software-frame", "software-window") and target in SOFTWARE_FRAME_VALUES)
                        or (module == "bink-video-parent" and target == "src/ui/main_menu/RuntimeMainMenu.h")
                        or (module == "lan-menu-secure-source" and target in set(roots) | {"src/core/security/RuntimeSecureBytes.h"})
                        or (module == "internet-menu-entropy-source" and target in set(roots) | {"src/core/security/RuntimeSecureBytes.h", "src/runtime/contracts/RuntimeInternetMenuData.h"})
                        or (module == "internet-skin-drawing" and target in set(roots) | {"src/ui/input/RuntimeInputPixelValues.h", "src/runtime/config/RuntimeConfig.h"})
                        or (module == "hud-system-font" and target in set(roots) | {"src/ui/input/RuntimeInputPixelValues.h"})
                        or (module == "campaign-score-system-font" and target in set(roots) | {"src/ui/input/RuntimeInputPixelValues.h", "src/ui/values/RuntimeColorValues.h"})
                        or (module == "match-result-system-font" and target in set(roots) | {"src/ui/input/RuntimeInputPixelValues.h", "src/ui/values/RuntimeColorValues.h"})
                        or (module == "campaign-system-font" and target in set(roots) | {"src/ui/input/RuntimeInputPixelValues.h", "src/ui/values/RuntimeColorValues.h"})
                        or (module == "skirmish-system-font-values" and target in roots)
                        or (module == "main-system-font" and target in set(roots) | {"src/ui/input/RuntimeInputPixelValues.h", "src/ui/values/RuntimeColorValues.h"})
                        or (module == "result-bitmap-text" and target in set(roots) | {"src/ui/input/RuntimeInputPixelValues.h"})
                        or (module == "main-bitmap-text" and target in set(roots) | SOFTWARE_FRAME_VALUES)
                        or (module in ("sprite-frame-values", "structure-geometry-values", "render-placement-values", "mobile-probe-values", "hit-span-point-query", "geometry-scope-viewport", "body-anchor-query", "projectile-screen-query", "building-visible-viewport", "structure-query-point", "turret-offset-return", "map-surface-pick", "infantry-muzzle-query", "marine-muzzle-query", "structure-muzzle-query", "voxel-muzzle-query", "spawned-missile-bounds-query", "object-shp-bounds-query", "mobile-sprite-bounds-query") and target == "src/ui/input/RuntimeInputPixelValues.h")
                        or (module == "campaign-menu-controller" and target in {"src/ui/time/RuntimeUiClock.h", "src/ui/input/RuntimeInputPixelValues.h"})
                        or (module == "main-menu-controller" and target in {"src/ui/time/RuntimeUiClock.h", "src/ui/input/RuntimeInputPixelValues.h"})
                        or (module == "radar-pixel-store" and target in set(roots) | SOFTWARE_FRAME_VALUES)
                        or (module == "map-drawing-interface" and target == "src/ui/input/RuntimeInputPixelValues.h")
                        or (module == "radar-cell-colour-values" and target in roots)
                        or (module == "menu-resource-watch" and target == "src/ui/resources/RuntimeMenuResourceWatch.h")
                        or (module == "presentation-clock" and target == "src/ui/time/RuntimeUiClock.h")
                        or (module == "input-controllers" and target in set(roots) | {"src/ui/time/RuntimeUiClock.h"})
                        or (module == "process-performance-policy" and target == "src/runtime/performance/RuntimeProcessPerformanceScope.h")
                        or (module == "timer-resolution-scope" and target == "src/runtime/timing/RuntimeTimerResolutionScope.h")
                        or (module == "host-loop-port" and target == "src/runtime/timing/RuntimeHostLoopPort.h")
                        or (module == "menu-draw" and target in MENU_DRAW_VALUES)
                        or (module in ("map-draw", "map-tile-preparation") and target in MAP_DRAW_VALUES)):
                    add("protected-dependency", start, module + ":" + target, witness=witness)
                for detail in platform.get(target, ()):
                    # Original synchronous diagnostic observer contexts carry no
                    # OS/device semantics. Keep all opaque facts visible in the
                    # global ledger; exempt only these three reviewed fields
                    # from this one standard module's native capability check.
                    if (module == "radar-cell-colour-values"
                            and target == "src/ui/radar/RuntimeRadarCellColors.h"
                            and detail in {"opaque:void*:context", "opaque:void*:previousContext_",
                                           "opaque:void*:runtimeRadarCellColorsObserverContext"}):
                        continue
                    add("protected-platform", start, module + ":" + target + ":" + detail, witness=witness)
                if re.search(r'\b(?:RuntimeWindowState|WindowState)\b', code_views.get(target, "")):
                    add("protected-host-state", start, module + ":" + target, witness=witness)

    targets = target_model(inventory)
    owners = defaultdict(list)
    for name, target in targets.items():
        per_target = Counter()
        for source in target.get("sources", []):
            if "compileGroupIndex" not in source or source.get("isGenerated"):
                continue
            path = Path(source["path"])
            if not path.is_absolute():
                path = root / path
            if path.resolve().is_relative_to(root):
                rel = relative(path, root)
                per_target[rel] += 1
                owners[rel].append(name)
                if (name in ("concord_superweapon_core_runtime_leaf", "concord_superweapon_core_contract_leaf")
                        and rel.startswith("src/simulation/superweapons/diagnostics/")):
                    add("superweapon-core-diagnostic-source", rel, name)
        for source, count in per_target.items():
            if count > 1:
                add("duplicate-target-source", source, name, count - 1)
        for fragment in target.get("link", {}).get("commandFragments", []):
            for lib in BUSINESS_LIB.findall(fragment["fragment"]):
                add("platform-link", "target:" + name, lib.lower())
    # A configure trace catches duplicate declarations that CMake deduplicates
    # before emitting the codemodel/compile database.
    declared = Counter()
    for line in Path(trace).read_text(encoding="utf-8-sig").splitlines():
        item = json.loads(line)
        if item.get("cmd", "").lower() not in ("add_executable", "add_library", "target_sources"):
            continue
        args = item.get("args", [])
        if not args:
            continue
        for arg in args[1:]:
            for token in arg.split(";"):
                if token.endswith(".cpp") and "$" not in token:
                    path = Path(token)
                    if not path.is_absolute():
                        path = root / path
                    if path.resolve().is_relative_to(root):
                        declared[(args[0], relative(path, root))] += 1
    for (target, source), count in declared.items():
        if count > 1:
            add("duplicate-source-declaration", source, target, count - 1)

    by_id = {t["id"]: name for name, t in targets.items() if "id" in t}
    for name, target in targets.items():
        if target.get("type") != "EXECUTABLE":
            continue
        linked = {name}
        todo = [name]
        while todo:
            consumer = todo.pop()
            for dep in targets[consumer].get("dependencies", []):
                child = by_id.get(dep["id"])
                if child and child not in linked and targets[child].get("type") in (
                        "STATIC_LIBRARY", "OBJECT_LIBRARY", "INTERFACE_LIBRARY"):
                    linked.add(child)
                    todo.append(child)
        for source, compile_owners in owners.items():
            joined = sorted(set(compile_owners) & linked)
            if len(joined) > 1:
                add("duplicate-link-source", source, name + ":" + "|".join(joined),
                    len(joined) - 1)

    mapped_tests = defaultdict(list)
    tests = inventory["tests"]
    for test in tests:
        target = test.get("target")
        if not target or target not in targets:
            add("test-without-target", "test:" + test["name"], str(target))
        else:
            mapped_tests[target].append(test["name"])
        if test.get('commandArtifactMatches') is False:
            add('test-command-target-mismatch', 'test:' + test['name'], str(target))
        if not test.get("labels"):
            add("test-without-label", "test:" + test["name"], str(target))
    test_files = sorted(n for n in files if n.startswith("tests/") and n.endswith(".cpp"))
    for name in test_files:
        available = owners.get(name, [])
        if not available:
            add("test-source-without-target", name, "no compile owner")
        elif not any(t in mapped_tests for t in available):
            add("test-source-without-registration", name, "|".join(sorted(available)))
    for row in cpp_inventory:
        row["compileOwners"] = sorted(owners.get(row["cpp"], []))

    cmake_files = sorted(set([root / "CMakeLists.txt"] + list((root / "cmake").rglob("*.cmake"))
                             + [p for p in (root / "tools").rglob("CMakeLists.txt")]))
    for path in cmake_files:
        scope = []
        for command, args, line in cmake_commands(path.read_text(encoding="utf-8-sig")):
            if command in ("if", "elseif"):
                if command == "elseif" and scope:
                    scope.pop()
                scope.append(args)
            elif command == "else" and scope:
                scope[-1] = "NOT (" + scope[-1] + ")"
            elif command == "endif" and scope:
                scope.pop()
            elif command in ("add_compile_options", "target_compile_options",
                             "add_link_options", "target_link_options", "set"):
                if command == "set" and not re.search(r'CMAKE_(?:CXX|EXE_LINKER|SHARED_LINKER)_FLAGS', args):
                    continue
                flags = FRONTEND_FLAG.findall(args)
                guard = " ".join(scope) + " " + args
                if flags and not re.search(r'CONCORD_FRONTEND_|'
                                           r'CMAKE_CXX_COMPILER_FRONTEND_VARIANT|'
                                           r'FRONTEND_VARIANT', guard):
                    add("unguarded-frontend-flags", relative(path, root),
                        command + ":" + " ".join(args.split()), line=line)
    for directory in ("cmake", "tools"):
        for path in sorted((root / directory).rglob("*")):
            if path.suffix.lower() not in (".ps1", ".cmake") and path.name != "CMakeLists.txt":
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
                if not line.lstrip().startswith("#"):
                    for match in VS_PATH.finditer(line):
                        add("hardcoded-vs-path", relative(path, root), match[0], line=number)

    if "hud-system-font" in modules:
        for canvas in ("src/ui/input/RuntimePlayerDefeatNoticeUi.inl",
                       "src/ui/input/RuntimePlayerWaypointsUi.inl",
                       "src/ui/input/RuntimePlayerHotkeysUi.inl",
                       "src/ui/diplomacy/RuntimeDiplomacyPage.inl"):
            if re.search(r'\b(?:CreateFontW|GetTextExtentPoint32W|DrawTextW|TextOutW)\s*\(', code_views.get(canvas, "")):
                add("hud-system-font-native-bypass", canvas, "font creation, measurement and text use the private provider")
    if "internet-skin-drawing" in modules:
        skin_header = "src/ui/network/RuntimeInternetMenuSkin.h"
        if re.search(r'\bvoid\s*\*|\b(?:RuntimeWindowState|GameWorld|RuntimeMapRenderCache)\b', code_views.get(skin_header, "")):
            add("internet-skin-live-owner-or-opaque", skin_header, "drawing borrows stay narrow standard values")
        native_header = "src/ui/network/win32/RuntimeInternetMenuSkinDrawingWin32.h"
        native_owners = {"src/ui/network/RuntimeInternetMenuSkin.cpp", "src/ui/network/RuntimeInternetMultiplayerMenu.cpp"}
        for edge in edges:
            if edge["target"] == native_header and edge["source"] not in native_owners:
                add("internet-skin-native-header-consumer", edge["source"], native_header, line=edge["line"])
    if "runtime-voice-path" in modules:
        path_header = "src/audio/backend/RuntimeVoicePath.h"
        if re.search(r'\b(?:RuntimeWindowState|GameWorld|RuntimeVoiceAudio)\b|\bvoid\s*\*', code_views.get(path_header, "")):
            add("runtime-voice-path-live-owner", path_header, "standard conversion only, no device or live runtime state")
        bindings = {
            "src/audio/backend/RuntimeSfxChannelPoolMci.inl": r'\baudio\s*\.\s*open\s*\([^;\n]*,\s*path\s*\)',
            "src/audio/backend/RuntimeWorldAudioBackend.inl": r'\baudio\s*\.\s*(?:open|queue)\s*\([^;\n]*,\s*\*\s*path\s*\)',
        }
        for binding, pattern in bindings.items():
            if re.search(pattern, code_views.get(binding, "")):
                add("runtime-voice-path-native-bypass", binding, "legacy native label must cross the explicit UTF8 Voice boundary")
    if "runtime-voice-path" in modules:
        disk_diagnostic = "src/audio/events/diagnostics/RuntimeDiskContinuousAudioSmoke.inl"
        if re.search(r'\bloadFromFile\s*\(\s*live\s*\.\s*report\s*\.\s*rulesPath\s*\)', code_views.get(disk_diagnostic, "")):
            add("runtime-voice-diagnostic-path-label-io", disk_diagnostic, "reuse native selected source owner, never open a diagnostic label as native narrow")
    if "runtime-mci-device-private" in modules:
        native_fixture = "src/audio/backend/diagnostics/RuntimeMciPathProducerSmoke.inl"
        for edge in edges:
            if edge["target"] == native_fixture and edge["source"] != "src/runtime/host/RuntimeShell.cpp":
                add("runtime-mci-path-native-consumer", edge["source"], native_fixture, line=edge["line"])
        shell = "src/runtime/host/RuntimeShell.cpp"
        code = code_views.get(shell, "")
        signature = r'\b(?:bool|void)\s+(playRuntimeEvaOnDedicatedChannel|startRuntimeMusic|playRuntimeResolvedSound)\s*\([^{};]*\)\s*\{'
        bypass = r'\bwidenUtf8\s*\(\s*(?:samplePath|\*\s*samplePath|\*\s*musicPath)\s*\)'
        for function in re.finditer(signature, code):
            cursor, depth = function.end(), 1
            while cursor < len(code) and depth:
                if code[cursor] == "{": depth += 1
                elif code[cursor] == "}": depth -= 1
                cursor += 1
            if re.search(bypass, code[function.end():cursor-1]):
                add("runtime-mci-path-native-bypass", shell, function[1])
    if "main-music-device" in modules:
        for standard in modules["main-music-device"]:
            if re.search(r'\b(?:RuntimeWindowState|GameWorld|RuntimeVoiceAudio)\b|\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(standard, "")):
                add("main-music-live-or-opaque", standard, "standard playback results cannot hide live or native owners")
        native_header = "src/audio/menu/win32/RuntimeMainMenuMusicWin32.h"
        allowed = {"src/ui/main_menu/RuntimeMainMenu.cpp", "src/audio/menu/win32/RuntimeMainMenuMusicWin32.cpp", "tests/audio/main_music_device/MainMusicNativeContract.cpp"}
        for edge in edges:
            if edge["target"] == native_header and edge["source"] not in allowed:
                add("main-music-native-consumer", edge["source"], native_header, line=edge["line"])
        main = "src/ui/main_menu/RuntimeMainMenu.cpp"
        if re.search(r'\bmciSendString[AW]?\s*\(', code_views.get(main, "")):
            add("main-music-native-bypass", main, "Main music operations use the single private device owner; PlaySound stays its separate original owner")
    if "campaign-map-audio-device" in modules:
        for standard in modules["campaign-map-audio-device"]:
            if re.search(r'\b(?:RuntimeWindowState|GameWorld|RuntimeVoiceAudio)\b|\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(standard, "")):
                add("campaign-map-audio-live-or-opaque", standard, "standard playback results cannot hide live or native owners")
        native_header = "src/audio/menu/win32/RuntimeCampaignMapAudioWin32.h"
        allowed = {"src/ui/campaign/RuntimeCampaignMenu.cpp", "src/audio/menu/win32/RuntimeCampaignMapAudioWin32.cpp", "tests/audio/campaign_map_audio/CampaignMapAudioNativeContract.cpp"}
        for edge in edges:
            if edge["target"] == native_header and edge["source"] not in allowed:
                add("campaign-map-audio-native-consumer", edge["source"], native_header, line=edge["line"])
        main = "src/ui/campaign/RuntimeCampaignMenu.cpp"
        if re.search(r'\bmciSendString[AW]?\s*\(', code_views.get(main, "")):
            add("campaign-map-audio-native-bypass", main, "Campaign voice/effect operations use the single private device owner")
    if "menu-sound-device" in modules:
        for standard in modules["menu-sound-device"]:
            if re.search(r'\b(?:RuntimeWindowState|GameWorld|RuntimeVoiceAudio)\b|\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(standard, "")):
                add("menu-sound-live-or-opaque", standard, "standard playback results cannot hide live or native owners")
        native_header = "src/audio/events/win32/RuntimeMenuSoundWin32.h"
        allowed = {"tests/audio/RuntimeMenuSfxClockContract.cpp", "src/ui/skirmish/RuntimeSkirmishMenu.cpp", "src/audio/events/win32/RuntimeMenuSoundWin32.cpp", "tests/audio/menu_sfx_device/MenuSoundNativeContract.cpp"}
        for edge in edges:
            if edge["target"] == native_header and edge["source"] not in allowed:
                add("menu-sound-native-consumer", edge["source"], native_header, line=edge["line"])
        main = "src/audio/events/RuntimeMenuSoundMci.inl"
        if re.search(r'\bmciSendString[AW]?\s*\(', code_views.get(main, "")):
            add("menu-sound-native-bypass", main, "Campaign voice/effect operations use the single private device owner")
    if "service-console" in modules:
        public = "src/network/internet/RuntimeInternetServiceProcess.h"
        private = "src/network/internet/private/RuntimeInternetServiceConsoleControl.h"
        for path in (public, private):
            if re.search(r'\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(path, "")):
                add("service-console-opaque", path, "service host and synchronous control borrow without erased native resources")
        allowed = {"src/network/internet/RuntimeInternetServiceProcess.cpp", "tests/internet/service_console/RuntimeServiceConsoleSmoke.cpp"}
        for edge in edges:
            if edge["target"] == private and edge["source"] not in allowed:
                add("service-console-private-owner", edge["source"], private, line=edge["line"])
        main = "src/network/internet/RuntimeInternetServiceMain.cpp"
        if re.search(r'\b(?:SetConsoleCtrlHandler|consoleControlHandler|stopRequested)\b', code_views.get(main, "")):
            add("service-console-main-bypass", main, "console atomic and registration remain in the sole service process producer")
    if "lan-nonce-noise-source" in modules:
        private = "src/network/session/private/RuntimeLanNonceNoiseSource.h"
        if re.search(r'\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(private, "")):
            add("lan-noise-opaque-source", private, "standard two-word noise borrow, no native resource erasure")
        allowed = {"src/network/session/RuntimeMultiplayerLanSession.cpp", "tests/multiplayer/RuntimeMultiplayerLanFlowSmoke.cpp"}
        oracle = "tests/multiplayer/RuntimeLanNonceNoiseSourceSmoke.inl"
        for edge in edges:
            if edge["target"] == private and edge["source"] not in allowed:
                add("lan-noise-private-source", edge["source"], private, line=edge["line"])
            if edge["target"] == oracle and edge["source"] != "tests/multiplayer/RuntimeMultiplayerLanFlowSmoke.cpp":
                add("lan-noise-private-oracle", edge["source"], oracle, line=edge["line"])
        owner = "src/network/session/RuntimeMultiplayerLanSession.cpp"
        if re.search(r'\b(?:BCryptGenRandom|random_device|mt19937)\b|\brand\s*\(', code_views.get(owner, "")):
            add("lan-noise-source-bypass", owner, "OS noise acquisition uses sole standard provider, S26 mix is separate")
    if "service-main-random" in modules:
        private = "src/network/internet/private/RuntimeInternetServiceRandomBytes.h"
        if re.search(r'\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(private, "")):
            add("service-main-random-opaque", private, "typed byte tuple/synchronous source, no erased native resource")
        allowed = {"src/network/internet/RuntimeInternetServiceMain.cpp", "src/network/internet/private/RuntimeInternetServiceRandomBytes.cpp", "tests/internet/service_random_bytes/RuntimeServiceRandomBytesSmoke.cpp"}
        for edge in edges:
            if edge["target"] == private and edge["source"] not in allowed:
                add("service-main-random-private-owner", edge["source"], private, line=edge["line"])
        for owner in ("src/network/internet/RuntimeInternetServiceMain.cpp", "src/network/internet/private/RuntimeInternetServiceRandomBytes.cpp"):
            if re.search(r'\b(?:BCryptGenRandom|random_device|mt19937)\b|\brand\s*\(', code_views.get(owner, "")):
                add("service-main-random-bypass", owner, "sole standardOS source; console/time/policy staysoriginalowner")
    if "tunnel-prelude-source" in modules:
        private = "src/network/transport/private/RuntimeTunnelPreludeSource.h"
        if re.search(r'\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(private, "")):
            add("tunnel-prelude-opaque-source", private, "typed synchronous byte borrow, no erased native owner")
        allowed = {"src/network/transport/RuntimeInternetTcpTunnelProxy.cpp", "tests/internet/RuntimeInternetTcpTunnelProxySmoke.cpp"}
        oracle = "tests/internet/RuntimeTunnelPreludeSourceSmoke.inl"
        for edge in edges:
            if edge["target"] == private and edge["source"] not in allowed:
                add("tunnel-prelude-private-source", edge["source"], private, line=edge["line"])
            if edge["target"] == oracle and edge["source"] != "tests/internet/RuntimeInternetTcpTunnelProxySmoke.cpp":
                add("tunnel-prelude-private-oracle", edge["source"], oracle, line=edge["line"])
        owner = "src/network/transport/RuntimeInternetTcpTunnelProxy.cpp"
        if re.search(r'\b(?:BCryptGenRandom|random_device|mt19937)\b|\brand\s*\(', code_views.get(owner, "")):
            add("tunnel-prelude-source-bypass", owner, "raw32 uses standardOSsource; no second or predictable generator")
    if "service-nonce-source" in modules:
        private = "src/network/transport/private/RuntimeInternetServiceNonceSource.h"
        if re.search(r'\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(private, "")):
            add("service-nonce-opaque-source", private, "nonce source is typed synchronous borrow, no erased native owner")
        allowed = {"src/network/transport/RuntimeInternetServiceTransport.cpp", "tests/internet/RuntimeInternetServiceTransportSmoke.cpp"}
        oracle = "tests/internet/RuntimeInternetServiceNonceSourceSmoke.inl"
        for edge in edges:
            if edge["target"] == private and edge["source"] not in allowed:
                add("service-nonce-private-source", edge["source"], private, line=edge["line"])
            if edge["target"] == oracle and edge["source"] != "tests/internet/RuntimeInternetServiceTransportSmoke.cpp":
                add("service-nonce-private-oracle", edge["source"], oracle, line=edge["line"])
        owner = "src/network/transport/RuntimeInternetServiceTransport.cpp"
        if re.search(r'\b(?:BCryptGenRandom|random_device|mt19937)\b|\brand\s*\(', code_views.get(owner, "")):
            add("service-nonce-source-bypass", owner, "actual standard OS bytes, no second or predictable source")
    if "lan-menu-secure-source" in modules:
        private = "src/ui/network/private/RuntimeLanMenuSecureSource.h"
        if re.search(r'\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(private, "")):
            add("lan-secure-opaque-source", private, "typed byte source borrows onecall; no erased native owner")
        allowed = {"src/ui/network/RuntimeMultiplayerMenu.cpp", "tests/multiplayer/RuntimeMultiplayerMenuSmoke.cpp"}
        oracle = "tests/multiplayer/RuntimeLanMenuSecureSourceSmoke.inl"
        for edge in edges:
            if edge["target"] == private and edge["source"] not in allowed:
                add("lan-secure-private-source", edge["source"], private, line=edge["line"])
            if edge["target"] == oracle and edge["source"] != "tests/multiplayer/RuntimeMultiplayerMenuSmoke.cpp":
                add("lan-secure-private-oracle", edge["source"], oracle, line=edge["line"])
        owner = "src/ui/network/RuntimeMultiplayerMenu.cpp"
        if re.search(r'\b(?:BCryptGenRandom|random_device|mt19937)\b|\brand\s*\(', code_views.get(owner, "")):
            add("lan-secure-source-bypass", owner, "original ID/U64 policies use standard secure source")
    if "internet-menu-entropy-source" in modules:
        private = "src/ui/network/private/RuntimeInternetMenuEntropySource.h"
        if re.search(r'\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(private, "")):
            add("internet-entropy-opaque-source", private, "typed synchronous byte source cannot erase native resource owners")
        allowed = {"src/ui/network/RuntimeInternetMultiplayerMenu.cpp", "tests/internet/RuntimeInternetMultiplayerMenuSmoke.cpp"}
        oracle = "tests/internet/RuntimeInternetMenuEntropySourceSmoke.inl"
        for edge in edges:
            if edge["target"] == private and edge["source"] not in allowed:
                add("internet-entropy-private-source", edge["source"], private, line=edge["line"])
            if edge["target"] == oracle and edge["source"] != "tests/internet/RuntimeInternetMultiplayerMenuSmoke.cpp":
                add("internet-entropy-private-oracle", edge["source"], oracle, line=edge["line"])
        owner = "src/ui/network/RuntimeInternetMultiplayerMenu.cpp"
        if re.search(r'\b(?:BCryptGenRandom|random_device|mt19937)\b|\brand\s*\(', code_views.get(owner, "")):
            add("internet-entropy-source-bypass", owner, "original transaction uses standard system source; no independent generator")
    if "system-secure-bytes" in modules:
        for public in modules["system-secure-bytes"]:
            if re.search(r'\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(public, "")):
                add("secure-bytes-opaque-native", public, "standard bytes/result contract cannot expose erased OS resources")
        source = "src/core/security/RuntimeSecureBytes.cpp"
        if re.search(r'\b(?:random_device|mt19937|steady_clock|system_clock)\b|\brand\s*\(', code_views.get(source, "")):
            add("secure-bytes-predictable-fallback", source, "OS CSPRNG failure stays explicit; no weaker fallback")
        private = "src/core/security/private/RuntimeSecureBytesReader.h"
        allowed = {source, "tests/infrastructure/RuntimeSecureBytesContract.cpp"}
        for edge in edges:
            if edge["target"] == private and edge["source"] not in allowed:
                add("secure-bytes-private-reader", edge["source"], private, line=edge["line"])
    if "credential-certificate-private" in modules:
        private = "src/network/internet/private/RuntimeInternetServiceCertificateFactoryWin32.h"
        allowed = {"src/network/internet/RuntimeInternetServiceHostConfig.cpp", "tests/internet/RuntimeInternetTlsTransportSmoke.cpp"}
        oracle = "tests/internet/RuntimeInternetServiceCredentialCertificateSmoke.inl"
        for edge in edges:
            if edge["target"] == private and edge["source"] not in allowed:
                add("credential-certificate-native-consumer", edge["source"], private, line=edge["line"])
            if edge["target"] == oracle and edge["source"] != "tests/internet/RuntimeInternetTlsTransportSmoke.cpp":
                add("credential-certificate-private-oracle", edge["source"], oracle, line=edge["line"])
    if "tls-certificate-contract" in modules:
        for public in modules["tls-certificate-contract"]:
            if re.search(r'\b(?:nativeCertificateContext|nativeCertificateChainEngine)\b|\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(public, "")):
                add("tls-certificate-opaque-native", public, "server identity and peer trust use distinct standard borrowed capabilities")
        private = "src/network/transport/private/RuntimeInternetTlsCertificateWin32.h"
        allowed = {"src/network/transport/RuntimeInternetTlsTransport.cpp", "src/network/internet/RuntimeInternetServiceHostConfig.cpp",
                   "tests/internet/RuntimeInternetTlsTransportSmoke.cpp", "tests/internet/RuntimeInternetServiceTransportSmoke.cpp",
                   "tests/internet/RuntimeInternetServiceHostSmoke.cpp", "tests/internet/RuntimeInternetServiceLobbyE2E.cpp",
                   "tests/internet/service_console/RuntimeServiceConsoleSmoke.cpp"}
        oracle = "tests/internet/RuntimeInternetTlsCertificateBorrowSmoke.inl"
        for edge in edges:
            if edge["target"] == private and edge["source"] not in allowed:
                add("tls-certificate-native-consumer", edge["source"], private, line=edge["line"])
            if edge["target"] == oracle and edge["source"] != "tests/internet/RuntimeInternetTlsTransportSmoke.cpp":
                add("tls-certificate-private-oracle", edge["source"], oracle, line=edge["line"])
    if "video-caller-private" in modules:
        private = "src/ui/main_menu/diagnostics/RuntimeMainVideoCallerBoundarySmoke.inl"
        for edge in edges:
            if edge["target"] == private and edge["source"] != "src/ui/main_menu/RuntimeMainMenu.cpp":
                add("video-caller-private-oracle", edge["source"], private, line=edge["line"])
    if "bink-video-parent" in modules:
        public = "src/ui/main_menu/RuntimeMainMenu.h"
        declaration = code_views.get(public, "")
        found = re.search(r"runRuntimeBinkVideo[^a-zA-Z0-9_]*[(]([^)]*)[)]", declaration)
        if found and ("RuntimeWindowId hostWindow" not in found.group(1) or "void*" in found.group(1) or "void *" in found.group(1)):
            add("bink-video-opaque-parent", public, "modal video borrows existing allocated RuntimeWindowId")
        private = "src/ui/campaign/diagnostics/RuntimeBinkParentIdentitySmoke.inl"
        for edge in edges:
            if edge["target"] == private and edge["source"] != "src/ui/main_menu/RuntimeMainMenu.cpp":
                add("bink-parent-private-oracle", edge["source"], private, line=edge["line"])
    if "async-sound-device" in modules:
        for standard in modules["async-sound-device"]:
            if re.search(r'\b(?:RuntimeWindowState|GameWorld|RuntimeVoiceAudio)\b|\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(standard, "")):
                add("async-sound-live-or-opaque", standard, "standard playback results cannot hide live or native owners")
        native_header = "src/audio/backend/win32/RuntimeAsyncSoundDeviceWin32.h"
        shell = "src/runtime/host/RuntimeShell.cpp"
        allowed = {"src/ui/main_menu/RuntimeMainMenu.cpp", shell, "src/runtime/session/win32/RuntimeNativeSessionDevices.cpp", "src/audio/backend/win32/RuntimeAsyncSoundDeviceWin32.cpp", "tests/audio/async_sound_device/AsyncSoundNativeContract.cpp"}
        for edge in edges:
            if edge["target"] == native_header and edge["source"] not in allowed:
                add("async-sound-native-consumer", edge["source"], native_header, line=edge["line"])
        if re.search(r'\bcreateAsyncSoundDeviceWin32\s*\(', code_views.get(shell, "")):
            add("async-sound-native-factory-owner", shell, "runtime async sound is composed by the private session device owner")
        for main in ("src/ui/main_menu/RuntimeMainMenu.cpp", shell,
                     "src/runtime/session/RuntimeSessionFeedback.cpp", "src/runtime/session/win32/RuntimeNativeSessionDevices.cpp"):
            if re.search(r'\bPlaySound[AW]?\s*\(', code_views.get(main, "")):
                add("async-sound-native-bypass", main, "Main/EVA async sound operations use the single private device owner")
        fixture = "src/audio/events/diagnostics/RuntimeEvaPlaySoundFallbackSmoke.inl"
        for edge in edges:
            if edge["target"] == fixture and edge["source"] != "src/runtime/host/RuntimeShell.cpp":
                add("async-sound-fixture-consumer", edge["source"], fixture, line=edge["line"])
    if "legacy-wave-device" in modules:
        for standard in modules["legacy-wave-device"]:
            if re.search(r'\b(?:RuntimeWindowState|GameWorld|RuntimeVoiceAudio)\b|\bvoid\s*\*|\b(?:std::)?uintptr_t\b', code_views.get(standard, "")):
                add("legacy-wave-live-or-opaque", standard, "standard playback results cannot hide live or native owners")
        native_header = "src/audio/backend/win32/RuntimeLegacyWaveDeviceWin32.h"
        allowed = {"src/runtime/session/win32/RuntimeNativeSessionDevices.cpp", "src/audio/backend/win32/RuntimeLegacyWaveDeviceWin32.cpp", "tests/audio/legacy_wave/LegacyWaveNativeContract.cpp"}
        for edge in edges:
            if edge["target"] == native_header and edge["source"] not in allowed:
                add("legacy-wave-native-consumer", edge["source"], native_header, line=edge["line"])
        signature = r'\b(?:bool|void|std::optional<bool>|std::wstring)\s+(closeRuntimeEvaMciChannel|runtimeEvaMciChannelIsPlaying|playRuntimeEvaOnDedicatedChannel|startRuntimeMusic|stopRuntimeMusic|ensureRuntimeMusicPlaying|encodeRuntimeLegacyWaveDevicePath)\s*\([^{};]*\)\s*\{'
        for source in ("src/runtime/host/RuntimeShell.cpp", "src/runtime/session/RuntimeSessionFeedback.cpp",
                       "src/runtime/session/win32/RuntimeNativeSessionDevices.cpp"):
            code = code_views.get(source, "")
            for function in re.finditer(signature, code):
                cursor, depth = function.end(), 1
                while cursor < len(code) and depth:
                    if code[cursor] == "{": depth += 1
                    elif code[cursor] == "}": depth -= 1
                    cursor += 1
                body = code[function.end():cursor-1]
                if re.search(r'\bmciSendString[AW]?\s*\(', body):
                    add("legacy-wave-native-bypass", source, function[1])
                if function[1] == "encodeRuntimeLegacyWaveDevicePath" and re.search(r'\bwidenUtf8\s*\(\s*path\s*\)', body):
                    add("legacy-wave-native-token-bypass", source, "reuse existing native-to-UTF8 composition before original wide encoder")
    if "world-audio-device-private" in modules:
        native_fixture = "src/audio/world/diagnostics/RuntimeWorldAudioDeviceLifecycleSmoke.inl"
        for edge in edges:
            if edge["target"] == native_fixture and edge["source"] != "src/runtime/host/RuntimeShell.cpp":
                add("world-audio-device-native-consumer", edge["source"], native_fixture, line=edge["line"])
        sw_fixture = "src/audio/events/diagnostics/RuntimeSuperWeaponActivationAudioSmoke.inl"
        if re.search(r'\.\s*loadFromFile\s*\(\s*live\s*\.\s*report\s*\.\s*(?:rulesPath|artPath)\s*\)', code_views.get(sw_fixture, "")):
            add("world-audio-device-display-label-io", sw_fixture, "diagnostic display paths are not native source inputs")
    if "campaign-menu-surface-owner" in modules:
        private_header = "src/ui/campaign/win32/RuntimeCampaignDeviceBitmapWin32.h"
        allowed = {"src/ui/campaign/RuntimeCampaignMenu.cpp", "tests/infrastructure/campaign_menu_surface/CampaignDeviceBitmapContract.cpp"}
        for edge in edges:
            if edge["target"] == private_header and edge["source"] not in allowed:
                add("campaign-menu-surface-native-consumer", edge["source"], private_header, line=edge["line"])
        ui_source = "src/ui/campaign/RuntimeCampaignMenuUi.inl"
        if re.search(r'\b(?:CreateCompatibleBitmap|CreateCompatibleDC)\s*\(', code_views.get(ui_source, "")):
            add("campaign-menu-surface-owner-bypass", ui_source, "reuse the single private compatible bitmap resource owner")
    if "main-menu-surface-owner" in modules:
        private_header = "src/ui/main_menu/win32/RuntimeMainMenuBackBufferWin32.h"
        allowed = {"src/ui/main_menu/RuntimeMainMenu.cpp", "tests/infrastructure/main_menu_surface/MainMenuBackBufferContract.cpp"}
        for edge in edges:
            if edge["target"] == private_header and edge["source"] not in allowed:
                add("main-menu-surface-native-consumer", edge["source"], private_header, line=edge["line"])
        main_source = "src/ui/main_menu/RuntimeMainMenu.cpp"
        if re.search(r'\b(?:CreateDIBSection|CreateCompatibleDC)\s*\(', code_views.get(main_source, "")):
            add("main-menu-surface-owner-bypass", main_source, "reuse the single private native resource owner")
    if "hud-private-drawing" in modules:
        private_hud_headers = {
            "src/ui/selection/RuntimeSelectedPathBatch.h": {"src/runtime/host/RuntimeShell.cpp"},
            "src/ui/selection/RuntimeSelectedPathRaster.h": {"src/runtime/host/RuntimeShell.cpp", "src/ui/selection/RuntimeSelectionFrameRaster.h"},
            "src/ui/selection/RuntimeSelectionFrameRaster.h": {"src/runtime/host/RuntimeShell.cpp"},
        }
        for edge in edges:
            if edge["target"] in private_hud_headers and edge["source"] not in private_hud_headers[edge["target"]]:
                add("hud-native-header-consumer", edge["source"], edge["target"], line=edge["line"])
    compile_db = read_json(build / "compile_commands.json")
    if "map-drawing-interface" in modules:
        application = "src/runtime/host/RuntimeShell.cpp"
        application_code = code_views.get(application, "")
        if re.search(r'\bdrawRuntimeMap\s*\(', application_code):
            add("map-drawing-native-production-bypass", application, "direct native map call bypasses the standard target port")
        if re.search(r'\bdrawRuntimeAuxiliaryShpFrame\s*\(', application_code):
            add("auxiliary-shp-native-production-bypass", application, "direct native auxiliary call bypasses the standard target port")
        if re.search(r'\bapplyRuntimeScreenShake\s*\(', application_code):
            add("screen-shake-native-production-bypass", application, "direct native screen-shake call bypasses the standard target port")
        # The remaining original preview entry is diagnostic-only, not a new
        # gameplay drawing port. Keep its exact original private consumers.
        preview_diagnostic_owners = {
            'src/render/buildings/diagnostics/RuntimeGarrisonPipRenderSmoke.inl',
            'src/render/buildings/diagnostics/RuntimeGarrisonRenderContract.inl',
            'src/render/core/RuntimeMapRenderer.cpp',
            'src/render/core/win32/RuntimeMapDrawingWin32.h',
            'src/render/geometry/RuntimeAllWeaponMuzzleRender.inl',
            'src/render/geometry/diagnostics/RuntimeGroundOcclusionMarkerIssue25Smoke.inl',
            'src/simulation/buildings/diagnostics/RuntimeBuildingFrozenDepthSmoke.inl',
            'src/simulation/buildings/diagnostics/RuntimeBuildingOcclusionSmoke.inl',
            'src/simulation/buildings/diagnostics/RuntimeBuildingTransparencySmoke.inl',
            'src/simulation/production/diagnostics/RuntimeBuildupSmoke.inl',
            'src/simulation/production/diagnostics/RuntimeBuildupTheaterSmoke.inl',
            'src/ui/selection/diagnostics/RuntimeBuildingSelectionAlignmentSmoke.inl',
        }
        for preview_path, preview_code in code_views.items():
            if (preview_path not in preview_diagnostic_owners
                    and re.search(r'\bdrawRuntimeObjectPreviewSprite\b', preview_code)):
                add("object-preview-native-consumer", preview_path,
                    "original native preview helper is limited to its reviewed private diagnostic owners")

    return dict(schema="ConcordStructureScan/v1", generatedUtc=datetime.now(timezone.utc).isoformat(),
                limitations=["Lexical all-branch include graph; no macro expansion or system-header expansion.",
                             "File API and configure trace cover the selected current Clang Release cache.",
                             "Native semantic pointers are review signals, not proof of portability.",
                             "No GUI/device/non-Windows/performance or production behavior execution."],
                files=[dict(path=n, bytes=p.stat().st_size, sha256=sha(p),
                            lines=len(p.read_text(encoding="utf-8-sig").splitlines()))
                       for n, p in sorted(files.items())],
                rootCmakeIncludes=[args.strip() for cmd,args,line in cmake_commands((root/'CMakeLists.txt').read_text(encoding='utf-8-sig')) if cmd == 'include'],
                includeGraph=edges, unresolvedLocalIncludes=unresolved,
                headerPlatformReachability=transitive, cppDependencies=cpp_inventory,
                targets=targets, compileOwners={k: sorted(v) for k, v in sorted(owners.items())},
                compileCommands=compile_db, testInventory=tests,
                protectedModules=modules, findings=sorted(facts.values(),
                    key=lambda f: (f["rule"], f["path"], f["detail"])))

def identity(fact):
    return fact["rule"], fact["path"], fact["detail"]

def evaluate(report, baseline):
    if baseline.get("schema") != "ConcordStructureBaseline/v1":
        raise ValueError("Unsupported exception schema")
    exceptions = {}
    for row in baseline["exceptions"]:
        key = identity(row)
        if key in exceptions or any(c in row["path"] for c in "*?[]"):
            raise ValueError("Duplicate or wildcard exception: " + str(key))
        if not row.get("reason") or not row.get("owner_step") or not row.get("follow_up"):
            raise ValueError("Exception lacks reason/owner/follow-up: " + str(key))
        if not isinstance(row.get("maxOccurrences"), int) or row["maxOccurrences"] < 1:
            raise ValueError("Invalid exact occurrence limit: " + str(key))
        exceptions[key] = row
    debt, violations, retired = [], [], []
    if baseline.get('rootCmakeIncludes') != report.get('rootCmakeIncludes'):
        raise ValueError('Root CMake include order changed or missing from baseline')
    actual = {identity(f): f for f in report["findings"]}
    for key, fact in actual.items():
        allowed = exceptions.get(key)
        if allowed and fact["occurrences"] <= allowed["maxOccurrences"]:
            debt.append(dict(**fact, exception=allowed))
            if fact["occurrences"] < allowed["maxOccurrences"]:
                retired.append(dict(**allowed, remainingOccurrences=fact["occurrences"]))
        else:
            violations.append(dict(**fact, reason="new finding" if not allowed else "occurrence budget increased"))
    for key, allowed in exceptions.items():
        if key not in actual:
            retired.append(dict(**allowed, remainingOccurrences=0))
    # Retire resolved exceptions in the same implementation batch. No automatic growth or shrink.
    return dict(schema="ConcordStructureResult/v1", status="passed" if not violations and not retired else "failed",
                historicalDebt=debt, newViolations=violations, exceptionsToTighten=retired,
                counts=dict(historicalDebt=len(debt), newViolations=len(violations),
                            exceptionsToTighten=len(retired)),
                platformLevels="Static boundary evidence only; no complete application L1-L4 claim")

def write_json(path, value):
    # All artifacts are new files. Never overwrite a prior result.
    with Path(path).open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--inventory", required=True, type=Path)
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output.resolve()
    if not output.is_relative_to(root) or output == root or output.exists():
        parser.error("Choose a new project-local output directory")
    output.mkdir(parents=True)
    report = scan(root, args.build, read_json(args.inventory), args.trace)
    result = evaluate(report, read_json(args.baseline))
    write_json(output / "scan.json", report)
    write_json(output / "result.json", result)
    write_json(output / 'include-graph.json', report['includeGraph'])
    write_json(output / 'cpp-diagnostics.json', report['cppDependencies'])
    write_json(output / 'platform-dependencies.json', report['headerPlatformReachability'])
    write_json(output / 'historical-debt.json', result['historicalDebt'])
    write_json(output / 'new-violations.json', result['newViolations'])
    print(json.dumps(dict(status=result["status"], **result["counts"], output=str(output))))
    return 0 if result["status"] == "passed" else 1

if __name__ == "__main__":
    sys.exit(main())
