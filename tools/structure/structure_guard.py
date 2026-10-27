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
DEFAULT_MODULES = {
    "software-window": ["src/runtime/window/RuntimeSoftwareWindow.h"],
    "software-frame": ["src/runtime/presentation/SoftwareFrame.h"],
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
                        module in ("software-frame", "software-window") and target in SOFTWARE_FRAME_VALUES):
                    add("protected-dependency", start, module + ":" + target, witness=witness)
                for detail in platform.get(target, ()):
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

    compile_db = read_json(build / "compile_commands.json")
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
