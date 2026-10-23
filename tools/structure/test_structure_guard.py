"""Synthetic fixtures for the real guard; every fixture is retained, never deleted."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

spec = importlib.util.spec_from_file_location("structure_guard", Path(__file__).with_name("structure_guard.py"))
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)
FIXTURES = None

class GuardFixtures(unittest.TestCase):
    def setUp(self):
        self.root = FIXTURES / self._testMethodName
        self.root.mkdir(parents=True, exist_ok=False)
        for directory in ("src/core", "src/ui", "src/runtime/host", "src/render/core",
                          "tests", "tools", "cmake", "build/reply"):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        self.put("src/core/Pure.h", "#pragma once\n")
        self.put("src/core/Pure.cpp", '#include "Pure.h"\n')
        self.put("CMakeLists.txt", "project(Fixture)\n")
        self.put("build/compile_commands.json", "[]")
        self.put("build/trace.jsonl", "{}\n")
        self.targets = {}
        self.inventory = dict(configuration="Release", fileApi=dict(
            codemodelFile=str(self.root / "build/reply/model.json")), tests=[])

    def put(self, name, value):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value, encoding="utf-8")

    def scan(self):
        refs = []
        for name, target in self.targets.items():
            self.put("build/reply/" + name + ".json", json.dumps(dict(name=name, **target)))
            refs.append(dict(name=name, jsonFile=name + ".json"))
        self.put("build/reply/model.json", json.dumps(dict(configurations=[
            dict(name="Release", targets=refs)])))
        return guard.scan(self.root, self.root / "build", self.inventory,
                          self.root / "build/trace.jsonl",
                          {"pure": ["src/core/Pure.h", "src/core/Pure.cpp"]})

    def rules(self):
        return {f["rule"] for f in self.scan()["findings"]}

    def baseline(self, report):
        return dict(schema="ConcordStructureBaseline/v1", rootCmakeIncludes=report["rootCmakeIncludes"], exceptions=[
            dict(**{k: f[k] for k in ("rule", "path", "detail")},
                 maxOccurrences=f["occurrences"], reason="Exact synthetic historical fact",
                 owner_step="P05", follow_up="W013")
            for f in report["findings"]])

    def test_legal_relative_and_angle_local(self):
        self.put("src/core/Pure.cpp", '#include "../core/Pure.h"\n#include <core/Pure.h>\n')
        report = self.scan()
        self.assertEqual(len(report["includeGraph"]), 2)
        self.assertEqual(report["findings"], [])

    def test_comments_do_not_invent_includes_types(self):
        self.put("src/core/Pure.h", '/* #include <windows.h>\nHWND x; */\n// WindowState HWND\n')
        self.assertEqual(self.scan()["findings"], [])

    def test_literals_and_raw_strings_are_not_code(self):
        self.put("src/core/Pure.h", 'auto a = "HWND WindowState";\nauto b=R"tag(\n#include <windows.h>\nRECT x;\n)tag";\n')
        self.assertEqual(self.scan()["findings"], [])

    def test_block_comment_before_directive(self):
        self.put("src/core/Pure.h", '/* okay */ # include <windows.h>\n')
        self.assertIn("protected-platform", self.rules())

    def test_line_spliced_include(self):
        self.put("src/core/Pure.h", '#inc\\\nlude <windows.h>\n')
        self.assertIn("protected-platform", self.rules())

    def test_line_spliced_comment(self):
        self.put("src/core/Pure.h", '// comment \\\n#include <windows.h>\n')
        self.assertEqual(self.scan()["findings"], [])

    def test_direct_host_dependency(self):
        self.put("src/runtime/host/RuntimeShell.h", "struct Host {};\n")
        self.put("src/core/Pure.h", '#include "runtime/host/RuntimeShell.h"\n')
        self.assertIn("protected-dependency", self.rules())

    def test_transitive_ui_dependency(self):
        self.put("src/ui/Display.h", "struct Display {};\n")
        self.put("src/core/Bridge.h", '#include "ui/Display.h"\n')
        self.put("src/core/Pure.h", '#include "Bridge.h"\n')
        facts = self.scan()["findings"]
        self.assertTrue(any(f["rule"] == "protected-dependency"
                            and len(f["witness"]) == 3 for f in facts))

    def test_transitive_windows_header(self):
        self.put("src/core/Bridge.h", "#include <wingdi.h>\n")
        self.put("src/core/Pure.h", '#include "Bridge.h"\n')
        self.assertIn("platform-edge", self.rules())

    def test_platform_types_without_windows_include(self):
        self.put("src/core/Pure.h", "struct P { HWND window; RECT rect; };\n")
        self.assertIn("protected-platform", self.rules())

    def test_opaque_window_without_windows_header(self):
        self.put("src/core/Pure.h", "void open(void* hostWindow);\n")
        self.assertIn("opaque-native", self.rules())

    def test_uintptr_native_handle(self):
        self.put("src/core/Pure.h", "std::uintptr_t nativeCertificateContext;\n")
        self.assertIn("opaque-native", self.rules())

    def test_host_state_token(self):
        self.put("src/core/Pure.h", "struct X { RuntimeWindowState& state; };\n")
        self.assertIn("protected-host-state", self.rules())

    def test_include_cycle_terminates(self):
        self.put("src/core/Pure.h", '#include "Bridge.h"\n')
        self.put("src/core/Bridge.h", '#include "Pure.h"\n')
        self.assertEqual(self.scan()["findings"], [])

    def test_cpp_text_include(self):
        self.put("src/core/Pure.h", '#include "Pure.cpp"\n')
        self.assertIn("cpp-text-include", self.rules())

    def test_new_business_inl_in_host(self):
        self.put("src/core/Business.inl", "int business = 1;\n")
        self.put("src/runtime/host/RuntimeShell.cpp", '#include "core/Business.inl"\n')
        self.assertIn("host-business-inl", self.rules())

    def test_diagnostic_classified_separately(self):
        self.put("src/core/diagnostics/Case.inl", "int check = 1;\n")
        self.put("src/runtime/host/RuntimeShell.cpp", '#include "core/diagnostics/Case.inl"\n')
        report = self.scan()
        host = next(r for r in report["cppDependencies"] if r["cpp"].endswith("RuntimeShell.cpp"))
        self.assertEqual(host["diagnostics"], ["src/core/diagnostics/Case.inl"])
        self.assertNotIn("host-business-inl", {f["rule"] for f in report["findings"]})

    def test_missing_local_include_fails(self):
        self.put("src/core/Pure.h", '#include "Missing.h"\n')
        self.assertIn("unresolved-local-include", self.rules())

    def test_duplicate_source_declaration(self):
        self.put("build/trace.jsonl", json.dumps(dict(cmd="add_executable",
            args=["test", "tests/Case.cpp", "tests/Case.cpp"])) + "\n")
        self.assertIn("duplicate-source-declaration", self.rules())

    def test_duplicate_codemodel_source(self):
        self.targets["test"] = dict(sources=[dict(path="src/core/Pure.cpp", compileGroupIndex=0)] * 2)
        self.assertIn("duplicate-target-source", self.rules())

    def test_same_implementation_direct_and_linked_library(self):
        source = dict(path="src/core/Pure.cpp", compileGroupIndex=0)
        self.targets["leaf"] = dict(id="leaf-id", type="STATIC_LIBRARY", sources=[source])
        self.targets["app"] = dict(id="app-id", type="EXECUTABLE", sources=[source],
                                   dependencies=[dict(id="leaf-id")])
        self.assertIn("duplicate-link-source", self.rules())

    def test_macro_include_requires_review(self):
        self.put('src/core/Pure.h', '#include UNKNOWN_PLATFORM\n')
        self.assertIn('macro-include-review', self.rules())

    def test_command_target_mismatch(self):
        self.targets['test'] = dict(sources=[])
        self.inventory['tests'] = [dict(name='case', target='test', labels=['logic'], commandArtifactMatches=False)]
        self.assertIn('test-command-target-mismatch', self.rules())

    def test_root_order_drift_fails(self):
        report=self.scan()
        baseline=self.baseline(report)
        self.put('CMakeLists.txt','include(cmake/Extra.cmake)\n')
        with self.assertRaises(ValueError):
            guard.evaluate(self.scan(),baseline)

    def test_compiler_id_does_not_prove_frontend(self):
        self.put('cmake/Flags.cmake','if(CMAKE_CXX_COMPILER_ID STREQUAL Clang)\n target_compile_options(t PRIVATE /WX)\nendif()\n')
        self.assertIn('unguarded-frontend-flags',self.rules())

    def test_new_test_source_without_target(self):
        self.put("tests/Case.cpp", "int main() { return 0; }\n")
        self.assertIn("test-source-without-target", self.rules())

    def test_test_registration_without_target(self):
        self.inventory["tests"] = [dict(name="case", target=None, labels=["logic"])]
        self.assertIn("test-without-target", self.rules())

    def test_test_without_label(self):
        self.targets["test"] = dict(sources=[])
        self.inventory["tests"] = [dict(name="case", target="test", labels=[])]
        self.assertIn("test-without-label", self.rules())

    def test_valid_test_target_registration_label(self):
        self.put("tests/Case.cpp", "int main() { return 0; }\n")
        self.targets["test"] = dict(sources=[dict(path="tests/Case.cpp", compileGroupIndex=0)])
        self.inventory["tests"] = [dict(name="case", target="test", labels=["logic"])]
        self.assertEqual(self.scan()["findings"], [])

    def test_platform_target_link(self):
        self.targets["pure"] = dict(link=dict(commandFragments=[dict(fragment="gdi32.lib")]))
        self.assertIn("platform-link", self.rules())

    def test_hardcoded_vs_path(self):
        self.put("tools/Build.ps1", "$tool='C:\\Microsoft Visual Studio\\VC\\cl.exe'\n")
        self.assertIn("hardcoded-vs-path", self.rules())

    def test_unguarded_frontend_flags(self):
        self.put("cmake/Flags.cmake", 'if(MSVC)\n target_compile_options(t PRIVATE /WX)\nendif()\n')
        self.assertIn("unguarded-frontend-flags", self.rules())

    def test_frontend_guard_and_comments(self):
        self.put("cmake/Flags.cmake", '# target_compile_options(t PRIVATE /BAD)\n'
                 'if(CONCORD_FRONTEND_MSVC)\n target_compile_options(t PRIVATE /WX)\nendif()\n')
        self.assertNotIn("unguarded-frontend-flags", self.rules())

    def test_cmake_nested_arguments_brackets(self):
        text = 'set(X [=[ a # literal ) ]=])\n#[[ fake( /BAD ) ]]\nmessage("hash # )")\n'
        self.assertEqual([c[0] for c in guard.cmake_commands(text)], ["set", "message"])

    def test_exact_debt_passes_and_new_finding_fails(self):
        self.put("src/core/Pure.h", "HWND x;\n")
        report = self.scan()
        baseline = self.baseline(report)
        self.assertEqual(guard.evaluate(report, baseline)["status"], "passed")
        self.put("src/core/Pure.h", "HWND x; RECT y;\n")
        self.assertGreater(guard.evaluate(self.scan(), baseline)["counts"]["newViolations"], 0)

    def test_occurrence_growth_fails(self):
        self.put("src/core/Pure.h", "HWND x;\n")
        baseline = self.baseline(self.scan())
        self.put("src/core/Pure.h", "HWND x; HWND y;\n")
        self.assertGreater(guard.evaluate(self.scan(), baseline)["counts"]["newViolations"], 0)

    def test_resolved_debt_requires_tightening(self):
        self.put("src/core/Pure.h", "HWND x;\n")
        baseline = self.baseline(self.scan())
        self.put("src/core/Pure.h", "#pragma once\n")
        self.assertGreater(guard.evaluate(self.scan(), baseline)["counts"]["exceptionsToTighten"], 0)

    def test_wildcard_exception_rejected(self):
        self.put("src/core/Pure.h", "HWND x;\n")
        report = self.scan()
        baseline = self.baseline(report)
        baseline["exceptions"][0]["path"] = "src/**"
        with self.assertRaises(ValueError):
            guard.evaluate(report, baseline)

    def test_exception_requires_owner_reason_followup(self):
        self.put("src/core/Pure.h", "HWND x;\n")
        report = self.scan()
        baseline = self.baseline(report)
        baseline["exceptions"][0]["follow_up"] = ""
        with self.assertRaises(ValueError):
            guard.evaluate(report, baseline)

    def test_h333_registration_only(self):
        self.put("src/analysis/sandbox/ReplaySandbox.cpp",
                 'auto x = std::filesystem::path("build") / "Debug" / "runtime_replay_events_command.txt";\n')
        facts = self.scan()["findings"]
        self.assertTrue(any(f["rule"] == "legacy-replay-discovery" for f in facts))

def main():
    global FIXTURES
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    FIXTURES = args.output.resolve()
    if FIXTURES.exists():
        parser.error("Fixture output already exists")
    FIXTURES.mkdir(parents=True)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(GuardFixtures)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    guard.write_json(FIXTURES / "fixture-result.json",
                     dict(testsRun=result.testsRun, failures=len(result.failures),
                          errors=len(result.errors), skipped=len(result.skipped),
                          status="passed" if result.wasSuccessful() else "failed"))
    return 0 if result.wasSuccessful() else 1

if __name__ == "__main__":
    sys.exit(main())
