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
    def test_service_main_random_transitive_standard_boundary(self):
        header="src/network/internet/private/RuntimeInternetServiceRandomBytes.h"
        self.put(header,'#include "core/security/RuntimeSecureBytes.h"\n')
        self.put("src/core/security/RuntimeSecureBytes.h",'#include <span>\n')
        modules={"service-main-random":[header]}
        self.assertFalse(any(x["rule"].startswith("protected-") for x in self.scan(modules)["findings"]))
        self.put("src/core/security/RuntimeSecureBytes.h",'#include <windows.h>\n')
        self.assertIn("protected-platform",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_service_main_random_only_original_owner_and_oracle(self):
        header="src/network/internet/private/RuntimeInternetServiceRandomBytes.h"
        self.put(header,'class Source {};')
        self.put("src/network/Other.cpp",'#include "network/internet/private/RuntimeInternetServiceRandomBytes.h"\n')
        self.assertIn("service-main-random-private-owner",{x["rule"] for x in self.scan({"service-main-random":[header]})["findings"]})

    def test_service_main_random_bypass_ignores_comments_and_literals(self):
        header="src/network/internet/private/RuntimeInternetServiceRandomBytes.h"
        self.put(header,'class Source {};')
        owner="src/network/internet/RuntimeInternetServiceMain.cpp"
        self.put(owner,'// BCryptGenRandom()\nconst char* note="std::random_device";')
        modules={"service-main-random":[header]}
        self.assertNotIn("service-main-random-bypass",{x["rule"] for x in self.scan(modules)["findings"]})
        self.put(owner,'void f(){BCryptGenRandom();}')
        self.assertIn("service-main-random-bypass",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_service_main_random_rejects_erased_native_owner(self):
        header="src/network/internet/private/RuntimeInternetServiceRandomBytes.h"
        self.put(header,'class Source {void* context;};')
        self.assertIn("service-main-random-opaque",{x["rule"] for x in self.scan({"service-main-random":[header]})["findings"]})

    def test_lan_nonce_noise_source_transitive_standard_boundary(self):
        header="src/network/session/private/RuntimeLanNonceNoiseSource.h"
        self.put(header, '#include "core/security/RuntimeSecureBytes.h"\n')
        self.put("src/core/security/RuntimeSecureBytes.h", '#include <span>\n')
        self.put("src/runtime/contracts/RuntimeInternetMenuData.h", '#include <array>\n')
        modules={"lan-nonce-noise-source":[header]}
        self.assertFalse(any(x["rule"].startswith("protected-") for x in self.scan(modules)["findings"]))
        self.put("src/core/security/RuntimeSecureBytes.h", '#include <windows.h>\n')
        self.assertIn("protected-platform",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_lan_nonce_noise_source_remains_original_owner_private(self):
        header="src/network/session/private/RuntimeLanNonceNoiseSource.h"
        self.put(header, 'class Source {};')
        self.put("src/network/Other.cpp", '#include "network/session/private/RuntimeLanNonceNoiseSource.h"\n')
        self.put("tests/multiplayer/RuntimeLanNonceNoiseSourceSmoke.inl", 'void oracle(){}')
        self.put("src/runtime/Other.cpp", '#include "tests/multiplayer/RuntimeLanNonceNoiseSourceSmoke.inl"\n')
        rules={x["rule"] for x in self.scan({"lan-nonce-noise-source":[header]})["findings"]}
        self.assertIn("lan-noise-private-source",rules)
        self.assertIn("lan-noise-private-oracle",rules)

    def test_lan_nonce_noise_source_bypass_ignores_comments_and_literals(self):
        header="src/network/session/private/RuntimeLanNonceNoiseSource.h"
        self.put(header, 'class Source {};')
        owner="src/network/session/RuntimeMultiplayerLanSession.cpp"
        self.put(owner, '// BCryptGenRandom()\nconst char* note="std::random_device";')
        modules={"lan-nonce-noise-source":[header]}
        self.assertNotIn("lan-noise-source-bypass",{x["rule"] for x in self.scan(modules)["findings"]})
        self.put(owner, 'void f(){BCryptGenRandom();}')
        self.assertIn("lan-noise-source-bypass",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_lan_nonce_noise_source_rejects_erased_native_resources(self):
        header="src/network/session/private/RuntimeLanNonceNoiseSource.h"
        self.put(header, 'class Source { void* native; };')
        self.assertIn("lan-noise-opaque-source",{x["rule"] for x in self.scan({"lan-nonce-noise-source":[header]})["findings"]})

    def test_tunnel_prelude_source_transitive_standard_boundary(self):
        header="src/network/transport/private/RuntimeTunnelPreludeSource.h"
        self.put(header, '#include "core/security/RuntimeSecureBytes.h"\n')
        self.put("src/core/security/RuntimeSecureBytes.h", '#include <span>\n')
        self.put("src/runtime/contracts/RuntimeInternetMenuData.h", '#include <array>\n')
        modules={"tunnel-prelude-source":[header]}
        self.assertFalse(any(x["rule"].startswith("protected-") for x in self.scan(modules)["findings"]))
        self.put("src/core/security/RuntimeSecureBytes.h", '#include <windows.h>\n')
        self.assertIn("protected-platform",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_tunnel_prelude_source_remains_original_owner_private(self):
        header="src/network/transport/private/RuntimeTunnelPreludeSource.h"
        self.put(header, 'class Source {};')
        self.put("src/network/Other.cpp", '#include "network/transport/private/RuntimeTunnelPreludeSource.h"\n')
        self.put("tests/internet/RuntimeTunnelPreludeSourceSmoke.inl", 'void oracle(){}')
        self.put("src/runtime/Other.cpp", '#include "tests/internet/RuntimeTunnelPreludeSourceSmoke.inl"\n')
        rules={x["rule"] for x in self.scan({"tunnel-prelude-source":[header]})["findings"]}
        self.assertIn("tunnel-prelude-private-source",rules)
        self.assertIn("tunnel-prelude-private-oracle",rules)

    def test_tunnel_prelude_source_bypass_ignores_comments_and_literals(self):
        header="src/network/transport/private/RuntimeTunnelPreludeSource.h"
        self.put(header, 'class Source {};')
        owner="src/network/transport/RuntimeInternetTcpTunnelProxy.cpp"
        self.put(owner, '// BCryptGenRandom()\nconst char* note="std::random_device";')
        modules={"tunnel-prelude-source":[header]}
        self.assertNotIn("tunnel-prelude-source-bypass",{x["rule"] for x in self.scan(modules)["findings"]})
        self.put(owner, 'void f(){BCryptGenRandom();}')
        self.assertIn("tunnel-prelude-source-bypass",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_tunnel_prelude_source_rejects_erased_native_resources(self):
        header="src/network/transport/private/RuntimeTunnelPreludeSource.h"
        self.put(header, 'class Source { void* native; };')
        self.assertIn("tunnel-prelude-opaque-source",{x["rule"] for x in self.scan({"tunnel-prelude-source":[header]})["findings"]})

    def test_service_nonce_source_transitive_standard_boundary(self):
        header="src/network/transport/private/RuntimeInternetServiceNonceSource.h"
        self.put(header, '#include "core/security/RuntimeSecureBytes.h"\n')
        self.put("src/core/security/RuntimeSecureBytes.h", '#include <span>\n')
        self.put("src/runtime/contracts/RuntimeInternetMenuData.h", '#include <array>\n')
        modules={"service-nonce-source":[header]}
        self.assertFalse(any(x["rule"].startswith("protected-") for x in self.scan(modules)["findings"]))
        self.put("src/core/security/RuntimeSecureBytes.h", '#include <windows.h>\n')
        self.assertIn("protected-platform",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_service_nonce_source_remains_original_owner_private(self):
        header="src/network/transport/private/RuntimeInternetServiceNonceSource.h"
        self.put(header, 'class Source {};')
        self.put("src/network/Other.cpp", '#include "network/transport/private/RuntimeInternetServiceNonceSource.h"\n')
        self.put("tests/internet/RuntimeInternetServiceNonceSourceSmoke.inl", 'void oracle(){}')
        self.put("src/runtime/Other.cpp", '#include "tests/internet/RuntimeInternetServiceNonceSourceSmoke.inl"\n')
        rules={x["rule"] for x in self.scan({"service-nonce-source":[header]})["findings"]}
        self.assertIn("service-nonce-private-source",rules)
        self.assertIn("service-nonce-private-oracle",rules)

    def test_service_nonce_source_bypass_ignores_comments_and_literals(self):
        header="src/network/transport/private/RuntimeInternetServiceNonceSource.h"
        self.put(header, 'class Source {};')
        owner="src/network/transport/RuntimeInternetServiceTransport.cpp"
        self.put(owner, '// BCryptGenRandom()\nconst char* note="std::random_device";')
        modules={"service-nonce-source":[header]}
        self.assertNotIn("service-nonce-source-bypass",{x["rule"] for x in self.scan(modules)["findings"]})
        self.put(owner, 'void f(){BCryptGenRandom();}')
        self.assertIn("service-nonce-source-bypass",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_service_nonce_source_rejects_erased_native_resources(self):
        header="src/network/transport/private/RuntimeInternetServiceNonceSource.h"
        self.put(header, 'class Source { void* native; };')
        self.assertIn("service-nonce-opaque-source",{x["rule"] for x in self.scan({"service-nonce-source":[header]})["findings"]})

    def test_lan_secure_source_transitive_standard_boundary(self):
        header="src/ui/network/private/RuntimeLanMenuSecureSource.h"
        self.put(header, '#include "core/security/RuntimeSecureBytes.h"\n')
        self.put("src/core/security/RuntimeSecureBytes.h", '#include <span>\n')
        self.put("src/runtime/contracts/RuntimeInternetMenuData.h", '#include <array>\n')
        modules={"lan-menu-secure-source":[header]}
        self.assertFalse(any(x["rule"].startswith("protected-") for x in self.scan(modules)["findings"]))
        self.put("src/core/security/RuntimeSecureBytes.h", '#include <windows.h>\n')
        self.assertIn("protected-platform",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_lan_secure_source_remains_original_owner_private(self):
        header="src/ui/network/private/RuntimeLanMenuSecureSource.h"
        self.put(header, 'class Source {};')
        self.put("src/network/Other.cpp", '#include "ui/network/private/RuntimeLanMenuSecureSource.h"\n')
        self.put("tests/multiplayer/RuntimeLanMenuSecureSourceSmoke.inl", 'void oracle(){}')
        self.put("src/runtime/Other.cpp", '#include "tests/multiplayer/RuntimeLanMenuSecureSourceSmoke.inl"\n')
        rules={x["rule"] for x in self.scan({"lan-menu-secure-source":[header]})["findings"]}
        self.assertIn("lan-secure-private-source",rules)
        self.assertIn("lan-secure-private-oracle",rules)

    def test_lan_secure_source_bypass_ignores_comments_and_literals(self):
        header="src/ui/network/private/RuntimeLanMenuSecureSource.h"
        self.put(header, 'class Source {};')
        owner="src/ui/network/RuntimeMultiplayerMenu.cpp"
        self.put(owner, '// BCryptGenRandom()\nconst char* note="std::random_device";')
        modules={"lan-menu-secure-source":[header]}
        self.assertNotIn("lan-secure-source-bypass",{x["rule"] for x in self.scan(modules)["findings"]})
        self.put(owner, 'void f(){BCryptGenRandom();}')
        self.assertIn("lan-secure-source-bypass",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_lan_secure_source_rejects_erased_native_resources(self):
        header="src/ui/network/private/RuntimeLanMenuSecureSource.h"
        self.put(header, 'class Source { void* native; };')
        self.assertIn("lan-secure-opaque-source",{x["rule"] for x in self.scan({"lan-menu-secure-source":[header]})["findings"]})

    def test_internet_entropy_source_transitive_standard_boundary(self):
        header="src/ui/network/private/RuntimeInternetMenuEntropySource.h"
        self.put(header, '#include "core/security/RuntimeSecureBytes.h"\n#include "runtime/contracts/RuntimeInternetMenuData.h"\n')
        self.put("src/core/security/RuntimeSecureBytes.h", '#include <span>\n')
        self.put("src/runtime/contracts/RuntimeInternetMenuData.h", '#include <array>\n')
        modules={"internet-menu-entropy-source":[header]}
        self.assertFalse(any(x["rule"].startswith("protected-") for x in self.scan(modules)["findings"]))
        self.put("src/core/security/RuntimeSecureBytes.h", '#include <windows.h>\n')
        self.assertIn("protected-platform",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_internet_entropy_source_remains_original_owner_private(self):
        header="src/ui/network/private/RuntimeInternetMenuEntropySource.h"
        self.put(header, 'class Source {};')
        self.put("src/network/Other.cpp", '#include "ui/network/private/RuntimeInternetMenuEntropySource.h"\n')
        self.put("tests/internet/RuntimeInternetMenuEntropySourceSmoke.inl", 'void oracle(){}')
        self.put("src/runtime/Other.cpp", '#include "tests/internet/RuntimeInternetMenuEntropySourceSmoke.inl"\n')
        rules={x["rule"] for x in self.scan({"internet-menu-entropy-source":[header]})["findings"]}
        self.assertIn("internet-entropy-private-source",rules)
        self.assertIn("internet-entropy-private-oracle",rules)

    def test_internet_entropy_source_bypass_ignores_comments_and_literals(self):
        header="src/ui/network/private/RuntimeInternetMenuEntropySource.h"
        self.put(header, 'class Source {};')
        owner="src/ui/network/RuntimeInternetMultiplayerMenu.cpp"
        self.put(owner, '// BCryptGenRandom()\nconst char* note="std::random_device";')
        modules={"internet-menu-entropy-source":[header]}
        self.assertNotIn("internet-entropy-source-bypass",{x["rule"] for x in self.scan(modules)["findings"]})
        self.put(owner, 'void f(){BCryptGenRandom();}')
        self.assertIn("internet-entropy-source-bypass",{x["rule"] for x in self.scan(modules)["findings"]})

    def test_internet_entropy_source_rejects_erased_native_resources(self):
        header="src/ui/network/private/RuntimeInternetMenuEntropySource.h"
        self.put(header, 'class Source { void* native; };')
        self.assertIn("internet-entropy-opaque-source",{x["rule"] for x in self.scan({"internet-menu-entropy-source":[header]})["findings"]})

    def test_system_secure_bytes_accepts_standard_public_values(self):
        public = "src/core/security/RuntimeSecureBytes.h"
        self.put(public, "#include <span>\nstruct Result { int status; };\n")
        facts = self.scan({"system-secure-bytes": [public]})["findings"]
        self.assertFalse(any(f["rule"].startswith(("secure-bytes-", "protected-")) for f in facts))

    def test_system_secure_bytes_rejects_public_native_transitively(self):
        public = "src/core/security/RuntimeSecureBytes.h"
        self.put(public, '#include "core/security/Bad.h"\n')
        self.put("src/core/security/Bad.h", "#include <bcrypt.h>\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"system-secure-bytes": [public]})["findings"]})
        self.put(public, "void* nativeContext;\n")
        self.assertIn("secure-bytes-opaque-native", {f["rule"] for f in self.scan({"system-secure-bytes": [public]})["findings"]})

    def test_system_secure_bytes_never_substitutes_predictable_fallback(self):
        source = "src/core/security/RuntimeSecureBytes.cpp"
        for bad in ("std::random_device random;", "std::mt19937 random;", "auto now = std::chrono::steady_clock::now();", "int x = rand();"):
            self.put(source, bad)
            self.assertIn("secure-bytes-predictable-fallback", {f["rule"] for f in self.scan({"system-secure-bytes": []})["findings"]})

    def test_system_secure_bytes_private_reader_cannot_enter_production_callers(self):
        private = "core/security/private/RuntimeSecureBytesReader.h"
        self.put("src/" + private, "struct Reader {};\n")
        self.put("src/core/security/RuntimeSecureBytes.cpp", '#include "' + private + '"\n')
        self.assertNotIn("secure-bytes-private-reader", {f["rule"] for f in self.scan({"system-secure-bytes": []})["findings"]})
        self.put("src/network/internet/RuntimeInternetServiceMain.cpp", '#include "' + private + '"\n')
        self.assertIn("secure-bytes-private-reader", {f["rule"] for f in self.scan({"system-secure-bytes": []})["findings"]})

    def test_credential_certificate_factory_remains_with_same_owner(self):
        private = "network/internet/private/RuntimeInternetServiceCertificateFactoryWin32.h"
        self.put("src/" + private, "struct Factory {};\n")
        self.put("src/network/internet/RuntimeInternetServiceHostConfig.cpp", '#include "' + private + '"\n')
        self.assertNotIn("credential-certificate-native-consumer", {f["rule"] for f in self.scan({"credential-certificate-private": []})["findings"]})
        self.put("src/network/internet/RuntimeInternetServiceMain.cpp", '#include "' + private + '"\n')
        self.assertIn("credential-certificate-native-consumer", {f["rule"] for f in self.scan({"credential-certificate-private": []})["findings"]})

    def test_credential_certificate_oracle_cannot_enter_production(self):
        private = "tests/internet/RuntimeInternetServiceCredentialCertificateSmoke.inl"
        self.put(private, "void oracle();\n")
        self.put("tests/internet/RuntimeInternetTlsTransportSmoke.cpp", '#include "RuntimeInternetServiceCredentialCertificateSmoke.inl"\n')
        self.assertNotIn("credential-certificate-private-oracle", {f["rule"] for f in self.scan({"credential-certificate-private": []})["findings"]})
        self.put("src/network/internet/Bad.cpp", '#include "../../../tests/internet/RuntimeInternetServiceCredentialCertificateSmoke.inl"\n')
        self.assertIn("credential-certificate-private-oracle", {f["rule"] for f in self.scan({"credential-certificate-private": []})["findings"]})

    def test_tls_certificate_standard_borrow_values(self):
        public = "src/network/transport/RuntimeInternetTlsCredentials.h"
        self.put(public, "struct RuntimeInternetTlsServerCertificate {}; struct RuntimeInternetTlsPeerTrust {};\n")
        facts = self.scan({"tls-certificate-contract": [public]})["findings"]
        self.assertFalse(any(f["rule"].startswith(("tls-certificate-", "protected-")) for f in facts))

    def test_tls_certificate_rejects_public_native_or_integer_erasure(self):
        public = "src/network/transport/RuntimeInternetTlsTransport.h"
        for field in ("const void* nativeCertificateContext;", "void * nativeCertificateChainEngine;", "std::uintptr_t credential;"):
            self.put(public, field)
            facts = self.scan({"tls-certificate-contract": [public]})["findings"]
            self.assertIn("tls-certificate-opaque-native", {f["rule"] for f in facts})

    def test_tls_certificate_rejects_transitive_sdk(self):
        public = "src/network/transport/RuntimeInternetTlsCredentials.h"
        self.put(public, '#include "network/transport/BadTrust.h"\n')
        self.put("src/network/transport/BadTrust.h", "#include <wincrypt.h>\nPCCERT_CONTEXT cert;\n")
        facts = self.scan({"tls-certificate-contract": [public]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_tls_certificate_private_bridge_stays_with_existing_owners(self):
        private = "network/transport/private/RuntimeInternetTlsCertificateWin32.h"
        self.put("src/" + private, "struct Binding {};\n")
        self.put("src/network/transport/RuntimeInternetTlsTransport.cpp", '#include "' + private + '"\n')
        facts = self.scan({"tls-certificate-contract": []})["findings"]
        self.assertNotIn("tls-certificate-native-consumer", {f["rule"] for f in facts})
        self.put("src/ui/Public.h", '#include "' + private + '"\n')
        facts = self.scan({"tls-certificate-contract": []})["findings"]
        self.assertIn("tls-certificate-native-consumer", {f["rule"] for f in facts})

    def test_tls_certificate_native_oracle_cannot_enter_production(self):
        private = "tests/internet/RuntimeInternetTlsCertificateBorrowSmoke.inl"
        self.put(private, "void oracle();\n")
        self.put("tests/internet/RuntimeInternetTlsTransportSmoke.cpp", '#include "RuntimeInternetTlsCertificateBorrowSmoke.inl"\n')
        facts = self.scan({"tls-certificate-contract": []})["findings"]
        self.assertNotIn("tls-certificate-private-oracle", {f["rule"] for f in facts})
        self.put("src/network/transport/Bad.cpp", '#include "../../../tests/internet/RuntimeInternetTlsCertificateBorrowSmoke.inl"\n')
        facts = self.scan({"tls-certificate-contract": []})["findings"]
        self.assertIn("tls-certificate-private-oracle", {f["rule"] for f in facts})

    def test_video_caller_oracle_stays_with_original_main_owner(self):
        private="ui/main_menu/diagnostics/RuntimeMainVideoCallerBoundarySmoke.inl";self.put("src/"+private,"struct Oracle{};")
        self.put("src/ui/main_menu/RuntimeMainMenu.cpp",'#include "'+private+'"'+chr(10))
        self.assertNotIn("video-caller-private-oracle",{f["rule"] for f in self.scan({"video-caller-private":[]})["findings"]})
        for bad in ("src/core/Public.h","src/runtime/host/RuntimeShell.cpp"):
            self.put(bad,'#include "'+private+'"'+chr(10))
            self.assertIn("video-caller-private-oracle",{f["rule"] for f in self.scan({"video-caller-private":[]})["findings"]})
            self.put(bad,"struct Value{};")

    def test_bink_video_parent_is_typed_and_standard_transitively(self):
        public="src/ui/main_menu/RuntimeMainMenu.h"
        self.put(public,"bool runRuntimeBinkVideo(Config const&,RuntimeWindowId hostWindow);")
        self.assertFalse(any(f["rule"].startswith(("bink-video-","protected-")) for f in self.scan({"bink-video-parent":[public]})["findings"]))
        for old in ("void* hostWindow","void * hostWindow","std::uintptr_t hostWindow","HWND hostWindow"):
            self.put(public,"bool runRuntimeBinkVideo(Config const&,"+old+");")
            self.assertIn("bink-video-opaque-parent",{f["rule"] for f in self.scan({"bink-video-parent":[public]})["findings"]})
        self.put(public,'#include "runtime/window/BadParent.h"'+chr(10)+'bool runRuntimeBinkVideo(Config const&,RuntimeWindowId hostWindow);')
        self.put("src/runtime/window/BadParent.h",'#include <windows.h>'+chr(10))
        self.assertIn("protected-platform",{f["rule"] for f in self.scan({"bink-video-parent":[public]})["findings"]})

    def test_bink_parent_oracle_remains_private_to_original_video_owner(self):
        private="ui/campaign/diagnostics/RuntimeBinkParentIdentitySmoke.inl";self.put("src/"+private,"struct Oracle{};")
        self.put("src/ui/main_menu/RuntimeMainMenu.cpp",'#include "'+private+'"'+chr(10))
        self.assertNotIn("bink-parent-private-oracle",{f["rule"] for f in self.scan({"bink-video-parent":[]})["findings"]})
        for forbidden in ("src/core/Public.h","src/runtime/host/RuntimeShell.cpp"):
            self.put(forbidden,'#include "'+private+'"'+chr(10))
            self.assertIn("bink-parent-private-oracle",{f["rule"] for f in self.scan({"bink-video-parent":[]})["findings"]})
            self.put(forbidden,"struct Value{};")

    def test_async_sound_standard_closure_rejects_native_live_and_opaque(self):
        header="src/audio/backend/RuntimeAsyncSoundDevice.h";base="src/audio/menu/RuntimeMenuMusicDevice.h"
        self.put(header,'#include "audio/menu/RuntimeMenuMusicDevice.h"\nstruct Device:Base{};');self.put(base,"struct Base{};")
        self.assertFalse(any(f["rule"].startswith(("protected-","async-sound-")) for f in self.scan({"async-sound-device":[header]})["findings"]))
        for bad in ('#include <windows.h>\n','struct GameWorld;','struct RuntimeWindowState;','struct Value{void* device;};','struct Value{std::uintptr_t handle;};'):
            self.put(header,bad)
            self.assertTrue(any(f["rule"].startswith(("protected-","async-sound-")) for f in self.scan({"async-sound-device":[header]})["findings"]))
        self.put(header,'#include "audio/menu/RuntimeMenuMusicDevice.h"\n');self.put(base,'#include <mmsystem.h>\n')
        self.assertIn("protected-platform",{f["rule"] for f in self.scan({"async-sound-device":[header]})["findings"]})

    def test_async_sound_private_factory_only_main_provider_and_native_test(self):
        native="audio/backend/win32/RuntimeAsyncSoundDeviceWin32.h";self.put("src/"+native,"struct Factory{};")
        shell = "src/runtime/host/RuntimeShell.cpp"
        for caller in ("src/ui/main_menu/RuntimeMainMenu.cpp", shell, "src/runtime/session/win32/RuntimeNativeSessionDevices.cpp","src/audio/backend/win32/RuntimeAsyncSoundDeviceWin32.cpp","tests/audio/async_sound_device/AsyncSoundNativeContract.cpp"):
            self.put(caller,'#include "'+native+'"\n')
        self.assertNotIn("async-sound-native-consumer",{f["rule"] for f in self.scan({"async-sound-device":[]})["findings"]})
        self.put(shell, '#include "' + native + '"\nvoid compose() { createAsyncSoundDeviceWin32(); }\n')
        self.assertIn("async-sound-native-factory-owner", {f["rule"] for f in self.scan({"async-sound-device":[]})["findings"]})
        for caller in ("src/audio/menu/Public.h","src/core/io/Other.h"):
            self.put(caller,'#include "'+native+'"\n')
            self.assertIn("async-sound-native-consumer",{f["rule"] for f in self.scan({"async-sound-device":[]})["findings"]})
            self.put(caller,"struct Value{};")

        fixture="audio/events/diagnostics/RuntimeEvaPlaySoundFallbackSmoke.inl"
        self.put("src/"+fixture,"struct Fixture{};")
        self.put("src/runtime/host/RuntimeShell.cpp",'#include "'+fixture+'"'+chr(10))
        self.assertNotIn("async-sound-fixture-consumer",{f["rule"] for f in self.scan({"async-sound-device":[]})["findings"]})
        self.put("src/core/io/Other.h",'#include "'+fixture+'"'+chr(10))
        self.assertIn("async-sound-fixture-consumer",{f["rule"] for f in self.scan({"async-sound-device":[]})["findings"]})

    def test_async_sound_rejects_native_production_bypass_in_both_real_consumers(self):
        for main in ("src/ui/main_menu/RuntimeMainMenu.cpp","src/runtime/host/RuntimeShell.cpp"):
            self.put(main,'const char* label="PlaySoundW()";void f(){device.playFile(path);}/* PlaySoundW(a,b,c); */')
            self.assertNotIn("async-sound-native-bypass",{f["rule"] for f in self.scan({"async-sound-device":[]})["findings"]})
            for call in ("PlaySoundW","PlaySoundA"):
                self.put(main,'void f(){if(true){'+call+'(a,b,c);}}')
                self.assertIn("async-sound-native-bypass",{f["rule"] for f in self.scan({"async-sound-device":[]})["findings"]})
            self.put(main,"")

    def test_menu_sound_standard_closure_rejects_native_live_and_opaque(self):
        header="src/audio/events/RuntimeMenuSoundDevice.h";base="src/audio/menu/RuntimeMenuMusicDevice.h"
        self.put(header,'#include "audio/menu/RuntimeMenuMusicDevice.h"\nstruct Device:Base{};');self.put(base,"struct Base{};")
        self.assertFalse(any(f["rule"].startswith(("protected-","menu-sound-")) for f in self.scan({"menu-sound-device":[header]})["findings"]))
        for bad in ('#include <windows.h>\n','struct GameWorld;','struct RuntimeWindowState;','struct Value{void* device;};','struct Value{std::uintptr_t handle;};'):
            self.put(header,bad)
            self.assertTrue(any(f["rule"].startswith(("protected-","menu-sound-")) for f in self.scan({"menu-sound-device":[header]})["findings"]))
        self.put(header,'#include "audio/menu/RuntimeMenuMusicDevice.h"\n');self.put(base,'#include <mmsystem.h>\n')
        self.assertIn("protected-platform",{f["rule"] for f in self.scan({"menu-sound-device":[header]})["findings"]})

    def test_menu_sound_private_factory_only_main_provider_and_native_test(self):
        native="audio/events/win32/RuntimeMenuSoundWin32.h";self.put("src/"+native,"struct Factory{};")
        for caller in ("tests/audio/RuntimeMenuSfxClockContract.cpp","src/ui/skirmish/RuntimeSkirmishMenu.cpp","src/audio/events/win32/RuntimeMenuSoundWin32.cpp","tests/audio/menu_sfx_device/MenuSoundNativeContract.cpp"):
            self.put(caller,'#include "'+native+'"\n')
        self.assertNotIn("menu-sound-native-consumer",{f["rule"] for f in self.scan({"menu-sound-device":[]})["findings"]})
        for caller in ("src/audio/menu/Public.h","src/core/io/Other.h"):
            self.put(caller,'#include "'+native+'"\n')
            self.assertIn("menu-sound-native-consumer",{f["rule"] for f in self.scan({"menu-sound-device":[]})["findings"]})
            self.put(caller,"struct Value{};")

    def test_menu_sound_rejects_mci_production_bypass_leaves_effect_owner(self):
        main="src/audio/events/RuntimeMenuSoundMci.inl"
        self.put(main,'void f(){PlaySoundW(path,a,b);device.playFromStart();}// mciSendStringW();\nconst char* label="mciSendStringW()";')
        self.assertNotIn("menu-sound-native-bypass",{f["rule"] for f in self.scan({"menu-sound-device":[]})["findings"]})
        for call in ("mciSendStringW","mciSendStringA"):
            self.put(main,'void startMusic(){if(true){'+call+'(a,b,c,d);}}')
            self.assertIn("menu-sound-native-bypass",{f["rule"] for f in self.scan({"menu-sound-device":[]})["findings"]})

    def test_campaign_map_audio_standard_closure_rejects_native_live_and_opaque(self):
        header="src/audio/menu/RuntimeCampaignMapAudioDevice.h";base="src/audio/menu/RuntimeMenuMusicDevice.h"
        self.put(header,'#include "audio/menu/RuntimeMenuMusicDevice.h"\nstruct Device:Base{};');self.put(base,"struct Base{};")
        self.assertFalse(any(f["rule"].startswith(("protected-","campaign-map-audio-")) for f in self.scan({"campaign-map-audio-device":[header]})["findings"]))
        for bad in ('#include <windows.h>\n','struct GameWorld;','struct RuntimeWindowState;','struct Value{void* device;};','struct Value{std::uintptr_t handle;};'):
            self.put(header,bad)
            self.assertTrue(any(f["rule"].startswith(("protected-","campaign-map-audio-")) for f in self.scan({"campaign-map-audio-device":[header]})["findings"]))
        self.put(header,'#include "audio/menu/RuntimeMenuMusicDevice.h"\n');self.put(base,'#include <mmsystem.h>\n')
        self.assertIn("protected-platform",{f["rule"] for f in self.scan({"campaign-map-audio-device":[header]})["findings"]})

    def test_campaign_map_audio_private_factory_only_main_provider_and_native_test(self):
        native="audio/menu/win32/RuntimeCampaignMapAudioWin32.h";self.put("src/"+native,"struct Factory{};")
        for caller in ("src/ui/campaign/RuntimeCampaignMenu.cpp","src/audio/menu/win32/RuntimeCampaignMapAudioWin32.cpp","tests/audio/campaign_map_audio/CampaignMapAudioNativeContract.cpp"):
            self.put(caller,'#include "'+native+'"\n')
        self.assertNotIn("campaign-map-audio-native-consumer",{f["rule"] for f in self.scan({"campaign-map-audio-device":[]})["findings"]})
        for caller in ("src/audio/menu/Public.h","src/core/io/Other.h"):
            self.put(caller,'#include "'+native+'"\n')
            self.assertIn("campaign-map-audio-native-consumer",{f["rule"] for f in self.scan({"campaign-map-audio-device":[]})["findings"]})
            self.put(caller,"struct Value{};")

    def test_campaign_map_audio_rejects_mci_production_bypass_leaves_effect_owner(self):
        main="src/ui/campaign/RuntimeCampaignMenu.cpp"
        self.put(main,'void f(){PlaySoundW(path,a,b);device.playFromStart();}// mciSendStringW();\nconst char* label="mciSendStringW()";')
        self.assertNotIn("campaign-map-audio-native-bypass",{f["rule"] for f in self.scan({"campaign-map-audio-device":[]})["findings"]})
        for call in ("mciSendStringW","mciSendStringA"):
            self.put(main,'void startMusic(){if(true){'+call+'(a,b,c,d);}}')
            self.assertIn("campaign-map-audio-native-bypass",{f["rule"] for f in self.scan({"campaign-map-audio-device":[]})["findings"]})

    def test_main_music_standard_closure_rejects_native_live_and_opaque(self):
        header="src/audio/menu/RuntimeMainMenuMusicDevice.h";base="src/audio/menu/RuntimeMenuMusicDevice.h"
        self.put(header,'#include "audio/menu/RuntimeMenuMusicDevice.h"\nstruct Device:Base{};');self.put(base,"struct Base{};")
        self.assertFalse(any(f["rule"].startswith(("protected-","main-music-")) for f in self.scan({"main-music-device":[header]})["findings"]))
        for bad in ('#include <windows.h>\n','struct GameWorld;','struct RuntimeWindowState;','struct Value{void* device;};','struct Value{std::uintptr_t handle;};'):
            self.put(header,bad)
            self.assertTrue(any(f["rule"].startswith(("protected-","main-music-")) for f in self.scan({"main-music-device":[header]})["findings"]))
        self.put(header,'#include "audio/menu/RuntimeMenuMusicDevice.h"\n');self.put(base,'#include <mmsystem.h>\n')
        self.assertIn("protected-platform",{f["rule"] for f in self.scan({"main-music-device":[header]})["findings"]})

    def test_main_music_private_factory_only_main_provider_and_native_test(self):
        native="audio/menu/win32/RuntimeMainMenuMusicWin32.h";self.put("src/"+native,"struct Factory{};")
        for caller in ("src/ui/main_menu/RuntimeMainMenu.cpp","src/audio/menu/win32/RuntimeMainMenuMusicWin32.cpp","tests/audio/main_music_device/MainMusicNativeContract.cpp"):
            self.put(caller,'#include "'+native+'"\n')
        self.assertNotIn("main-music-native-consumer",{f["rule"] for f in self.scan({"main-music-device":[]})["findings"]})
        for caller in ("src/audio/menu/Public.h","src/core/io/Other.h"):
            self.put(caller,'#include "'+native+'"\n')
            self.assertIn("main-music-native-consumer",{f["rule"] for f in self.scan({"main-music-device":[]})["findings"]})
            self.put(caller,"struct Value{};")

    def test_main_music_rejects_mci_production_bypass_leaves_effect_owner(self):
        main="src/ui/main_menu/RuntimeMainMenu.cpp"
        self.put(main,'void f(){PlaySoundW(path,a,b);device.playFromStart();}// mciSendStringW();\nconst char* label="mciSendStringW()";')
        self.assertNotIn("main-music-native-bypass",{f["rule"] for f in self.scan({"main-music-device":[]})["findings"]})
        for call in ("mciSendStringW","mciSendStringA"):
            self.put(main,'void startMusic(){if(true){'+call+'(a,b,c,d);}}')
            self.assertIn("main-music-native-bypass",{f["rule"] for f in self.scan({"main-music-device":[]})["findings"]})

    def test_legacy_wave_standard_closure_rejects_native_live_and_opaque(self):
        header="src/audio/backend/RuntimeLegacyWaveDevice.h"
        self.put(header,"struct PlaybackResult{};")
        self.assertFalse(any(f["rule"].startswith(("protected-","legacy-wave-")) for f in self.scan({"legacy-wave-device":[header]})["findings"]))
        for bad in ('#include <windows.h>\n','struct GameWorld;','struct RuntimeWindowState;','struct Value{void* device;};','struct Value{std::uintptr_t handle;};'):
            self.put(header,bad)
            self.assertTrue(any(f["rule"].startswith(("protected-","legacy-wave-")) for f in self.scan({"legacy-wave-device":[header]})["findings"]))

    def test_legacy_wave_private_factory_only_composition_provider_and_native_test(self):
        native="audio/backend/win32/RuntimeLegacyWaveDeviceWin32.h";self.put("src/"+native,"struct Factory{};")
        for caller in ("src/runtime/session/win32/RuntimeNativeSessionDevices.cpp","src/audio/backend/win32/RuntimeLegacyWaveDeviceWin32.cpp","tests/audio/legacy_wave/LegacyWaveNativeContract.cpp"):
            self.put(caller,'#include "'+native+'"\n')
        self.assertNotIn("legacy-wave-native-consumer",{f["rule"] for f in self.scan({"legacy-wave-device":[]})["findings"]})
        for caller in ("src/audio/backend/Public.h","src/core/io/Other.h","src/runtime/host/RuntimeShell.cpp"):
            self.put(caller,'#include "'+native+'"\n')
            self.assertIn("legacy-wave-native-consumer",{f["rule"] for f in self.scan({"legacy-wave-device":[]})["findings"]})
            self.put(caller,"struct Value{};")

    def test_legacy_wave_rejects_production_bypass_preserves_diagnostic_oracle(self):
        shell="src/runtime/host/RuntimeShell.cpp"
        self.put(shell,'bool validateRuntimeMusicPlayback(){mciSendStringW(a,b,c,d);}\nvoid startRuntimeMusic(){device.playFromStart(path,0);}')
        self.assertNotIn("legacy-wave-native-bypass",{f["rule"] for f in self.scan({"legacy-wave-device":[]})["findings"]})
        for function in ("closeRuntimeEvaMciChannel","runtimeEvaMciChannelIsPlaying","playRuntimeEvaOnDedicatedChannel","startRuntimeMusic","stopRuntimeMusic","ensureRuntimeMusicPlaying"):
            self.put(shell,'void '+function+'(){// mciSendStringW();\nconst char* label="mciSendStringW()";}')
            self.assertNotIn("legacy-wave-native-bypass",{f["rule"] for f in self.scan({"legacy-wave-device":[]})["findings"]})
            self.put(shell,'void '+function+'(){if(true){mciSendStringW(a,b,c,d);}}')
            self.assertIn("legacy-wave-native-bypass",{f["rule"] for f in self.scan({"legacy-wave-device":[]})["findings"]})
        self.put(shell,'std::wstring encodeRuntimeLegacyWaveDevicePath(const std::string& path){return widenUtf8(path);}')
        self.assertIn("legacy-wave-native-token-bypass",{f["rule"] for f in self.scan({"legacy-wave-device":[]})["findings"]})
        self.put(shell,'std::wstring encodeRuntimeLegacyWaveDevicePath(const std::string& path){return widenUtf8(runtimeVoiceUtf8PathFromLegacyNative(path));}')
        self.assertNotIn("legacy-wave-native-token-bypass",{f["rule"] for f in self.scan({"legacy-wave-device":[]})["findings"]})

    def test_input_standard_root_cannot_import_private_file_readiness(self):
        self.put("src/ui/input/Input.h", '#include "ui/input/diagnostics/RuntimeInputPreferenceFixtureReadiness.inl"\n')
        self.put("src/ui/input/diagnostics/RuntimeInputPreferenceFixtureReadiness.inl", '#include <windows.h>\nHANDLE h;\n')
        r=self.scan({"input-controllers":["src/ui/input/Input.h"]})
        rules={x["rule"] for x in r["findings"]}
        self.assertIn("protected-dependency",rules)
        self.assertIn("protected-platform",rules)

    def test_input_public_header_rejects_direct_native_header(self):
        self.put("src/ui/input/Input.h", '#include <windows.h>\n')
        r=self.scan({"input-controllers":["src/ui/input/Input.h"]})
        self.assertIn("protected-platform", {x["rule"] for x in r["findings"]})

    def test_input_cpp_rejects_transitive_native_dependency(self):
        self.put("src/ui/input/Input.cpp", '#include "core/Hidden.h"\n')
        self.put("src/core/Hidden.h", '#include <windows.h>\n')
        r=self.scan({"input-controllers":["src/ui/input/Input.cpp"]})
        self.assertIn("protected-platform", {x["rule"] for x in r["findings"]})

    def test_input_rejects_hidden_host_state(self):
        self.put("src/ui/input/Input.h", '#include "core/Hidden.h"\n')
        self.put("src/core/Hidden.h", 'struct RuntimeWindowState;\n')
        r=self.scan({"input-controllers":["src/ui/input/Input.h"]})
        self.assertIn("protected-host-state", {x["rule"] for x in r["findings"]})

    def test_input_rejects_other_ui_owner(self):
        self.put("src/ui/input/Input.h", '#include "ui/Hidden.h"\n')
        self.put("src/ui/Hidden.h", 'struct Hidden {};\n')
        r=self.scan({"input-controllers":["src/ui/input/Input.h"]})
        self.assertIn("protected-dependency", {x["rule"] for x in r["findings"]})

    def test_input_standard_roots_and_existing_clock_are_allowed(self):
        self.put("src/ui/input/Input.cpp", '#include "ui/input/Input.h"\n')
        self.put("src/ui/input/Input.h", '#include "ui/time/RuntimeUiClock.h"\n#include <cstdint>\n')
        self.put("src/ui/time/RuntimeUiClock.h", '#include <cstdint>\nstruct Clock {};\n')
        r=self.scan({"input-controllers":["src/ui/input/Input.h","src/ui/input/Input.cpp"]})
        self.assertFalse(any(x["rule"].startswith("protected-") for x in r["findings"]))

    def test_geometry_scope_requires_standard_viewport(self):
        header="src/render/geometry/RuntimeGeometryQueryScope.h"
        self.put(header,"class RuntimeGeometryQueryScope{RuntimeGeometryQueryScope(const GameWorld& w,const RECT& v);};")
        self.assertIn("geometry-scope-viewport-value",{r["rule"] for r in self.scan({"geometry-scope-viewport":[header]})["findings"]})

    def test_geometry_scope_reuses_only_canonical_pixel_header(self):
        header="src/render/geometry/RuntimeGeometryQueryScope.h"
        decl="class RuntimeGeometryQueryScope{RuntimeGeometryQueryScope(const GameWorld& w,const RuntimeInputPixelRect& v);};"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n'+decl)
        self.put("src/ui/input/RuntimeInputPixelValues.h",'#include <cstdint>\n')
        self.assertFalse(any(r["rule"].startswith("protected-") for r in self.scan({"geometry-scope-viewport":[header]})["findings"]))
        self.put("src/ui/input/RuntimeInputPixelValues.h",'#include <windows.h>\n')
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"geometry-scope-viewport":[header]})["findings"]})
        self.put(header,'#include "ui/input/Other.h"\n'+decl);self.put("src/ui/input/Other.h",'struct Other{};')
        self.assertIn("protected-dependency",{r["rule"] for r in self.scan({"geometry-scope-viewport":[header]})["findings"]})

    def test_hit_span_point_query_requires_canonical_signature(self):
        header="src/render/geometry/RuntimeSpriteHitSpanQueries.h"
        self.put(header,"bool runtimeSpriteHitSpansContain(const std::vector<RuntimeSpriteHitSpan>& spans,POINT point);")
        self.assertIn("hit-span-query-point-value",{r["rule"] for r in self.scan({"hit-span-point-query":[header]})["findings"]})

    def test_hit_span_point_query_reuses_only_canonical_pixel_header(self):
        header="src/render/geometry/RuntimeSpriteHitSpanQueries.h"
        signature="bool runtimeSpriteHitSpansContain(const std::vector<RuntimeSpriteHitSpan>& spans,RuntimeInputPixelPoint point);"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n'+signature)
        self.put("src/ui/input/RuntimeInputPixelValues.h",'#include <cstdint>\n')
        self.assertFalse(any(r["rule"].startswith("protected-") for r in self.scan({"hit-span-point-query":[header]})["findings"]))
        self.put("src/ui/input/RuntimeInputPixelValues.h",'#include <windows.h>\n')
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"hit-span-point-query":[header]})["findings"]})
        self.put(header,'#include "ui/input/Other.h"\n'+signature)
        self.put("src/ui/input/Other.h",'struct Other{};')
        self.assertIn("protected-dependency",{r["rule"] for r in self.scan({"hit-span-point-query":[header]})["findings"]})

    def test_mobile_probe_requires_standard_fields(self):
        self.put("src/render/geometry/RuntimeMobileSpriteBoundsProbe.h", "struct Probe{RECT spriteBounds{};RuntimeInputPixelRect renderedBounds{};POINT imageOrigin{};};")
        rules={r["rule"] for r in self.scan({"mobile-probe-values":["src/render/geometry/RuntimeMobileSpriteBoundsProbe.h"]})["findings"]}
        self.assertIn("mobile-probe-pixel-values",rules)

    def test_mobile_probe_reuses_only_canonical_pixel_header(self):
        header="src/render/geometry/RuntimeMobileSpriteBoundsProbe.h"
        fields="struct Probe{RuntimeInputPixelRect spriteBounds{};RuntimeInputPixelRect renderedBounds{};RuntimeInputPixelPoint imageOrigin{};};"
        self.put(header, '#include "ui/input/RuntimeInputPixelValues.h"\n'+fields)
        self.put("src/ui/input/RuntimeInputPixelValues.h", '#include <cstdint>\n')
        self.assertFalse(any(r["rule"].startswith("protected-") for r in self.scan({"mobile-probe-values":[header]})["findings"]))
        self.put("src/ui/input/RuntimeInputPixelValues.h", '#include <windows.h>\n')
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"mobile-probe-values":[header]})["findings"]})
        self.put(header, '#include "ui/input/Other.h"\n'+fields)
        self.put("src/ui/input/Other.h", 'struct Other{};')
        self.assertIn("protected-dependency",{r["rule"] for r in self.scan({"mobile-probe-values":[header]})["findings"]})

    def test_infantry_muzzle_requires_canonical_point_contract(self):
        header="src/render/geometry/RuntimeInfantryMuzzleQuery.h"
        self.put(header,"bool resolveRuntimeInfantryWeaponMuzzlePoint(const WorldObjectInstance& o,const POINT& p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,POINT& r);")
        self.assertIn("infantry-muzzle-pixel-borrow-signature",{r["rule"] for r in self.scan({"infantry-muzzle-query":[header]})["findings"]})

    def test_infantry_muzzle_keeps_center_const_and_result_mutable_borrows(self):
        header="src/render/geometry/RuntimeInfantryMuzzleQuery.h"
        for center,result,valid in (("const RuntimeInputPixelPoint&","RuntimeInputPixelPoint&",True),("RuntimeInputPixelPoint","RuntimeInputPixelPoint&",False),("RuntimeInputPixelPoint&","RuntimeInputPixelPoint&",False),("const RuntimeInputPixelPoint&","const RuntimeInputPixelPoint&",False)):
            self.put(header,"bool resolveRuntimeInfantryWeaponMuzzlePoint(const WorldObjectInstance& o,"+center+" p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,"+result+" r);")
            rules={r["rule"] for r in self.scan({"infantry-muzzle-query":[header]})["findings"]}
            self.assertEqual("infantry-muzzle-pixel-borrow-signature" not in rules,valid)

    def test_marine_muzzle_requires_canonical_point_contract(self):
        header="src/render/geometry/RuntimeMarineMuzzleQuery.h"
        self.put(header,"bool resolveRuntimeMarineWeaponMuzzlePoint(const WorldObjectInstance& o,const POINT& p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,POINT& r);")
        self.assertIn("infantry-muzzle-pixel-borrow-signature",{r["rule"] for r in self.scan({"marine-muzzle-query":[header]})["findings"]})

    def test_marine_muzzle_keeps_center_const_and_result_mutable_borrows(self):
        header="src/render/geometry/RuntimeMarineMuzzleQuery.h"
        for center,result,valid in (("const RuntimeInputPixelPoint&","RuntimeInputPixelPoint&",True),("RuntimeInputPixelPoint","RuntimeInputPixelPoint&",False),("RuntimeInputPixelPoint&","RuntimeInputPixelPoint&",False),("const RuntimeInputPixelPoint&","const RuntimeInputPixelPoint&",False)):
            self.put(header,"bool resolveRuntimeMarineWeaponMuzzlePoint(const WorldObjectInstance& o,"+center+" p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,"+result+" r);")
            rules={r["rule"] for r in self.scan({"marine-muzzle-query":[header]})["findings"]}
            self.assertEqual("infantry-muzzle-pixel-borrow-signature" not in rules,valid)

    def test_mobile_sprite_bounds_requires_canonical_values(self):
        header="src/render/geometry/RuntimeMobileSpriteBoundsQuery.h"
        self.put(header,"bool resolveRuntimeMobileObjectSpriteBounds(const WorldObjectInstance& o,const POINT& p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,RECT& r);")
        self.assertIn("mobile-sprite-bounds-pixel-borrow-signature",{r["rule"] for r in self.scan({"mobile-sprite-bounds-query":[header]})["findings"]})

    def test_mobile_sprite_bounds_keeps_const_input_and_mutable_output_borrows(self):
        header="src/render/geometry/RuntimeMobileSpriteBoundsQuery.h"
        for center,bounds,valid in (("const RuntimeInputPixelPoint&","RuntimeInputPixelRect&",True),("RuntimeInputPixelPoint&","RuntimeInputPixelRect&",False),("const RuntimeInputPixelPoint&","const RuntimeInputPixelRect&",False),("const RuntimeInputPixelPoint&","RuntimeInputPixelRect",False)):
            self.put(header,"bool resolveRuntimeMobileObjectSpriteBounds(const WorldObjectInstance& o,"+center+" p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,"+bounds+" r);")
            rules={r["rule"] for r in self.scan({"mobile-sprite-bounds-query":[header]})["findings"]}
            self.assertEqual("mobile-sprite-bounds-pixel-borrow-signature" not in rules,valid)

    def test_object_shp_bounds_requires_canonical_values(self):
        header="src/render/geometry/RuntimeObjectShpBoundsQuery.h"
        self.put(header,"bool resolveRuntimeObjectShpSpriteBounds(const WorldObjectInstance& o,const POINT& p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,RECT& r,bool stable=false);")
        self.assertIn("object-shp-bounds-pixel-borrow-default-signature",{r["rule"] for r in self.scan({"object-shp-bounds-query":[header]})["findings"]})

    def test_object_shp_bounds_keeps_borrows_and_final_bool_default_false(self):
        header="src/render/geometry/RuntimeObjectShpBoundsQuery.h"
        for center,tail,valid in (("const RuntimeInputPixelPoint&","RuntimeInputPixelRect& r,bool stable=false",True),("RuntimeInputPixelPoint&","RuntimeInputPixelRect& r,bool stable=false",False),("const RuntimeInputPixelPoint&","const RuntimeInputPixelRect& r,bool stable=false",False),("const RuntimeInputPixelPoint&","RuntimeInputPixelRect& r,bool stable=true",False),("const RuntimeInputPixelPoint&","RuntimeInputPixelRect& r,bool stable",False)):
            self.put(header,"bool resolveRuntimeObjectShpSpriteBounds(const WorldObjectInstance& o,"+center+" p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,"+tail+");")
            rules={r["rule"] for r in self.scan({"object-shp-bounds-query":[header]})["findings"]}
            self.assertEqual("object-shp-bounds-pixel-borrow-default-signature" not in rules,valid)

    def test_spawned_missile_bounds_requires_canonical_values(self):
        header="src/render/geometry/RuntimeSpawnedMissileBoundsQuery.h"
        self.put(header,"bool resolveRuntimeSpawnedMissileSpriteBounds(const WorldObjectInstance& o,const POINT& p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,RECT& r);")
        self.assertIn("spawned-missile-bounds-pixel-borrow-signature",{r["rule"] for r in self.scan({"spawned-missile-bounds-query":[header]})["findings"]})

    def test_spawned_missile_bounds_keeps_const_input_and_mutable_output(self):
        header="src/render/geometry/RuntimeSpawnedMissileBoundsQuery.h"
        for center,bounds,valid in (("const RuntimeInputPixelPoint&","RuntimeInputPixelRect&",True),("RuntimeInputPixelPoint&","RuntimeInputPixelRect&",False),("const RuntimeInputPixelPoint&","const RuntimeInputPixelRect&",False),("const RuntimeInputPixelPoint&","RuntimeInputPixelRect",False)):
            self.put(header,"bool resolveRuntimeSpawnedMissileSpriteBounds(const WorldObjectInstance& o,"+center+" p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,"+bounds+" r);")
            rules={r["rule"] for r in self.scan({"spawned-missile-bounds-query":[header]})["findings"]}
            self.assertEqual("spawned-missile-bounds-pixel-borrow-signature" not in rules,valid)

    def test_voxel_muzzle_requires_canonical_point_contract(self):
        header="src/render/geometry/RuntimeVoxelMuzzleQuery.h"
        self.put(header,"bool resolveRuntimeVoxelWeaponMuzzlePoint(const WorldObjectInstance& o,const POINT& p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,int f,int l,int h,int facing,POINT& r,bool endpoint=true);")
        self.assertIn("voxel-muzzle-pixel-borrow-default-signature",{r["rule"] for r in self.scan({"voxel-muzzle-query":[header]})["findings"]})

    def test_voxel_muzzle_keeps_last_bool_default_and_mutable_point(self):
        header="src/render/geometry/RuntimeVoxelMuzzleQuery.h"
        for tail,valid in (("RuntimeInputPixelPoint& r,bool endpoint=true",True),("bool endpoint,RuntimeInputPixelPoint& r",False),("RuntimeInputPixelPoint& r,bool endpoint=false",False),("RuntimeInputPixelPoint& r,bool endpoint",False),("const RuntimeInputPixelPoint& r,bool endpoint=true",False)):
            self.put(header,"bool resolveRuntimeVoxelWeaponMuzzlePoint(const WorldObjectInstance& o,const RuntimeInputPixelPoint& p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,int f,int l,int h,int facing,"+tail+");")
            rules={r["rule"] for r in self.scan({"voxel-muzzle-query":[header]})["findings"]}
            self.assertEqual("voxel-muzzle-pixel-borrow-default-signature" not in rules,valid)

    def test_structure_muzzle_requires_canonical_point_contract(self):
        header="src/render/geometry/RuntimeStructureMuzzleQuery.h"
        self.put(header,"bool resolveRuntimeStructureWeaponMuzzlePoint(const WorldObjectInstance& o,const POINT& p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,int f,int l,int h,int facing,bool secondary,POINT& r);")
        self.assertIn("infantry-muzzle-pixel-borrow-signature",{r["rule"] for r in self.scan({"structure-muzzle-query":[header]})["findings"]})

    def test_structure_muzzle_keeps_center_const_and_result_mutable_borrows(self):
        header="src/render/geometry/RuntimeStructureMuzzleQuery.h"
        for center,result,valid in (("const RuntimeInputPixelPoint&","RuntimeInputPixelPoint&",True),("RuntimeInputPixelPoint","RuntimeInputPixelPoint&",False),("RuntimeInputPixelPoint&","RuntimeInputPixelPoint&",False),("const RuntimeInputPixelPoint&","const RuntimeInputPixelPoint&",False)):
            self.put(header,"bool resolveRuntimeStructureWeaponMuzzlePoint(const WorldObjectInstance& o,"+center+" p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,int f,int l,int h,int facing,bool secondary,"+result+" r);")
            rules={r["rule"] for r in self.scan({"structure-muzzle-query":[header]})["findings"]}
            self.assertEqual("infantry-muzzle-pixel-borrow-signature" not in rules,valid)

    def test_surface_pick_logical_cell_requires_signed32_zero_defaults(self):
        header="src/render/geometry/RuntimeMapSurfacePick.h"
        for member in ("LONG", "int", "unsigned"):
            self.put(header,"struct RuntimeMapSurfaceCell{"+member+" x=0;"+member+" y=0;};")
            self.assertIn("surface-pick-logical-cell-value",{r["rule"] for r in self.scan({"map-surface-pick":[header]})["findings"]})

    def test_surface_pick_keeps_pixel_value_and_logical_output_units(self):
        header="src/render/geometry/RuntimeMapSurfacePick.h"
        for output in ("RuntimeInputPixelPoint", "RuntimeMapSurfaceCell"):
            self.put(header,"bool pickRuntimeMapSurfaceCell(const GameWorld& w,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,const RuntimeInputPixelRect& v,RuntimeInputPixelPoint p,RuntimeMapRenderCache& k,"+output+"& r);")
            rules={r["rule"] for r in self.scan({"map-surface-pick":[header]})["findings"]}
            self.assertEqual("surface-pick-unit-borrow-signature" in rules,output!="RuntimeMapSurfaceCell")

    def test_turret_offset_requires_canonical_pixel_return(self):
        header="src/render/geometry/RuntimeStructureTurretScreenOffset.h"
        for result in ("POINT","int","void"):
            self.put(header,result+" runtimeStructureTurretAnimScreenOffsetForRender();")
            self.assertIn("turret-offset-return-value",{r["rule"] for r in self.scan({"turret-offset-return":[header]})["findings"]})

    def test_turret_offset_keeps_original_source_camera_borrow(self):
        header="src/render/geometry/RuntimeStructureTurretScreenOffset.h"
        for camera in ("RuntimeMapCamera", "RuntimeMapCamera&", "const RuntimeMapCamera&"):
            self.put(header,"RuntimeInputPixelPoint runtimeStructureTurretAnimScreenOffsetForRender(const BuildingType& b,const std::string& s,int w,int h,"+camera+" c);")
            rules={r["rule"] for r in self.scan({"turret-offset-return":[header]})["findings"]}
            self.assertEqual("turret-offset-source-signature" in rules,camera!="const RuntimeMapCamera&")

    def test_structure_query_requires_canonical_borrowed_center(self):
        header="src/render/geometry/RuntimeStructureSpriteQuery.h"
        for center in ("const POINT&","RuntimeInputPixelPoint","const RuntimeInputPixelRect&"):
            self.put(header,"bool resolveRuntimeStructureSpriteGeometry(const WorldObjectInstance& w,"+center+" c);")
            self.assertIn("structure-query-center-value",{r["rule"] for r in self.scan({"structure-query-point":[header]})["findings"]})

    def test_structure_query_keeps_original_mutable_geometry_reference(self):
        header="src/render/geometry/RuntimeStructureSpriteQuery.h"
        for ref in ("", "&"):
            self.put(header,"bool resolveRuntimeStructureSpriteGeometry(const WorldObjectInstance& w,const RuntimeInputPixelPoint& c,RuntimeStructureSpriteGeometry"+ref+" out);")
            rules={r["rule"] for r in self.scan({"structure-query-point":[header]})["findings"]}
            self.assertEqual("structure-query-output-reference" in rules,ref=="")

    def test_visible_spans_requires_canonical_borrowed_viewport(self):
        header="src/render/geometry/RuntimeBuildingVisibleHitSpans.h"
        for viewport in ("const RECT&","RuntimeInputPixelRect","const RuntimeInputPixelPoint&"):
            self.put(header,"bool resolveRuntimeBuildingVisibleHitSpans(const GameWorld& w,"+viewport+" v);")
            self.assertIn("visible-spans-viewport-value",{r["rule"] for r in self.scan({"building-visible-viewport":[header]})["findings"]})

    def test_visible_spans_keeps_original_mutable_map_reference(self):
        header="src/render/geometry/RuntimeBuildingVisibleHitSpans.h"
        for ref in ("", "&"):
            self.put(header,"bool resolveRuntimeBuildingVisibleHitSpans(const GameWorld& w,const RuntimeInputPixelRect& v,std::unordered_map<int,std::vector<RuntimeSpriteHitSpan>>"+ref+" out);")
            rules={r["rule"] for r in self.scan({"building-visible-viewport":[header]})["findings"]}
            self.assertEqual("visible-spans-output-reference" in rules,ref=="")

    def test_projectile_screen_requires_canonical_return_and_viewport(self):
        header="src/render/geometry/RuntimeProjectileScreenPoint.h"
        for result,viewport in (("POINT","const RuntimeInputPixelRect&"),("RuntimeInputPixelPoint","const RECT&"),("RuntimeInputPixelPoint","RuntimeInputPixelRect")):
            self.put(header,result+" runtimeProjectileActorScreenPoint(const RuntimeProjectileRenderActor& o,"+viewport+" v);")
            self.assertIn("projectile-screen-pixel-values",{r["rule"] for r in self.scan({"projectile-screen-query":[header]})["findings"]})

    def test_projectile_screen_standard_header_transitive_boundary(self):
        header="src/render/geometry/RuntimeProjectileScreenPoint.h"
        decl="RuntimeInputPixelPoint runtimeProjectileActorScreenPoint(const RuntimeProjectileRenderActor& o,const RuntimeInputPixelRect& v);"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n'+decl);self.put("src/ui/input/RuntimeInputPixelValues.h",'#include <cstdint>\n')
        self.assertFalse(self.scan({"projectile-screen-query":[header]})["findings"])
        self.put("src/ui/input/RuntimeInputPixelValues.h",'#include <windows.h>\n')
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"projectile-screen-query":[header]})["findings"]})

    def test_body_anchor_requires_canonical_return_and_viewport(self):
        header="src/render/geometry/RuntimeObjectBodyAnchor.h"
        for result,viewport in (("POINT","const RuntimeInputPixelRect&"),("RuntimeInputPixelPoint","const RECT&"),("RuntimeInputPixelPoint","RuntimeInputPixelRect")):
            self.put(header,result+" runtimeObjectBodyAnchorForRender(const WorldObjectInstance& o,"+viewport+" v);")
            self.assertIn("body-anchor-pixel-values",{r["rule"] for r in self.scan({"body-anchor-query":[header]})["findings"]})

    def test_body_anchor_standard_header_transitive_boundary(self):
        header="src/render/geometry/RuntimeObjectBodyAnchor.h"
        decl="RuntimeInputPixelPoint runtimeObjectBodyAnchorForRender(const WorldObjectInstance& o,const RuntimeInputPixelRect& v);"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n'+decl);self.put("src/ui/input/RuntimeInputPixelValues.h",'#include <cstdint>\n')
        self.assertFalse(self.scan({"body-anchor-query":[header]})["findings"])
        self.put("src/ui/input/RuntimeInputPixelValues.h",'#include <windows.h>\n')
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"body-anchor-query":[header]})["findings"]})

    def test_world_placement_rejects_native_or_nonborrowed_viewport(self):
        header="src/render/geometry/RuntimeObjectWorldRenderPlacement.h"
        for viewport in ("const RECT&", "RuntimeInputPixelRect", "const RuntimeInputPixelPoint&"):
            self.put(header,"RuntimeObjectWorldRenderPlacement runtimeObjectWorldRenderPlacement(const WorldObjectInstance& o,const GameWorld& w,"+viewport+" v);")
            self.assertIn("world-placement-viewport-value",{r["rule"] for r in self.scan({"render-placement-values":[header]})["findings"]})

    def test_world_placement_accepts_canonical_borrowed_viewport(self):
        header="src/render/geometry/RuntimeObjectWorldRenderPlacement.h"
        self.put(header,"RuntimeObjectWorldRenderPlacement runtimeObjectWorldRenderPlacement(const WorldObjectInstance& o,const GameWorld& w,const RuntimeInputPixelRect& v);")
        self.assertNotIn("world-placement-viewport-value",{r["rule"] for r in self.scan({"render-placement-values":[header]})["findings"]})

    def test_render_placement_requires_standard_fields(self):
        self.put("src/render/geometry/RuntimeObjectWorldRenderPlacement.h", "struct Placement{POINT center{};RuntimeInputPixelPoint sortPoint{};RECT bodyViewport{};};")
        rules={r["rule"] for r in self.scan({"render-placement-values":["src/render/geometry/RuntimeObjectWorldRenderPlacement.h"]})["findings"]}
        self.assertIn("render-placement-pixel-values",rules)

    def test_render_placement_reuses_only_canonical_pixel_header(self):
        header="src/render/geometry/RuntimeObjectWorldRenderPlacement.h"
        fields="struct Placement{RuntimeInputPixelPoint center{};RuntimeInputPixelPoint sortPoint{};RuntimeInputPixelRect bodyViewport{};};"
        self.put(header, '#include "ui/input/RuntimeInputPixelValues.h"\n'+fields)
        self.put("src/ui/input/RuntimeInputPixelValues.h", '#include <cstdint>\n')
        self.assertFalse(any(r["rule"].startswith("protected-") for r in self.scan({"render-placement-values":[header]})["findings"]))
        self.put("src/ui/input/RuntimeInputPixelValues.h", '#include <windows.h>\n')
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"render-placement-values":[header]})["findings"]})
        self.put(header, '#include "ui/input/Other.h"\n'+fields)
        self.put("src/ui/input/Other.h", 'struct Other{};')
        self.assertIn("protected-dependency",{r["rule"] for r in self.scan({"render-placement-values":[header]})["findings"]})

    def test_radar_cell_colours_require_original_full32_storage_and_borrows(self):
        header="src/ui/radar/RuntimeRadarCellColors.h"
        canonical="class C{RuntimeMapColorValue colors[side*side];void setColor(int k,RuntimeMapColorValue c);bool findColor(int k,RuntimeMapColorValue& c)const;std::unordered_map<int,RuntimeMapColorValue> fallbackColors_;};"
        for wrong in ("COLORREF", "std::uint64_t", "RuntimeRgbColorValue"):
            self.put(header,canonical.replace("RuntimeMapColorValue",wrong))
            self.assertIn("radar-cell-colour-full32-signature",{r["rule"] for r in self.scan({"radar-cell-colour-values":[header]})["findings"]})
        self.put(header,canonical.replace("RuntimeMapColorValue& c","const RuntimeMapColorValue& c"))
        self.assertIn("radar-cell-colour-full32-signature",{r["rule"] for r in self.scan({"radar-cell-colour-values":[header]})["findings"]})

    def test_radar_cell_colours_allow_raw_values_reject_native_or_ui_dependencies(self):
        header="src/ui/radar/RuntimeRadarCellColors.h"
        canonical="class C{RuntimeMapColorValue colors[side*side];void setColor(int k,RuntimeMapColorValue c);bool findColor(int k,RuntimeMapColorValue& c)const;std::unordered_map<int,RuntimeMapColorValue> fallbackColors_;};"
        raw="src/render/core/RuntimeMapColorValues.h"
        self.put(header,'#include "render/core/RuntimeMapColorValues.h"\n// COLORREF and POINT are only a comment\n'+canonical)
        self.put(raw,'#include <cstdint>\nusing RuntimeMapColorValue=std::uint32_t;')
        self.assertFalse(any(r["rule"].startswith(("protected-","radar-cell-colour-")) for r in self.scan({"radar-cell-colour-values":[header]})["findings"]))
        self.put(raw,'#include <windows.h>\nusing RuntimeMapColorValue=std::uint32_t;')
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"radar-cell-colour-values":[header]})["findings"]})
        self.put(header,'#include "ui/OtherUi.h"\n'+canonical)
        self.put("src/ui/OtherUi.h","struct OtherUi{};")
        self.assertIn("radar-cell-colour-dependency",{r["rule"] for r in self.scan({"radar-cell-colour-values":[header]})["findings"]})

    def test_radar_cell_colours_only_original_observer_contexts_are_allowed(self):
        header="src/ui/radar/RuntimeRadarCellColors.h"
        canonical="class C{RuntimeMapColorValue colors[side*side];void setColor(int k,RuntimeMapColorValue c);bool findColor(int k,RuntimeMapColorValue& c)const;std::unordered_map<int,RuntimeMapColorValue> fallbackColors_;};"
        contexts="void* context;void* previousContext_;void* runtimeRadarCellColorsObserverContext;"
        self.put(header,canonical+contexts)
        findings=self.scan({"radar-cell-colour-values":[header]})["findings"]
        self.assertEqual(3,sum(r["rule"]=="opaque-native" for r in findings))
        self.assertNotIn("protected-platform",{r["rule"] for r in findings})
        self.put(header,canonical+contexts+"void* otherContext;")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"radar-cell-colour-values":[header]})["findings"]})
        self.put(header,canonical+contexts+"HDC dc;")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"radar-cell-colour-values":[header]})["findings"]})

    def test_menu_bmp_reader_accepts_only_existing_owned_image_values(self):
        header="src/assets/menu/MenuBmpReader.h"
        self.put(header,'#include "assets/menu/MenuImage.h"\n')
        self.put("src/assets/menu/MenuImage.h",'#include "assets/codecs/RuntimeTmpTypes.h"\nstruct Image{};')
        self.put("src/assets/codecs/RuntimeTmpTypes.h","struct PaletteColor{};")
        self.assertFalse(any(f["rule"].startswith(("protected-","menu-bmp-reader-")) for f in self.scan({"menu-bmp-reader":[header]})["findings"]))

    def test_menu_bmp_reader_rejects_transitive_native_and_opaque_resources(self):
        header="src/assets/menu/MenuBmpReader.h"
        self.put(header,'#include "assets/menu/MenuImage.h"\n')
        for source in ('#include <windows.h>\n',"struct Image { HBITMAP bitmap; };","struct Image { void* native; };"):
            self.put("src/assets/menu/MenuImage.h",source)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"menu-bmp-reader":[header]})["findings"]})

    def test_menu_bmp_reader_rejects_native_provider_resource_owner_or_live_state(self):
        header="src/assets/menu/MenuBmpReader.h"
        for source in ("struct MenuState;","struct GameWorld;","struct RuntimeMapRenderCache;","class MenuImageResources;","class Win32MenuBmpReader;"):
            self.put(header,source)
            self.assertIn("menu-bmp-reader-owner-or-live-state",{f["rule"] for f in self.scan({"menu-bmp-reader":[header]})["findings"]})
        self.put(header,'#include "assets/menu/win32/MenuBmpReaderWin32.h"\n')
        self.put("src/assets/menu/win32/MenuBmpReaderWin32.h","struct PrivateProvider{};")
        self.assertIn("menu-bmp-reader-dependency",{f["rule"] for f in self.scan({"menu-bmp-reader":[header]})["findings"]})

    def test_skirmish_system_font_roles_and_canvas_capability_are_standard_only(self):
        header="src/ui/text/RuntimeSkirmishSystemFont.h"
        self.put(header,"#include <cstdint>\nenum class Role:std::uint8_t{Title,Caption,Normal,Small};")
        self.assertFalse(any(f["rule"].startswith(("protected-","skirmish-system-font-")) for f in self.scan({"skirmish-system-font-values":[header]})["findings"]))

    def test_skirmish_system_font_values_reject_transitive_native_or_opaque(self):
        header="src/ui/text/RuntimeSkirmishSystemFont.h"
        self.put(header,'#include "core/HiddenFontValues.h"\n')
        for source in ('#include <windows.h>\n',"struct Hidden { HFONT font; };","struct Hidden { void* native; };"):
            self.put("src/core/HiddenFontValues.h",source)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"skirmish-system-font-values":[header]})["findings"]})

    def test_skirmish_system_font_values_reject_private_owner_and_live_scene(self):
        header="src/ui/text/RuntimeSkirmishSystemFont.h"
        for source in ("struct MenuState;","class SkirmishSystemFontsWin32;","class GameWorld;","struct RuntimeMapRenderCache;"):
            self.put(header,source)
            self.assertIn("skirmish-system-font-owner-or-live-state",{f["rule"] for f in self.scan({"skirmish-system-font-values":[header]})["findings"]})
        self.put(header,'#include "ui/text/win32/RuntimeSkirmishSystemFontWin32.h"\n')
        self.put("src/ui/text/win32/RuntimeSkirmishSystemFontWin32.h","struct PrivateFontOwner{};")
        self.assertIn("skirmish-system-font-dependency",{f["rule"] for f in self.scan({"skirmish-system-font-values":[header]})["findings"]})

    def test_main_stock_footer_accepts_only_typed_nul_text_and_existing_rect(self):
        header="src/ui/text/RuntimeMainSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n'+'template<class Character> struct Footer { const Character* nulTerminatedText; RuntimeInputPixelRect rect; };')
        self.put("src/ui/input/RuntimeInputPixelValues.h","struct RuntimeInputPixelRect { int left,top,right,bottom; };")
        self.assertFalse(any(f["rule"].startswith(("protected-","main-system-font-")) for f in self.scan({"main-system-font":[header]})["findings"]))

    def test_main_stock_footer_rejects_opaque_text_or_native_font_handle(self):
        header="src/ui/text/RuntimeMainSystemFont.h"
        for text in ("struct Footer { void* nulTerminatedText; };","struct Footer { HFONT font; };",'#include <windows.h>\n'):
            self.put(header,text)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"main-system-font":[header]})["findings"]})

    def test_main_stock_footer_rejects_private_stock_provider_in_standard_header(self):
        header="src/ui/text/RuntimeMainSystemFont.h"
        for text in ("class MainVideoFooterFontWin32;","void drawMainStockVideoFooter();","void mainStockVideoFooterCapability();"):
            self.put(header,text)
            self.assertIn("main-system-font-owner-or-live-state",{f["rule"] for f in self.scan({"main-system-font":[header]})["findings"]})
        self.put(header,'#include "ui/text/win32/RuntimeMainVideoFooterFontWin32.h"\n')
        self.put("src/ui/text/win32/RuntimeMainVideoFooterFontWin32.h","struct PrivateFooter{};")
        self.assertIn("main-system-font-dependency",{f["rule"] for f in self.scan({"main-system-font":[header]})["findings"]})

    def test_object_preview_private_boundary_accepts_original_diagnostic_owner(self):
        header="src/render/core/RuntimeMapRenderer.h"
        self.put(header,"class RuntimeMapDrawTarget;")
        self.put("src/render/core/win32/RuntimeMapDrawingWin32.h","bool drawRuntimeObjectPreviewSprite();")
        self.put("src/render/geometry/RuntimeAllWeaponMuzzleRender.inl","void contract(){ drawRuntimeObjectPreviewSprite(); }")
        findings=self.scan({"map-drawing-interface":[header]})["findings"]
        self.assertNotIn("object-preview-native-consumer",{f["rule"] for f in findings})

    def test_object_preview_private_boundary_rejects_new_production_and_public_refs(self):
        header="src/render/core/RuntimeMapRenderer.h"
        self.put(header,"class RuntimeMapDrawTarget;")
        for path in ("src/render/new/Production.cpp","src/runtime/host/RuntimeShell.cpp","src/render/new/Public.h"):
            self.put(path,"auto preview = &drawRuntimeObjectPreviewSprite;")
            findings=self.scan({"map-drawing-interface":[header]})["findings"]
            self.assertTrue(any(f["rule"]=="object-preview-native-consumer" and f["path"]==path for f in findings))

    def test_object_preview_private_boundary_ignores_text_but_detects_actual_ref(self):
        header="src/render/core/RuntimeMapRenderer.h"
        self.put(header,"class RuntimeMapDrawTarget;")
        path="src/render/new/Production.cpp"
        self.put(path,'// drawRuntimeObjectPreviewSprite\nconst char* label="drawRuntimeObjectPreviewSprite";')
        self.assertNotIn("object-preview-native-consumer",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})
        self.put(path,'// original private owner name in a comment is not authorization\nvoid paint(){drawRuntimeObjectPreviewSprite();}')
        self.assertIn("object-preview-native-consumer",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})

    def test_auxiliary_shp_standard_target_point_rect_closure(self):
        header="src/render/core/RuntimeMapRenderer.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\nclass RuntimeAuxiliaryShpDrawTarget; void draw(RuntimeAuxiliaryShpDrawTarget&,const RuntimeInputPixelRect&,const RuntimeInputPixelPoint&);')
        self.put("src/ui/input/RuntimeInputPixelValues.h","struct RuntimeInputPixelRect {}; struct RuntimeInputPixelPoint {};")
        facts=self.scan({"map-drawing-interface":[header]})["findings"]
        self.assertFalse(any(f["rule"].startswith(("protected-","map-drawing-","auxiliary-shp-")) for f in facts))

    def test_auxiliary_shp_public_closure_rejects_private_owner_sdk_and_opaque(self):
        header="src/render/core/RuntimeMapRenderer.h"
        for text in ("class RuntimeAuxiliaryShpDrawTargetWin32;","void drawRuntimeAuxiliaryShpFrameWin32();"):
            self.put(header,text)
            self.assertIn("map-drawing-native-owner-or-host-state",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})
        for text in ('#include <windows.h>\n',"struct Target { void* nativeDc; };"):
            self.put(header,text)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})

    def test_auxiliary_shp_actual_host_requires_standard_port_ignores_text(self):
        header="src/render/core/RuntimeMapRenderer.h"
        self.put(header,"class RuntimeAuxiliaryShpDrawTarget;")
        host="src/runtime/host/RuntimeShell.cpp"
        self.put(host,'// drawRuntimeAuxiliaryShpFrame()\nconst char* text="drawRuntimeAuxiliaryShpFrame()"; void paint(){drawRuntimeAuxiliaryShpFrameWin32();}')
        self.assertNotIn("auxiliary-shp-native-production-bypass",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})
        self.put(host,"void paint(){drawRuntimeAuxiliaryShpFrame();}")
        self.assertIn("auxiliary-shp-native-production-bypass",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})

    def test_screen_shake_standard_target_rect_context_closure(self):
        header="src/render/core/RuntimeMapRenderer.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\nclass RuntimeScreenShakeDrawTarget; struct RuntimeMapRenderContext; struct RuntimeMapRenderCache; bool apply(RuntimeScreenShakeDrawTarget&,const RuntimeInputPixelRect&,const RuntimeMapRenderContext&,RuntimeMapRenderCache&);')
        self.put("src/ui/input/RuntimeInputPixelValues.h","struct RuntimeInputPixelRect { int left,top,right,bottom; };")
        self.assertFalse(any(f["rule"].startswith(("protected-","map-drawing-","screen-shake-")) for f in self.scan({"map-drawing-interface":[header]})["findings"]))

    def test_screen_shake_public_closure_rejects_private_owner_native_and_opaque(self):
        header="src/render/core/RuntimeMapRenderer.h"
        for text in ("class RuntimeScreenShakeDrawTargetWin32;","void applyRuntimeScreenShakeWin32();"):
            self.put(header,text)
            self.assertIn("map-drawing-native-owner-or-host-state",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})
        for text in ('#include <windows.h>\n',"struct Pass { HDC dc; };","struct Pass { void* nativeDc; };"):
            self.put(header,text)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})

    def test_screen_shake_primary_host_rejects_raw_native_bypass_ignores_text(self):
        header="src/render/core/RuntimeMapRenderer.h"
        self.put(header,"class RuntimeScreenShakeDrawTarget;")
        path="src/runtime/host/RuntimeShell.cpp"
        self.put(path,'// applyRuntimeScreenShake()\nconst char* tag="applyRuntimeScreenShake()";void paint(){applyRuntimeScreenShakeWin32();}')
        self.assertNotIn("screen-shake-native-production-bypass",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})
        self.put(path,"void paint(){applyRuntimeScreenShake();}")
        self.assertIn("screen-shake-native-production-bypass",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})

    def test_map_drawing_interface_accepts_standard_target_and_pixel_rect(self):
        header="src/render/core/RuntimeMapRenderer.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\nclass RuntimeMapDrawTarget; struct GameWorld; void draw(RuntimeMapDrawTarget&, const RuntimeInputPixelRect&, const GameWorld&);')
        self.put("src/ui/input/RuntimeInputPixelValues.h","struct RuntimeInputPixelRect { int left,top,right,bottom; };")
        self.assertFalse(any(f["rule"].startswith(("protected-","map-drawing-")) for f in self.scan({"map-drawing-interface":[header]})["findings"]))

    def test_map_drawing_interface_rejects_native_owner_handle_and_dependency(self):
        header="src/render/core/RuntimeMapRenderer.h"
        for text in ("struct Target { HDC dc; };","struct Target { void* nativeDc; };",'#include <windows.h>\n'):
            self.put(header,text)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})
        for text in ("class RuntimeMapDrawTargetWin32;","class RuntimeWindowState;"):
            self.put(header,text)
            self.assertIn("map-drawing-native-owner-or-host-state",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})
        self.put(header,'#include "render/core/win32/RuntimeMapDrawingWin32.h"\n')
        self.put("src/render/core/win32/RuntimeMapDrawingWin32.h","class PrivateBackend{};")
        self.assertIn("map-drawing-native-dependency",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})

    def test_map_drawing_actual_application_cannot_bypass_standard_port(self):
        header="src/render/core/RuntimeMapRenderer.h"
        self.put(header,"class RuntimeMapDrawTarget;")
        self.put("src/runtime/host/RuntimeShell.cpp",'// drawRuntimeMapWin32()\nvoid paint(){ drawRuntimeMap(dc,viewport,world,context,camera,cache); }')
        self.assertIn("map-drawing-native-production-bypass",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})
        self.put("src/runtime/host/RuntimeShell.cpp",'// drawRuntimeMap(dc)\nvoid paint(){ drawRuntimeMapWin32(dc,viewport,world,context,camera,cache); }')
        self.assertNotIn("map-drawing-native-production-bypass",{f["rule"] for f in self.scan({"map-drawing-interface":[header]})["findings"]})

    def test_radar_pixel_store_accepts_only_standard_pixel_borrow_and_raw_colour(self):
        header="src/ui/radar/RuntimeRadarPixelStore.h"
        self.put(header,'#include "ui/surface/RuntimeMenuPixelSurface.h"\n#include "render/core/RuntimeMapColorValues.h"\nstruct Writer { MenuPixelSurfaceView view; RuntimeMapColorValue colour; };')
        self.put("src/ui/surface/RuntimeMenuPixelSurface.h","struct MenuPixelSurfaceView { unsigned char* bytes; };")
        self.put("src/render/core/RuntimeMapColorValues.h","using RuntimeMapColorValue = unsigned int;")
        self.assertFalse(any(f["rule"].startswith(("protected-","radar-pixel-store-")) for f in self.scan({"radar-pixel-store":[header]})["findings"]))

    def test_radar_pixel_store_rejects_native_surface_or_transitive_sdk(self):
        header="src/ui/radar/RuntimeRadarPixelStore.h"
        for text in ("struct Writer { HDC dc; };","struct Writer { void* native; };",'#include <windows.h>\n'):
            self.put(header,text)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"radar-pixel-store":[header]})["findings"]})
        self.put(header,'#include "ui/radar/win32/RuntimeRadarPixelStoreWin32.h"\n')
        self.put("src/ui/radar/win32/RuntimeRadarPixelStoreWin32.h","class Native{};")
        self.assertIn("radar-pixel-store-dependency",{f["rule"] for f in self.scan({"radar-pixel-store":[header]})["findings"]})

    def test_radar_pixel_store_rejects_native_owner_and_live_render_state(self):
        header="src/ui/radar/RuntimeRadarPixelStore.h"
        for text in ("class RuntimeRadarPixelStore;","class RuntimeRadarPixelStoreGdiApi;","class RuntimeRadarPixelStoreObserver;","class RuntimeMapRenderContext;","class GameWorld;"):
            self.put(header,text)
            self.assertIn("radar-pixel-store-native-owner-or-live-state",{f["rule"] for f in self.scan({"radar-pixel-store":[header]})["findings"]})

    def test_main_system_font_accepts_only_existing_owned_image_values(self):
        header="src/ui/text/RuntimeMainSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n')
        self.put("src/ui/input/RuntimeInputPixelValues.h",'#include "ui/values/RuntimeColorValues.h"\nstruct Image{};')
        self.put("src/ui/values/RuntimeColorValues.h","struct PaletteColor{};")
        self.assertFalse(any(f["rule"].startswith(("protected-","main-system-font-")) for f in self.scan({"main-system-font":[header]})["findings"]))

    def test_main_system_font_rejects_transitive_native_and_opaque_resources(self):
        header="src/ui/text/RuntimeMainSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n')
        for source in ('#include <windows.h>\n',"struct Image { HBITMAP bitmap; };","struct Image { void* native; };"):
            self.put("src/ui/input/RuntimeInputPixelValues.h",source)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"main-system-font":[header]})["findings"]})

    def test_main_system_font_rejects_native_provider_resource_owner_or_live_state(self):
        header="src/ui/text/RuntimeMainSystemFont.h"
        for source in ("struct MenuState;","struct GameWorld;","struct RuntimeMapRenderCache;","class RuntimeMainMenuController;","class MainSystemFontsWin32;"):
            self.put(header,source)
            self.assertIn("main-system-font-owner-or-live-state",{f["rule"] for f in self.scan({"main-system-font":[header]})["findings"]})
        self.put(header,'#include "ui/text/win32/RuntimeMainSystemFontWin32.h"\n')
        self.put("src/ui/text/win32/RuntimeMainSystemFontWin32.h","struct PrivateProvider{};")
        self.assertIn("main-system-font-dependency",{f["rule"] for f in self.scan({"main-system-font":[header]})["findings"]})

    def test_campaign_system_font_accepts_only_existing_owned_image_values(self):
        header="src/ui/text/RuntimeCampaignSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n')
        self.put("src/ui/input/RuntimeInputPixelValues.h",'#include "ui/values/RuntimeColorValues.h"\nstruct Image{};')
        self.put("src/ui/values/RuntimeColorValues.h","struct PaletteColor{};")
        self.assertFalse(any(f["rule"].startswith(("protected-","campaign-system-font-")) for f in self.scan({"campaign-system-font":[header]})["findings"]))

    def test_campaign_system_font_rejects_transitive_native_and_opaque_resources(self):
        header="src/ui/text/RuntimeCampaignSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n')
        for source in ('#include <windows.h>\n',"struct Image { HBITMAP bitmap; };","struct Image { void* native; };"):
            self.put("src/ui/input/RuntimeInputPixelValues.h",source)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"campaign-system-font":[header]})["findings"]})

    def test_campaign_system_font_rejects_native_provider_resource_owner_or_live_state(self):
        header="src/ui/text/RuntimeCampaignSystemFont.h"
        for source in ("struct MenuState;","struct GameWorld;","struct RuntimeMapRenderCache;","class RuntimeCampaignMenuController;","class CampaignSystemFontsWin32;"):
            self.put(header,source)
            self.assertIn("campaign-system-font-owner-or-live-state",{f["rule"] for f in self.scan({"campaign-system-font":[header]})["findings"]})
        self.put(header,'#include "ui/text/win32/RuntimeCampaignSystemFontWin32.h"\n')
        self.put("src/ui/text/win32/RuntimeCampaignSystemFontWin32.h","struct PrivateProvider{};")
        self.assertIn("campaign-system-font-dependency",{f["rule"] for f in self.scan({"campaign-system-font":[header]})["findings"]})

    def test_mci_path_binding_accepts_existing_explicit_converter(self):
        private="audio/backend/diagnostics/RuntimeMciPathProducerSmoke.inl"
        self.put("src/"+private,"struct NativeVerification{};")
        self.put("src/runtime/host/RuntimeShell.cpp",'#include "'+private+'"\n'
            'bool playRuntimeEvaOnDedicatedChannel(int samplePath){auto v=widenUtf8(convert(samplePath));return true;}\n'
            'void startRuntimeMusic(){auto v=widenUtf8(convert(*musicPath));}\n'
            'void playRuntimeResolvedSound(){auto v=widenUtf8(convert(*samplePath));}\n')
        self.assertFalse(any(f["rule"].startswith("runtime-mci-path-") for f in self.scan({"runtime-mci-device-private":[]})["findings"]))

    def test_mci_path_binding_rejects_all_three_raw_native_arguments(self):
        for name,argument in (("playRuntimeEvaOnDedicatedChannel","samplePath"),("startRuntimeMusic","*musicPath"),("playRuntimeResolvedSound","*samplePath")):
            raw="widenUtf8("+argument+");"
            self.put("src/runtime/host/RuntimeShell.cpp","void "+name+"(){// "+raw+'\nconst char* label="'+raw+'";}')
            self.assertNotIn("runtime-mci-path-native-bypass",{f["rule"] for f in self.scan({"runtime-mci-device-private":[]})["findings"]})
            self.put("src/runtime/host/RuntimeShell.cpp","void "+name+"(){if(true){"+raw+"}}")
            self.assertIn("runtime-mci-path-native-bypass",{f["rule"] for f in self.scan({"runtime-mci-device-private":[]})["findings"]})

    def test_mci_path_native_verification_rejects_public_and_core_includes(self):
        private="audio/backend/diagnostics/RuntimeMciPathProducerSmoke.inl"
        self.put("src/"+private,"struct NativeVerification{};")
        for caller in ("src/audio/backend/Public.h","src/core/io/Public.h"):
            self.put(caller,'#include "'+private+'"\n')
            self.assertIn("runtime-mci-path-native-consumer",{f["rule"] for f in self.scan({"runtime-mci-device-private":[]})["findings"]})
            self.put(caller,"struct Value{};")

    def test_world_audio_native_fixture_only_actual_runtime_consumer(self):
        private="audio/world/diagnostics/RuntimeWorldAudioDeviceLifecycleSmoke.inl"
        self.put("src/"+private,"struct NativeFixture{};")
        self.put("src/runtime/host/RuntimeShell.cpp",'#include "'+private+'"\n')
        self.put("src/audio/events/diagnostics/RuntimeSuperWeaponActivationAudioSmoke.inl","loadRuntimeIniSource(config,Rules,ini);loadRuntimeIniSource(config,Art,art);")
        self.assertFalse(any(f["rule"].startswith("world-audio-device-") for f in self.scan({"world-audio-device-private":[]})["findings"]))

    def test_world_audio_native_fixture_rejects_public_or_core_consumer(self):
        private="audio/world/diagnostics/RuntimeWorldAudioDeviceLifecycleSmoke.inl"
        self.put("src/"+private,"struct NativeFixture{};")
        for caller in ("src/audio/world/Public.h","src/world/model/Core.h"):
            self.put(caller,'#include "'+private+'"\n')
            self.assertIn("world-audio-device-native-consumer",{f["rule"] for f in self.scan({"world-audio-device-private":[]})["findings"]})
            self.put(caller,"struct Value{};")

    def test_world_audio_diagnostic_display_label_is_not_native_input(self):
        caller="src/audio/events/diagnostics/RuntimeSuperWeaponActivationAudioSmoke.inl"
        for field in ("rulesPath","artPath"):
            statement="ini.loadFromFile(live.report."+field+");"
            self.put(caller,"// "+statement)
            self.assertNotIn("world-audio-device-display-label-io",{f["rule"] for f in self.scan({"world-audio-device-private":[]})["findings"]})
            self.put(caller,statement)
            self.assertIn("world-audio-device-display-label-io",{f["rule"] for f in self.scan({"world-audio-device-private":[]})["findings"]})

    def test_voice_path_standard_helper_and_converted_consumers(self):
        header="src/audio/backend/RuntimeVoicePath.h"
        self.put(header,'#include <filesystem>\n#include <string>\nstd::string convert(const std::string& path);')
        self.put("src/audio/backend/RuntimeSfxChannelPoolMci.inl","return audio.open(id, convert(path));")
        self.put("src/audio/backend/RuntimeWorldAudioBackend.inl","audio.open(id, convert(*path));audio.queue(id, convert(*path));")
        self.assertFalse(any(f["rule"].startswith(("protected-","runtime-voice-path-")) for f in self.scan({"runtime-voice-path":[header]})["findings"]))

    def test_voice_path_rejects_transitive_device_or_live_owner(self):
        header="src/audio/backend/RuntimeVoicePath.h"
        for value in ('#include <windows.h>\n','struct RuntimeWindowState;','class RuntimeVoiceAudio;','struct Path{void* device;};'):
            self.put(header,value)
            self.assertTrue(any(f["rule"].startswith(("protected-","runtime-voice-path-")) for f in self.scan({"runtime-voice-path":[header]})["findings"]))

    def test_voice_path_bypass_detection_uses_code_and_covers_queue(self):
        header="src/audio/backend/RuntimeVoicePath.h";self.put(header,"struct StandardPath{};")
        for caller,statement in (("src/audio/backend/RuntimeSfxChannelPoolMci.inl","audio.open(id, path);"),("src/audio/backend/RuntimeWorldAudioBackend.inl","audio.open(id, *path);"),("src/audio/backend/RuntimeWorldAudioBackend.inl","audio.queue(id, *path);")):
            self.put(caller,'// '+statement+'\nconst char* label="'+statement+'";')
            self.assertNotIn("runtime-voice-path-native-bypass",{f["rule"] for f in self.scan({"runtime-voice-path":[header]})["findings"]})
            self.put(caller,statement)
            self.assertIn("runtime-voice-path-native-bypass",{f["rule"] for f in self.scan({"runtime-voice-path":[header]})["findings"]})
            self.put(caller,"// cleared fixture input")

    def test_voice_path_diagnostic_uses_native_source_not_display_label(self):
        header="src/audio/backend/RuntimeVoicePath.h";caller="src/audio/events/diagnostics/RuntimeDiskContinuousAudioSmoke.inl"
        self.put(header,"struct StandardPath{};")
        for code in ('// rules.loadFromFile(live.report.rulesPath);','loadRuntimeIniSource(live.config,Rules,rules);'):
            self.put(caller,code)
            self.assertNotIn("runtime-voice-diagnostic-path-label-io",{f["rule"] for f in self.scan({"runtime-voice-path":[header]})["findings"]})
        self.put(caller,'rules.loadFromFile(live.report.rulesPath);')
        self.assertIn("runtime-voice-diagnostic-path-label-io",{f["rule"] for f in self.scan({"runtime-voice-path":[header]})["findings"]})

    def test_campaign_surface_owner_accepts_actual_consumers(self):
        target="ui/campaign/win32/RuntimeCampaignDeviceBitmapWin32.h"
        self.put("src/"+target,"struct Owner{};")
        for caller in ("src/ui/campaign/RuntimeCampaignMenu.cpp","tests/infrastructure/campaign_menu_surface/CampaignDeviceBitmapContract.cpp"):
            self.put(caller,'#include "'+target+'"\n')
        self.assertFalse(any(f["rule"].startswith("campaign-menu-surface-") for f in self.scan({"campaign-menu-surface-owner":[]})["findings"]))

    def test_campaign_surface_owner_rejects_core_and_public_consumer(self):
        target="ui/campaign/win32/RuntimeCampaignDeviceBitmapWin32.h"
        self.put("src/"+target,"struct Owner{};")
        for caller in ("src/world/model/Core.h","src/ui/surface/Public.h"):
            self.put(caller,'#include "'+target+'"\n')
            self.assertIn("campaign-menu-surface-native-consumer",{f["rule"] for f in self.scan({"campaign-menu-surface-owner":[]})["findings"]})
            self.put(caller,"struct Value{};")

    def test_campaign_surface_owner_bypass_uses_code_not_comments(self):
        caller="src/ui/campaign/RuntimeCampaignMenuUi.inl"
        self.put(caller,'// CreateCompatibleBitmap(0);\nconst char* label="CreateCompatibleDC(0)";')
        self.assertNotIn("campaign-menu-surface-owner-bypass",{f["rule"] for f in self.scan({"campaign-menu-surface-owner":[]})["findings"]})
        for call in ("CreateCompatibleBitmap(0);","CreateCompatibleDC(0);"):
            self.put(caller,call)
            self.assertIn("campaign-menu-surface-owner-bypass",{f["rule"] for f in self.scan({"campaign-menu-surface-owner":[]})["findings"]})

    def test_main_surface_owner_accepts_actual_consumers(self):
        target="ui/main_menu/win32/RuntimeMainMenuBackBufferWin32.h"
        self.put("src/"+target,"struct Owner{};")
        for caller in ("src/ui/main_menu/RuntimeMainMenu.cpp","tests/infrastructure/main_menu_surface/MainMenuBackBufferContract.cpp"):
            self.put(caller,'#include "'+target+'"\n')
        self.assertFalse(any(f["rule"].startswith("main-menu-surface-") for f in self.scan({"main-menu-surface-owner":[]})["findings"]))

    def test_main_surface_owner_rejects_core_and_public_consumer(self):
        target="ui/main_menu/win32/RuntimeMainMenuBackBufferWin32.h"
        self.put("src/"+target,"struct Owner{};")
        for caller in ("src/world/model/Core.h","src/ui/surface/Public.h"):
            self.put(caller,'#include "'+target+'"\n')
            self.assertIn("main-menu-surface-native-consumer",{f["rule"] for f in self.scan({"main-menu-surface-owner":[]})["findings"]})
            self.put(caller,"struct Value{};")

    def test_main_surface_owner_bypass_uses_code_not_comments(self):
        caller="src/ui/main_menu/RuntimeMainMenu.cpp"
        self.put(caller,'// CreateDIBSection(0);\nconst char* label="CreateCompatibleDC(0)";')
        self.assertNotIn("main-menu-surface-owner-bypass",{f["rule"] for f in self.scan({"main-menu-surface-owner":[]})["findings"]})
        for call in ("CreateDIBSection(0);","CreateCompatibleDC(0);"):
            self.put(caller,call)
            self.assertIn("main-menu-surface-owner-bypass",{f["rule"] for f in self.scan({"main-menu-surface-owner":[]})["findings"]})

    def test_private_hud_headers_accept_only_actual_native_host_and_internal_edge(self):
        names=("RuntimeSelectedPathBatch.h","RuntimeSelectedPathRaster.h","RuntimeSelectionFrameRaster.h")
        for name in names:self.put("src/ui/selection/"+name,"struct NativeBackend{};")
        self.put("src/runtime/host/RuntimeShell.cpp",''.join('#include "ui/selection/'+name+'"\n' for name in names))
        self.put("src/ui/selection/RuntimeSelectionFrameRaster.h",'#include "ui/selection/RuntimeSelectedPathRaster.h"\n')
        self.assertNotIn("hud-native-header-consumer",{f["rule"] for f in self.scan({"hud-private-drawing":[]})["findings"]})

    def test_private_hud_headers_reject_new_public_or_core_consumers(self):
        for name in ("RuntimeSelectedPathBatch.h","RuntimeSelectedPathRaster.h","RuntimeSelectionFrameRaster.h"):
            self.put("src/ui/selection/"+name,"struct NativeBackend{};")
            for caller in ("src/ui/selection/PublicOverlay.h","src/world/model/Query.h"):
                self.put(caller,'#include "ui/selection/'+name+'"\n')
                self.assertIn("hud-native-header-consumer",{f["rule"] for f in self.scan({"hud-private-drawing":[]})["findings"]})
                self.put(caller,"struct Value{};")

    def test_private_hud_header_edges_ignore_text_and_handle_real_line_splice(self):
        target="ui/selection/RuntimeSelectionFrameRaster.h"
        self.put("src/"+target,"struct NativeBackend{};")
        caller="src/ui/selection/PublicOverlay.h"
        self.put(caller,'// #include "'+target+'"\nconst char* hint="#include \\\"'+target+'\\\"";')
        self.assertNotIn("hud-native-header-consumer",{f["rule"] for f in self.scan({"hud-private-drawing":[]})["findings"]})
        self.put(caller,'#include \\\n"'+target+'"\n')
        self.assertIn("hud-native-header-consumer",{f["rule"] for f in self.scan({"hud-private-drawing":[]})["findings"]})

    def test_internet_skin_standard_header_and_only_actual_native_consumers(self):
        header="src/ui/network/RuntimeInternetMenuSkin.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\nstruct Skin{};')
        self.put("src/ui/input/RuntimeInputPixelValues.h","struct Rect{int left,top,right,bottom;};")
        private="ui/network/win32/RuntimeInternetMenuSkinDrawingWin32.h"
        self.put("src/"+private,"struct Native{};")
        for caller in ("src/ui/network/RuntimeInternetMenuSkin.cpp","src/ui/network/RuntimeInternetMultiplayerMenu.cpp"):
            self.put(caller,'#include "'+private+'"\n')
        self.assertFalse(any(f["rule"].startswith(("protected-","internet-skin-")) for f in self.scan({"internet-skin-drawing":[header]})["findings"]))

    def test_internet_skin_rejects_transitive_device_opaque_or_live_state(self):
        header="src/ui/network/RuntimeInternetMenuSkin.h"
        for source in ('#include <windows.h>\n',"struct Skin{ void* device; };","struct RuntimeWindowState;"):
            self.put(header,source)
            self.assertTrue(any(f["rule"].startswith(("protected-","internet-skin-")) for f in self.scan({"internet-skin-drawing":[header]})["findings"]))

    def test_internet_skin_native_adapter_rejects_new_public_core_consumer(self):
        header="src/ui/network/RuntimeInternetMenuSkin.h"
        private="ui/network/win32/RuntimeInternetMenuSkinDrawingWin32.h"
        self.put("src/"+private,"struct Native{};")
        for caller in (header,"src/world/model/CoreQuery.h"):
            self.put(caller,'#include "'+private+'"\n')
            self.assertIn("internet-skin-native-header-consumer",{f["rule"] for f in self.scan({"internet-skin-drawing":[header]})["findings"]})
            self.put(caller,"struct Skin{};")

    def test_hud_system_font_accepts_standard_rect_and_request(self):
        header="src/ui/text/RuntimeHudSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\nstruct HudRequest { const char16_t* text; int count; };')
        self.put("src/ui/input/RuntimeInputPixelValues.h","struct PixelRect { int left,top,right,bottom; };")
        self.assertFalse(any(f["rule"].startswith(("protected-","hud-system-font-")) for f in self.scan({"hud-system-font":[header]})["findings"]))

    def test_hud_system_font_rejects_transitive_device_and_opaque(self):
        header="src/ui/text/RuntimeHudSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n')
        for source in ('#include <windows.h>\n',"struct Request { HFONT font; };","struct Request { void* native; };"):
            self.put("src/ui/input/RuntimeInputPixelValues.h",source)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"hud-system-font":[header]})["findings"]})

    def test_hud_system_font_rejects_live_owner_and_private_dependency(self):
        header="src/ui/text/RuntimeHudSystemFont.h"
        for source in ("struct RuntimeWindowState;","class HudSystemFontWin32;","struct GameWorld;","struct RuntimeMapRenderCache;"):
            self.put(header,source)
            self.assertIn("hud-system-font-owner-or-live-state",{f["rule"] for f in self.scan({"hud-system-font":[header]})["findings"]})
        self.put(header,'#include "ui/text/win32/RuntimeHudSystemFontWin32.h"\n')
        self.put("src/ui/text/win32/RuntimeHudSystemFontWin32.h","struct Native{};")
        self.assertIn("hud-system-font-dependency",{f["rule"] for f in self.scan({"hud-system-font":[header]})["findings"]})

    def test_hud_system_font_original_canvas_bypass_ignores_comments_and_strings(self):
        header="src/ui/text/RuntimeHudSystemFont.h"
        self.put(header,"struct Request {};")
        for path in ("src/ui/input/RuntimePlayerDefeatNoticeUi.inl","src/ui/input/RuntimePlayerWaypointsUi.inl","src/ui/input/RuntimePlayerHotkeysUi.inl","src/ui/diplomacy/RuntimeDiplomacyPage.inl"):
            self.put(path,'// CreateFontW()\nconst char* tag="DrawTextW()";void draw(){font.drawText();}')
            self.assertNotIn("hud-system-font-native-bypass",{f["rule"] for f in self.scan({"hud-system-font":[header]})["findings"]})
            for api in ("CreateFontW","GetTextExtentPoint32W","DrawTextW","TextOutW"):
                self.put(path,"void draw(){"+api+"();}")
                self.assertIn("hud-system-font-native-bypass",{f["rule"] for f in self.scan({"hud-system-font":[header]})["findings"]})
            self.put(path,"void draw(){font.drawText();}")

    def test_match_result_system_font_accepts_only_existing_owned_image_values(self):
        header="src/ui/text/RuntimeMatchResultSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n')
        self.put("src/ui/input/RuntimeInputPixelValues.h",'#include "ui/values/RuntimeColorValues.h"\nstruct Image{};')
        self.put("src/ui/values/RuntimeColorValues.h","struct PaletteColor{};")
        self.assertFalse(any(f["rule"].startswith(("protected-","match-result-system-font-")) for f in self.scan({"match-result-system-font":[header]})["findings"]))

    def test_match_result_system_font_rejects_transitive_native_and_opaque_resources(self):
        header="src/ui/text/RuntimeMatchResultSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n')
        for source in ('#include <windows.h>\n',"struct Image { HBITMAP bitmap; };","struct Image { void* native; };"):
            self.put("src/ui/input/RuntimeInputPixelValues.h",source)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"match-result-system-font":[header]})["findings"]})

    def test_match_result_system_font_rejects_native_provider_resource_owner_or_live_state(self):
        header="src/ui/text/RuntimeMatchResultSystemFont.h"
        for source in ("struct MenuState;","struct GameWorld;","struct RuntimeMapRenderCache;","class RuntimeCampaignMenuController;","class MatchResultSystemFontWin32;"):
            self.put(header,source)
            self.assertIn("match-result-system-font-owner-or-live-state",{f["rule"] for f in self.scan({"match-result-system-font":[header]})["findings"]})
        self.put(header,'#include "ui/text/win32/RuntimeMatchResultSystemFontWin32.h"\n')
        self.put("src/ui/text/win32/RuntimeMatchResultSystemFontWin32.h","struct PrivateProvider{};")
        self.assertIn("match-result-system-font-dependency",{f["rule"] for f in self.scan({"match-result-system-font":[header]})["findings"]})

    def test_campaign_score_system_font_accepts_only_existing_rect_rgb_values(self):
        header="src/ui/text/RuntimeCampaignScoreSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n')
        self.put("src/ui/input/RuntimeInputPixelValues.h",'#include "ui/values/RuntimeColorValues.h"\nstruct Image{};')
        self.put("src/ui/values/RuntimeColorValues.h","struct PaletteColor{};")
        self.assertFalse(any(f["rule"].startswith(("protected-","campaign-score-system-font-")) for f in self.scan({"campaign-score-system-font":[header]})["findings"]))

    def test_campaign_score_system_font_rejects_transitive_native_and_opaque_resources(self):
        header="src/ui/text/RuntimeCampaignScoreSystemFont.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n')
        for source in ('#include <windows.h>\n',"struct Image { HBITMAP bitmap; };","struct Image { void* native; };"):
            self.put("src/ui/input/RuntimeInputPixelValues.h",source)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"campaign-score-system-font":[header]})["findings"]})

    def test_campaign_score_system_font_rejects_native_provider_resource_owner_or_live_state(self):
        header="src/ui/text/RuntimeCampaignScoreSystemFont.h"
        for source in ("struct MenuState;","struct GameWorld;","struct RuntimeMapRenderCache;","class RuntimeCampaignMenuController;","class CampaignScoreSystemFontsWin32;"):
            self.put(header,source)
            self.assertIn("campaign-score-system-font-owner-or-live-state",{f["rule"] for f in self.scan({"campaign-score-system-font":[header]})["findings"]})
        self.put(header,'#include "ui/text/win32/RuntimeCampaignScoreSystemFontsWin32.h"\n')
        self.put("src/ui/text/win32/RuntimeCampaignScoreSystemFontsWin32.h","struct PrivateProvider{};")
        self.assertIn("campaign-score-system-font-dependency",{f["rule"] for f in self.scan({"campaign-score-system-font":[header]})["findings"]})

    def test_result_bitmap_text_accepts_only_unique_codec_and_signed_rect(self):
        header="src/ui/text/RuntimeResultBitmapText.h"
        self.put(header,'#include "assets/text/WestwoodFontCodec.h"\n#include "ui/input/RuntimeInputPixelValues.h"\n')
        self.put("src/assets/text/WestwoodFontCodec.h","struct Metrics {};")
        self.put("src/ui/input/RuntimeInputPixelValues.h","struct Rect { int left, top, right, bottom; };")
        facts=self.scan({"result-bitmap-text":[header]})["findings"]
        self.assertFalse(any(f["rule"].startswith(("protected-","result-bitmap-text-")) for f in facts))

    def test_result_bitmap_text_rejects_transitive_native_or_opaque_device(self):
        header="src/ui/text/RuntimeResultBitmapText.h"
        self.put(header,'#include "ui/input/RuntimeInputPixelValues.h"\n')
        for source in ('#include <windows.h>\n',"struct Rect { HDC dc; };","struct Rect { void* native; };"):
            self.put("src/ui/input/RuntimeInputPixelValues.h",source)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"result-bitmap-text":[header]})["findings"]})

    def test_result_bitmap_text_rejects_score_owner_resource_loader_and_native_scale(self):
        header="src/ui/text/RuntimeResultBitmapText.h"
        for source in ("struct RuntimeWindowState;","struct RuntimeResultBitFont;","struct RuntimeMatchScoreState;","std::ifstream file;","void f(){ runtimeMatchScoreScaledRect(); }"):
            self.put(header,source)
            self.assertIn("result-bitmap-text-owner-or-live-state",{f["rule"] for f in self.scan({"result-bitmap-text":[header]})["findings"]})
        self.put(header,'#include "ui/results/RuntimeMatchScoreAssets.inl"\n')
        self.put("src/ui/results/RuntimeMatchScoreAssets.inl","struct ResourceOwner {};")
        self.assertIn("result-bitmap-text-dependency",{f["rule"] for f in self.scan({"result-bitmap-text":[header]})["findings"]})

    def test_main_bitmap_text_accepts_only_shared_font_and_pixel_values(self):
        header="src/ui/text/RuntimeMainBitmapText.h"
        self.put(header,'#include "assets/text/WestwoodFontCodec.h"\n#include "ui/surface/RuntimeMenuPixelSurface.h"\n')
        self.put("src/assets/text/WestwoodFontCodec.h","struct Metrics {};")
        self.put("src/ui/surface/RuntimeMenuPixelSurface.h",'#include "ui/input/RuntimeInputPixelValues.h"\n#include "ui/values/RuntimeColorValues.h"\n')
        self.put("src/ui/input/RuntimeInputPixelValues.h","struct Rect { int x; };")
        self.put("src/ui/values/RuntimeColorValues.h","struct Color { unsigned value; };")
        facts=self.scan({"main-bitmap-text":[header]})["findings"]
        self.assertFalse(any(f["rule"].startswith(("protected-","main-bitmap-text-")) for f in facts))

    def test_main_bitmap_text_rejects_transitive_native_and_opaque_borrows(self):
        header="src/ui/text/RuntimeMainBitmapText.h"
        self.put(header,'#include "ui/surface/RuntimeMenuPixelSurface.h"\n')
        for source in ('#include <windows.h>\n',"struct Surface { HDC context; };", "struct Surface { void* device; };"):
            self.put("src/ui/surface/RuntimeMenuPixelSurface.h",source)
            self.assertIn("protected-platform",{f["rule"] for f in self.scan({"main-bitmap-text":[header]})["findings"]})

    def test_main_bitmap_text_rejects_menu_owner_and_authoritative_state(self):
        header="src/ui/text/RuntimeMainBitmapText.h"
        for source in ("struct MenuState;", "struct GameWorld;", "struct WestwoodBitFont;", "class RuntimeMainMenuController;"):
            self.put(header,source)
            self.assertIn("main-bitmap-text-owner-or-live-state",{f["rule"] for f in self.scan({"main-bitmap-text":[header]})["findings"]})
        self.put(header,'#include "runtime/menu/ForeignMenu.h"\n')
        self.put("src/runtime/menu/ForeignMenu.h","struct Owner {};")
        self.assertIn("main-bitmap-text-dependency",{f["rule"] for f in self.scan({"main-bitmap-text":[header]})["findings"]})

    def test_persistent_parent_standard_dependency_is_only_window_identity(self):
        header="src/runtime/window/RuntimeMenuParentHost.h"
        self.put("src/runtime/window/RuntimeWindowId.h","struct RuntimeWindowId { unsigned long long value = 0; };")
        self.put(header,'#include "runtime/window/RuntimeWindowId.h"')
        self.assertFalse(any(r["rule"].startswith(("protected-", "persistent-parent-")) for r in self.scan({"persistent-menu-parent":[header]})["findings"]))
        self.put("src/ui/HiddenPersistent.h","struct Foreign {};")
        self.put(header,'#include "ui/HiddenPersistent.h"')
        self.assertIn("persistent-parent-standard-dependency",{r["rule"] for r in self.scan({"persistent-menu-parent":[header]})["findings"]})

    def test_persistent_parent_core_never_owns_native_window_or_lease(self):
        header="src/runtime/window/RuntimeMenuParentHost.h"
        for source in ("struct Owner { RuntimePersistentMenuHostWin32 window; };", "struct Owner { RuntimeWindowParentWin32 lease; };", "void close() { nativeWindow(); }", "struct Owner { int parentBorrow_; };"):
            self.put(header,source)
            self.assertIn("persistent-parent-native-owner",{r["rule"] for r in self.scan({"persistent-menu-parent":[header]})["findings"]})
        self.put(header,"struct Owner { void* parent; };")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"persistent-menu-parent":[header]})["findings"]})

    def test_persistent_parent_same_production_teardown_revoke_precedes_destroy(self):
        source="src/runtime/window/RuntimeMenuParentHost.cpp"
        good="void closeRuntimeMenuParent(Port& port) noexcept { port.revokeParentBorrow(); port.destroyParentWindow(); }"
        self.put(source,good)
        self.assertNotIn("persistent-parent-teardown-order",{r["rule"] for r in self.scan({})["findings"]})
        for bad in ("void closeRuntimeMenuParent(Port& port) noexcept { port.destroyParentWindow(); port.revokeParentBorrow(); }", "void closeRuntimeMenuParent(Port& port) noexcept { port.destroyParentWindow(); }"):
            self.put(source,bad)
            self.assertIn("persistent-parent-teardown-order",{r["rule"] for r in self.scan({})["findings"]})

    def test_internet_menu_controller_precise_standard_dependency_owner(self):
        header="src/runtime/menu/RuntimeInternetMenuController.h"
        for target in ("network/lobby/RuntimeMultiplayerLobbyModel.h", "runtime/config/RuntimeConfig.h"):
            self.put("src/"+target,"struct Value {};")
            self.put(header,'#include "'+target+'"')
            self.assertFalse(any(r["rule"].startswith(("protected-", "internet-menu-controller-")) for r in self.scan({"internet-menu-controller":[header]})["findings"]))
        self.put("src/ui/OtherLanUi.h","struct Foreign {};")
        self.put(header,'#include "ui/OtherLanUi.h"')
        self.assertIn("internet-menu-controller-dependency",{r["rule"] for r in self.scan({"internet-menu-controller":[header]})["findings"]})

    def test_internet_menu_controller_transitive_platform_and_opaque_parent(self):
        header="src/runtime/menu/RuntimeInternetMenuController.h"
        self.put(header,'#include "core/HiddenLan.h"')
        self.put("src/core/HiddenLan.h","#include <windows.h>\nstruct Hidden { HWND parent; };")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"internet-menu-controller":[header]})["findings"]})
        self.put(header,"struct Controller { void* window; };")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"internet-menu-controller":[header]})["findings"]})

    def test_internet_menu_controller_does_not_own_private_native_backend(self):
        header="src/runtime/menu/RuntimeInternetMenuController.h"
        for source in ("struct State { RuntimeInternetMultiplayerServiceConfig credentials; };", "struct State { RuntimeInternetServiceTransport transport; };", "struct State { RuntimeInternetLobbyFlow backend; };", "struct State { RuntimeInternetMultiplayerMenuUiState owner; };", "struct State { RuntimeMultiplayerLobbyFlow backend; };", "void work(){state.flowBackend.poll();}", "struct State { RuntimeInternetMenuWidgetViewWin32 view; };"):
            self.put(header,source)
            self.assertIn("internet-menu-controller-native-owner",{r["rule"] for r in self.scan({"internet-menu-controller":[header]})["findings"]})

    def test_internet_menu_public_parents_use_existing_identity(self):
        header="src/ui/network/RuntimeInternetMultiplayerMenu.h"
        for function,parameter in (("runRuntimeInternetMultiplayerMenu","hostWindow"),("runRuntimeInternetMultiplayerMenuUiSmoke","hostWindow")):
            self.put(header,"Result "+function+"(const Config& config,RuntimeWindowId "+parameter+" = {});")
            self.assertNotIn("internet-menu-parent-identity",{r["rule"] for r in self.scan({})["findings"]})
            self.put(header,"Result "+function+"(const Config& config,void* "+parameter+" = nullptr);")
            self.assertIn("internet-menu-parent-identity",{r["rule"] for r in self.scan({})["findings"]})

    def test_lan_menu_controller_precise_standard_dependency_owner(self):
        header="src/runtime/menu/RuntimeLanMenuController.h"
        for target in ("network/lobby/RuntimeMultiplayerLobbyModel.h", "runtime/config/RuntimeConfig.h"):
            self.put("src/"+target,"struct Value {};")
            self.put(header,'#include "'+target+'"')
            self.assertFalse(any(r["rule"].startswith(("protected-", "lan-menu-controller-")) for r in self.scan({"lan-menu-controller":[header]})["findings"]))
        self.put("src/ui/OtherLanUi.h","struct Foreign {};")
        self.put(header,'#include "ui/OtherLanUi.h"')
        self.assertIn("lan-menu-controller-dependency",{r["rule"] for r in self.scan({"lan-menu-controller":[header]})["findings"]})

    def test_lan_menu_controller_transitive_platform_and_opaque_parent(self):
        header="src/runtime/menu/RuntimeLanMenuController.h"
        self.put(header,'#include "core/HiddenLan.h"')
        self.put("src/core/HiddenLan.h","#include <windows.h>\nstruct Hidden { HWND parent; };")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"lan-menu-controller":[header]})["findings"]})
        self.put(header,"struct Controller { void* window; };")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"lan-menu-controller":[header]})["findings"]})

    def test_lan_menu_controller_does_not_own_private_native_backend(self):
        header="src/runtime/menu/RuntimeLanMenuController.h"
        for source in ("struct State { RuntimeMultiplayerMenuUiState owner; };", "struct State { RuntimeMultiplayerLobbyFlow backend; };", "void work(){state.flowBackend.poll();}", "struct State { RuntimeLanMenuWidgetViewWin32 view; };"):
            self.put(header,source)
            self.assertIn("lan-menu-controller-native-owner",{r["rule"] for r in self.scan({"lan-menu-controller":[header]})["findings"]})

    def test_lan_menu_and_shared_resume_parent_use_existing_identity(self):
        header="src/ui/network/RuntimeMultiplayerMenu.h"
        for function,parameter in (("runRuntimeMultiplayerMenu","hostWindow"),("runRuntimeMultiplayerMenuUiSmoke","hostWindow"),("selectRuntimeMultiplayerResumeSave","owner")):
            self.put(header,"Result "+function+"(const Config& config,RuntimeWindowId "+parameter+" = {});")
            self.assertNotIn("lan-menu-parent-identity",{r["rule"] for r in self.scan({})["findings"]})
            self.put(header,"Result "+function+"(const Config& config,void* "+parameter+" = nullptr);")
            self.assertIn("lan-menu-parent-identity",{r["rule"] for r in self.scan({})["findings"]})

    def test_campaign_menu_controller_has_precise_cpu_data_dependencies(self):
        header="src/runtime/menu/RuntimeCampaignMenuController.h"
        allowed=("src/data/campaign/CampaignDatabase.h","src/ui/time/RuntimeUiClock.h","src/ui/input/RuntimeInputPixelValues.h","src/render/core/RuntimeSpriteFrameImage.h")
        for target in allowed:self.put(target,"struct OwnedStandardValue{};")
        self.put(header,"\n".join('#include "'+p.removeprefix("src/")+'"' for p in allowed))
        self.assertFalse(any(r["rule"].startswith(("protected-","campaign-menu-controller-")) for r in self.scan({"campaign-menu-controller":[header]})["findings"]))
        self.put("src/ui/UnrelatedCampaignView.h","struct ForeignView{};")
        self.put(header,'#include "ui/UnrelatedCampaignView.h"')
        self.assertIn("campaign-menu-controller-dependency",{r["rule"] for r in self.scan({"campaign-menu-controller":[header]})["findings"]})

    def test_campaign_menu_controller_rejects_transitive_native_and_opaque_handle(self):
        header="src/runtime/menu/RuntimeCampaignMenuController.h"
        self.put(header,'#include "core/HiddenCampaign.h"')
        self.put("src/core/HiddenCampaign.h","#include <windows.h>\nstruct Hidden { HWND window; };")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"campaign-menu-controller":[header]})["findings"]})
        self.put(header,"struct Controller { void* window; };")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"campaign-menu-controller":[header]})["findings"]})

    def test_campaign_menu_controller_retains_native_owner_outside_core(self):
        header="src/runtime/menu/RuntimeCampaignMenuController.h"
        for owner in ("CampaignMenuState","CampaignCsfCatalog","CampaignResourceStore","RuntimeMapRenderContext","RuntimeMapRenderCache"):
            self.put(header,"struct Controller {"+owner+" owner;};")
            self.assertIn("campaign-menu-controller-native-owner",{r["rule"] for r in self.scan({"campaign-menu-controller":[header]})["findings"]})

    def test_campaign_menu_public_parent_uses_existing_identity(self):
        header="src/ui/campaign/RuntimeCampaignMenu.h"
        self.put(header,"Result runRuntimeCampaignMenu(const Config& c,RuntimeWindowId hostWindow = {},bool loadGame = false);")
        self.assertNotIn("campaign-menu-parent-identity",{r["rule"] for r in self.scan({})["findings"]})
        self.put(header,"Result runRuntimeCampaignMenu(const Config& c,void* hostWindow = nullptr,bool loadGame = false);")
        self.assertIn("campaign-menu-parent-identity",{r["rule"] for r in self.scan({})["findings"]})

    def test_campaign_clock_guard_follows_moved_intro_policy(self):
        source="src/runtime/menu/RuntimeCampaignMenuController.cpp"
        self.put(source,"void skipCampaignMapIntro(State& state){auto now=GetTickCount64();}")
        self.assertIn("campaign-presentation-clock-bypass",{r["rule"] for r in self.scan({})["findings"]})
        self.put(source,"void skipCampaignMapIntro(State& state){auto now=state.presentationClock->nowMs();}")
        self.assertNotIn("campaign-presentation-clock-bypass",{r["rule"] for r in self.scan({})["findings"]})

    def test_main_menu_controller_dependency_closure_is_precise(self):
        header="src/runtime/menu/RuntimeMainMenuController.h"
        allowed=("src/runtime/config/RuntimeConfig.h","src/ui/time/RuntimeUiClock.h","src/ui/input/RuntimeInputPixelValues.h")
        for target in allowed:self.put(target,"struct OwnedStandardValue{};")
        self.put(header,"\n".join('#include "'+p.removeprefix("src/")+'"' for p in allowed))
        self.assertFalse(any(r["rule"].startswith(("protected-","main-menu-controller-")) for r in self.scan({"main-menu-controller":[header]})["findings"]))
        self.put("src/ui/ForeignMenu.h","struct ForeignMenu{};")
        self.put(header,'#include "ui/ForeignMenu.h"')
        self.assertIn("main-menu-controller-dependency",{r["rule"] for r in self.scan({"main-menu-controller":[header]})["findings"]})

    def test_main_menu_controller_rejects_transitive_native_or_opaque_handle(self):
        header="src/runtime/menu/RuntimeMainMenuController.h"
        self.put(header,'#include "core/HiddenMain.h"')
        self.put("src/core/HiddenMain.h","#include <windows.h>\nstruct Hidden { HWND window; };")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"main-menu-controller":[header]})["findings"]})
        self.put(header,"struct Controller {void* window;};")
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"main-menu-controller":[header]})["findings"]})

    def test_main_menu_public_parent_uses_identity_but_bink_keeps_separate_owner(self):
        header="src/ui/main_menu/RuntimeMainMenu.h"
        canonical="Result runRuntimeMainMenu(Config& c,Page page,RuntimeWindowId hostWindow = {}); void prewarmRuntimeMainMenu(const Config& c,RuntimeWindowId hostWindow = {});bool runRuntimeBinkVideo(const Config& c,void* hostWindow);"
        self.put(header,canonical)
        self.assertNotIn("main-menu-parent-identity",{r["rule"] for r in self.scan({})["findings"]})
        for wrong in (canonical.replace("RuntimeWindowId hostWindow","void* hostWindow",1),canonical.replace("void prewarmRuntimeMainMenu(const Config& c,RuntimeWindowId", "void prewarmRuntimeMainMenu(const Config& c,void*")):
            self.put(header,wrong)
            self.assertIn("main-menu-parent-identity",{r["rule"] for r in self.scan({})["findings"]})

    def test_main_presentation_clock_guard_follows_actual_controller_owner(self):
        source="src/runtime/menu/RuntimeMainMenuController.cpp"
        for method,kind in (("beginAction","void"),("updateDynamicState","bool")):
            self.put(source,kind+" "+method+"(State& state){auto now=GetTickCount64();return false;}")
            self.assertIn("main-presentation-clock-bypass",{r["rule"] for r in self.scan({})["findings"]})
        self.put(source,"bool updateDynamicState(State& state){auto now=state.presentationClock->nowMs();return false;}")
        self.assertNotIn("main-presentation-clock-bypass",{r["rule"] for r in self.scan({})["findings"]})

    def test_startup_recovery_prompt_requires_sync_string_borrow_and_null_callback(self):
        header="src/runtime/window/RuntimeStartupRecoveryPrompt.h"
        canonical="struct Port{using Request=bool(*)(const std::string&);Request requestRetry=nullptr;};"
        for wrong in (canonical.replace("const std::string&","std::string"),canonical.replace("nullptr","someCallback"),canonical.replace("bool(*)","void*(*)")):
            self.put(header,wrong)
            self.assertTrue(any(r["rule"].startswith("startup-recovery-prompt-") for r in self.scan({"startup-recovery-prompt":[header]})["findings"]))

    def test_startup_recovery_prompt_rejects_hidden_native_or_ui_owner(self):
        header="src/runtime/window/RuntimeStartupRecoveryPrompt.h"
        canonical="struct Port{using Request=bool(*)(const std::string&);Request requestRetry=nullptr;};"
        self.put(header,'#include <string>\n'+canonical)
        self.assertFalse(any(r["rule"].startswith(("protected-","startup-recovery-prompt-")) for r in self.scan({"startup-recovery-prompt":[header]})["findings"]))
        self.put(header,'#include "core/HiddenPrompt.h"\n'+canonical)
        self.put("src/core/HiddenPrompt.h",'#include <windows.h>\n')
        self.assertIn("protected-platform",{r["rule"] for r in self.scan({"startup-recovery-prompt":[header]})["findings"]})
        self.put("src/core/HiddenPrompt.h",'struct RuntimeWindowState{};')
        self.assertIn("protected-host-state",{r["rule"] for r in self.scan({"startup-recovery-prompt":[header]})["findings"]})
        self.put(header,'#include "ui/OtherPrompt.h"\n'+canonical)
        self.put("src/ui/OtherPrompt.h","struct OtherPrompt{};")
        self.assertIn("startup-recovery-prompt-dependency",{r["rule"] for r in self.scan({"startup-recovery-prompt":[header]})["findings"]})

    def test_startup_recovery_prompt_main_cannot_bypass_port_with_native_call(self):
        main="src/runtime/host/RuntimeMain.cpp"
        for call in ("MessageBoxA(nullptr,m.c_str(),t,flags)","nested(MessageBoxW(nullptr,m,t,flags))"):
            self.put(main,"int runRuntimeWindowWithMultiplayerFailureRecovery(const Config& c){if(true){"+call+";}return 1;}")
            self.assertIn("startup-recovery-prompt-native-bypass",{r["rule"] for r in self.scan({})["findings"]})
        self.put(main,'int runRuntimeWindowWithMultiplayerFailureRecovery(const Config& c){/*MessageBoxA()*/const char* text="MessageBoxW()";return 1;}')
        self.assertNotIn("startup-recovery-prompt-native-bypass",{r["rule"] for r in self.scan({})["findings"]})

    def test_render_colour_value_requires_full32_raw_type(self):
        for field_type in ("COLORREF", "long", "std::uint64_t", "RuntimeRgbColorValue"):
            self.put("src/render/core/RuntimeMapColorValues.h", "using RuntimeMapColorValue="+field_type+";")
            rules={r["rule"] for r in self.scan({"render-colour-values":["src/render/core/RuntimeMapColorValues.h"]})["findings"]}
            self.assertIn("render-colour-full32-value",rules)

    def test_render_colour_value_rejects_hidden_native_or_host(self):
        self.put("src/render/core/RuntimeMapColorValues.h", '#include "core/HiddenColour.h"\nusing RuntimeMapColorValue=std::uint32_t;')
        self.put("src/core/HiddenColour.h", '#include <windows.h>\n')
        rules={r["rule"] for r in self.scan({"render-colour-values":["src/render/core/RuntimeMapColorValues.h"]})["findings"]}
        self.assertIn("protected-platform",rules)
        self.put("src/core/HiddenColour.h", 'struct RuntimeWindowState{};')
        rules={r["rule"] for r in self.scan({"render-colour-values":["src/render/core/RuntimeMapColorValues.h"]})["findings"]}
        self.assertIn("protected-host-state",rules)

    def test_structure_geometry_value_requires_standard_bounds(self):
        self.put("src/render/geometry/RuntimeStructureSpriteGeometry.h", "struct Geometry{RECT bounds{};};")
        rules={r["rule"] for r in self.scan({"structure-geometry-values":["src/render/geometry/RuntimeStructureSpriteGeometry.h"]})["findings"]}
        self.assertIn("structure-geometry-pixel-bounds",rules)

    def test_structure_geometry_canonical_pixel_value_is_exact(self):
        header="src/render/geometry/RuntimeStructureSpriteGeometry.h"
        self.put(header, '#include "ui/input/RuntimeInputPixelValues.h"\nstruct Geometry{RuntimeInputPixelRect bounds{};};')
        self.put("src/ui/input/RuntimeInputPixelValues.h", '#include <cstdint>\n')
        findings=self.scan({"structure-geometry-values":[header]})["findings"]
        self.assertFalse(any(r["rule"].startswith("protected-") for r in findings))
        self.put("src/ui/input/RuntimeInputPixelValues.h", '#include <windows.h>\n')
        rules={r["rule"] for r in self.scan({"structure-geometry-values":[header]})["findings"]}
        self.assertIn("protected-platform",rules)
        self.put(header, '#include "ui/input/OtherUi.h"\nstruct Geometry{RuntimeInputPixelRect bounds{};};')
        self.put("src/ui/input/OtherUi.h", 'struct Other{};')
        rules={r["rule"] for r in self.scan({"structure-geometry-values":[header]})["findings"]}
        self.assertIn("protected-dependency",rules)

    def test_sprite_frame_reuses_only_canonical_pixel_values(self):
        fields="struct Frame{RuntimeInputPixelRect spriteOpaqueBounds{};RuntimeInputPixelPoint infantryContact{};std::uint32_t packedColorRemap=0x00ffffffU;};"
        self.put("src/render/core/RuntimeSpriteFrameImage.h", '#include "ui/input/RuntimeInputPixelValues.h"\n'+fields)
        self.put("src/ui/input/RuntimeInputPixelValues.h", '#include <cstdint>\n')
        findings=self.scan({"sprite-frame-values":["src/render/core/RuntimeSpriteFrameImage.h"]})["findings"]
        self.assertFalse(any(r["rule"].startswith("protected-") for r in findings))
        self.put("src/render/core/RuntimeSpriteFrameImage.h", '#include "ui/input/OtherUi.h"\n'+fields)
        self.put("src/ui/input/OtherUi.h", 'struct UnrelatedUi{};\n')
        rules={r["rule"] for r in self.scan({"sprite-frame-values":["src/render/core/RuntimeSpriteFrameImage.h"]})["findings"]}
        self.assertIn("protected-dependency",rules)

    def test_sprite_frame_canonical_value_cannot_hide_native_header(self):
        self.put("src/render/core/RuntimeSpriteFrameImage.h", '#include "ui/input/RuntimeInputPixelValues.h"\nstruct Frame{RuntimeInputPixelRect spriteOpaqueBounds{};RuntimeInputPixelPoint infantryContact{};std::uint32_t packedColorRemap=0x00ffffffU;};')
        self.put("src/ui/input/RuntimeInputPixelValues.h", '#include <windows.h>\n')
        rules={r["rule"] for r in self.scan({"sprite-frame-values":["src/render/core/RuntimeSpriteFrameImage.h"]})["findings"]}
        self.assertIn("protected-platform",rules)

    def test_sprite_frame_rejects_native_geometry(self):
        self.put("src/render/core/RuntimeSpriteFrameImage.h", "struct Frame{RECT spriteOpaqueBounds{}; RuntimeInputPixelPoint infantryContact{};std::uint32_t packedColorRemap=0x00ffffffU;};")
        rules={r["rule"] for r in self.scan({"sprite-frame-values":["src/render/core/RuntimeSpriteFrameImage.h"]})["findings"]}
        self.assertIn("sprite-frame-pixel-values",rules)

    def test_sprite_frame_rejects_masking_or_native_colour_key(self):
        for field_type in ("COLORREF", "long", "std::uint64_t", "RuntimeRgbColorValue"):
            self.put("src/render/core/RuntimeSpriteFrameImage.h", "struct Frame{RuntimeInputPixelRect spriteOpaqueBounds{};RuntimeInputPixelPoint infantryContact{};"+field_type+" packedColorRemap=0x00ffffffU;};")
            rules={r["rule"] for r in self.scan({"sprite-frame-values":["src/render/core/RuntimeSpriteFrameImage.h"]})["findings"]}
            self.assertIn("sprite-frame-full32-key",rules)

    def test_sprite_frame_rejects_transitive_native_header(self):
        self.put("src/render/core/RuntimeSpriteFrameImage.h", '#include "core/Hidden.h"\nstruct Frame{RuntimeInputPixelRect spriteOpaqueBounds{};RuntimeInputPixelPoint infantryContact{};std::uint32_t packedColorRemap=0x00ffffffU;};')
        self.put("src/core/Hidden.h", '#include <windows.h>\n')
        rules={r["rule"] for r in self.scan({"sprite-frame-values":["src/render/core/RuntimeSpriteFrameImage.h"]})["findings"]}
        self.assertIn("protected-platform",rules)

    def test_sprite_span_rejects_native_width_or_unsigned(self):
        for field_type in ("long", "LONG", "std::uint32_t"):
            self.put("src/render/geometry/RuntimeSpriteHitSpan.h", "struct Span{"+field_type+" y=0;std::int32_t left=0;std::int32_t right=0;};")
            rules={r["rule"] for r in self.scan({"sprite-hit-span-value":["src/render/geometry/RuntimeSpriteHitSpan.h"]})["findings"]}
            self.assertIn("sprite-hit-span-width",rules)

    def test_sprite_span_rejects_hidden_native_header(self):
        self.put("src/render/geometry/RuntimeSpriteHitSpan.h", '#include "core/Hidden.h"\nstruct Span{std::int32_t y=0;std::int32_t left=0;std::int32_t right=0;};')
        self.put("src/core/Hidden.h", '#include <windows.h>\n')
        rules={r["rule"] for r in self.scan({"sprite-hit-span-value":["src/render/geometry/RuntimeSpriteHitSpan.h"]})["findings"]}
        self.assertIn("protected-platform",rules)

    def test_sprite_span_standard_value_keeps_renderer_native_owner_private(self):
        self.put("src/render/geometry/RuntimeSpriteHitSpan.h", '#include <cstdint>\nstruct Span{std::int32_t y=0;std::int32_t left=0;std::int32_t right=0;};')
        self.put("src/render/core/RuntimeMapRenderer.h", '#include <windows.h>\n')
        rules={r["rule"] for r in self.scan({"sprite-hit-span-value":["src/render/geometry/RuntimeSpriteHitSpan.h"]})["findings"]}
        self.assertFalse(any(x.startswith(("protected-","sprite-hit-span-width")) for x in rules))

    def test_audio_diagnostic_rejects_platform_width_long(self):
        self.put("src/audio/backend/RuntimeVoiceAudio.h", 'class Audio{long lastError() const;};')
        self.assertIn("audio-diagnostic-width", {r["rule"] for r in self.scan({"audio-diagnostic-interface":["src/audio/backend/RuntimeVoiceAudio.h"]})["findings"]})

    def test_audio_diagnostic_rejects_transitive_device_header(self):
        self.put("src/audio/backend/RuntimeVoiceAudio.h", '#include "audio/Hidden.h"\nclass Audio{std::int32_t lastError() const;};')
        self.put("src/audio/Hidden.h", '#include <xaudio2.h>\n')
        self.assertIn("protected-platform", {r["rule"] for r in self.scan({"audio-diagnostic-interface":["src/audio/backend/RuntimeVoiceAudio.h"]})["findings"]})

    def test_audio_diagnostic_standard_header_keeps_native_owner_private(self):
        self.put("src/audio/backend/RuntimeVoiceAudio.h", '#include <cstdint>\nclass Audio{std::int32_t lastError() const;};')
        self.put("src/audio/backend/RuntimeVoiceAudio.cpp", '#include <xaudio2.h>\n')
        self.assertFalse(any(r["rule"].startswith(("protected-","audio-diagnostic-width")) for r in self.scan({"audio-diagnostic-interface":["src/audio/backend/RuntimeVoiceAudio.h"]})["findings"]))

    def test_host_loop_standard_port_rejects_native_transitive_owner(self):
        self.put("src/runtime/timing/RuntimeHostLoopPort.h", '#include "core/Hidden.h"\n')
        self.put("src/core/Hidden.h", '#include <windows.h>\n')
        rules={x["rule"] for x in self.scan({"host-loop-port":["src/runtime/timing/RuntimeHostLoopPort.h"]})["findings"]}
        self.assertIn("protected-platform",rules)
        self.assertIn("host-loop-port-dependency",rules)

    def test_host_loop_native_calls_cannot_bypass_services_in_real_session(self):
        self.put("src/runtime/host/RuntimeShell.cpp", 'int runRuntimeWindowSession(Config c){PeekMessageW(a);std::this_thread::sleep_for(x);windowHost.drainMessages();}')
        self.assertEqual(3,len([x for x in self.scan()["findings"] if x["rule"]=="host-loop-owner-bypass"]))

    def test_host_loop_existing_owner_and_loading_pump_stay_private(self):
        self.put("src/runtime/timing/RuntimeHostLoopPort.h", 'class Port{};')
        self.put("src/runtime/host/RuntimeShell.cpp", 'void pumpRuntimeLoadingWindow(){PeekMessageW(a);} int runRuntimeWindowSession(Config c){hostLoop.drainMessages();hostLoop.sleepForIdleSlice();}')
        r=self.scan({"host-loop-port":["src/runtime/timing/RuntimeHostLoopPort.h"]})
        self.assertFalse(any(x["rule"].startswith(("protected-","host-loop-")) for x in r["findings"]))

    def test_timer_scope_rejects_public_native_dependency(self):
        self.put("src/runtime/timing/RuntimeTimerResolutionScope.h", '#include "core/Hidden.h"\n')
        self.put("src/core/Hidden.h", '#include <mmsystem.h>\n')
        rules={x["rule"] for x in self.scan({"timer-resolution-scope":["src/runtime/timing/RuntimeTimerResolutionScope.h"]})["findings"]}
        self.assertIn("protected-platform",rules)
        self.assertIn("timer-resolution-scope-dependency",rules)

    def test_timer_scope_rejects_direct_native_calls_in_game_owner(self):
        self.put("src/runtime/host/RuntimeShell.cpp", 'void f(){timeBeginPeriod(1);timeEndPeriod(1);}')
        self.assertEqual(2,len([x for x in self.scan()["findings"] if x["rule"]=="timer-resolution-owner-bypass"]))

    def test_timer_scope_standard_port_and_private_native_are_allowed(self):
        self.put("src/runtime/timing/RuntimeTimerResolutionScope.h", '#include <cstdint>\nclass Port{};')
        self.put("src/runtime/timing/win32/RuntimeTimerResolutionScopeWin32.h", '#include <mmsystem.h>\nvoid f(){timeBeginPeriod(1);timeEndPeriod(1);}')
        r=self.scan({"timer-resolution-scope":["src/runtime/timing/RuntimeTimerResolutionScope.h"]})
        self.assertFalse(any(x["rule"].startswith(("protected-","timer-resolution-")) for x in r["findings"]))

    def test_process_policy_standard_header_has_no_platform_dependency(self):
        self.put("src/runtime/performance/RuntimeProcessPerformanceScope.h", "#include <cstdint>\nstruct Policy{std::uint32_t mask;};")
        self.assertFalse(any(x["rule"].startswith(("protected-","process-performance-policy-")) for x in self.scan({"process-performance-policy":["src/runtime/performance/RuntimeProcessPerformanceScope.h"]})["findings"]))

    def test_process_policy_transitive_native_owner_is_rejected(self):
        self.put("src/runtime/performance/RuntimeProcessPerformanceScope.h", '#include "core/Hidden.h"\n')
        self.put("src/core/Hidden.h", "#include <Windows.h>\nHANDLE process;\n")
        rules={x["rule"] for x in self.scan({"process-performance-policy":["src/runtime/performance/RuntimeProcessPerformanceScope.h"]})["findings"]}
        self.assertIn("protected-platform",rules)
        self.assertIn("process-performance-policy-dependency",rules)

    def test_world_audio_clock_callers_reject_raw_and_wrong_ui_sources(self):
        self.put("src/simulation/superweapons/RuntimeSupportPowers.inl", "void f(){GetTickCount64();runtimeUiClockNow(s);}")
        self.put("src/ui/selection/RuntimeChronosphereSelection.inl", "void f(){std::chrono::steady_clock::now();}")
        self.assertEqual(3,len([x for x in self.scan()["findings"] if x["rule"]=="world-audio-clock-bypass"]))

    def test_world_audio_clock_callers_accept_existing_separate_audio_port(self):
        self.put("src/simulation/superweapons/RuntimeSupportPowers.inl", "void f(){advanceRuntimeChronosphereSelectionAudio(s,runtimeWorldAudioClockNow(s));}")
        self.put("src/ui/selection/RuntimeChronosphereSelection.inl", "void f(){startRuntimeChronosphereSelectionAudio(s,m,runtimeWorldAudioClockNow(s));}")
        self.assertFalse(any(x["rule"]=="world-audio-clock-bypass" for x in self.scan()["findings"]))

    def test_skirmish_clock_rejects_direct_native_and_controller_provider_bypass(self):
        self.put("src/ui/skirmish/RuntimeSkirmishMenu.cpp", "std::uint64_t MenuState::nowMs() const {return GetTickCount64();}\nLRESULT CALLBACK shellSkirmishWindowProc(HWND w,UINT m){if(x){std::chrono::steady_clock::now();}}\nRuntimeSkirmishSelection runRuntimeSkirmishMenu(const Config& c){runtimeWin32UiClock();}")
        self.assertEqual(3,len([x for x in self.scan()["findings"] if x["rule"]=="skirmish-presentation-clock-bypass"]))

    def test_skirmish_clock_allows_existing_port_default_and_software_branch(self):
        self.put("src/ui/skirmish/RuntimeSkirmishMenu.cpp", 'std::uint64_t MenuState::nowMs() const {return softwareWindow ? softwareWindow->nowMs() : runtimeWin32UiClock().nowMs();}\nLRESULT CALLBACK shellSkirmishWindowProc(HWND w,UINT m){state->nowMs();}\nRuntimeSkirmishSelection runRuntimeSkirmishMenu(const Config& c){state.nowMs();}')
        self.assertFalse(any(x["rule"]=="skirmish-presentation-clock-bypass" for x in self.scan()["findings"]))

    def test_skirmish_clock_original_timer_is_private(self):
        self.put("src/ui/skirmish/diagnostics/skirmish_clock_original/Timer.original.inl", 'LRESULT CALLBACK shellSkirmishWindowProc(HWND w,UINT m){GetTickCount64();}')
        self.assertFalse(any(x["rule"]=="skirmish-presentation-clock-bypass" for x in self.scan()["findings"]))

    def test_campaign_clock_rejects_native_and_nested_steady_bypass(self):
        self.put("src/ui/campaign/RuntimeCampaignMenu.cpp", "void updateCampaignMapPresentation(State& state){GetTickCount64();}\nstd::vector<unsigned char> composeCampaignSurface(const State& state){if(x){std::chrono::steady_clock::now();}}")
        self.assertEqual(2,len([x for x in self.scan()["findings"] if x["rule"]=="campaign-presentation-clock-bypass"]))

    def test_campaign_clock_allows_borrow_and_diagnostic_temp_names(self):
        self.put("src/ui/campaign/RuntimeCampaignMenu.cpp", 'struct State{auto* presentationClock=&runtimeWin32UiClock();};\nvoid skipCampaignMapIntro(State& state){state.presentationClock->nowMs();}\nvoid smoke(){GetTickCount64();}')
        self.assertFalse(any(x["rule"]=="campaign-presentation-clock-bypass" for x in self.scan()["findings"]))

    def test_campaign_complete_original_methods_are_private(self):
        self.put("src/ui/campaign/diagnostics/campaign_clock_original/Methods.original.inl", 'void updateCampaignMapPresentation(State& state){GetTickCount64();}')
        self.assertFalse(any(x["rule"]=="campaign-presentation-clock-bypass" for x in self.scan()["findings"]))

    def test_main_presentation_rejects_native_steady_and_audio_clock(self):
        self.put("src/ui/main_menu/RuntimeMainMenu.cpp", "bool pollMainMenuVideo(State& state){GetTickCount64();}\nvoid beginAction(State& state){if(ok){std::chrono::steady_clock::now();}}\nbool updateDynamicState(State& state){state.audio.clock->nowMs();}")
        self.assertEqual(3,len([x for x in self.scan()["findings"] if x["rule"]=="main-presentation-clock-bypass"]))

    def test_main_presentation_allows_borrow_and_diagnostic_scope(self):
        self.put("src/ui/main_menu/RuntimeMainMenu.cpp", 'struct State{auto* presentationClock=&runtimeWin32UiClock();};\nbool pollMainMenuVideo(State& state){return state.presentationClock->nowMs();}\nvoid diagnostics(){GetTickCount64();}')
        self.assertFalse(any(x["rule"]=="main-presentation-clock-bypass" for x in self.scan()["findings"]))

    def test_main_presentation_original_methods_are_private(self):
        self.put("src/ui/main_menu/diagnostics/main_presentation_original/Methods.original.inl", 'bool pollMainMenuVideo(State& state){GetTickCount64();}')
        self.assertFalse(any(x["rule"]=="main-presentation-clock-bypass" for x in self.scan()["findings"]))

    def test_internet_clock_rejects_native_helper_and_nested_bypass(self):
        self.put("src/ui/network/RuntimeInternetMultiplayerMenu.cpp", "auto internetMenuNowMilliseconds(const State& s){return GetTickCount64();} void pollInternetMenu(State& s){if(s.flow){std::chrono::steady_clock::now();}}")
        self.assertEqual(2,len([x for x in self.scan()["findings"] if x["rule"]=="internet-menu-clock-bypass"]))

    def test_internet_clock_allows_borrow_and_default_outside_consumer(self):
        self.put("src/ui/network/RuntimeInternetMultiplayerMenu.cpp", 'struct State{auto* clock=&runtimeWin32UiClock();}; auto internetMenuNowMilliseconds(const State& s){return s.clock->nowMs();}void beginRoomRefresh(State& s){f(internetMenuNowMilliseconds(s));}void other(){GetTickCount64();}')
        self.assertFalse(any(x["rule"]=="internet-menu-clock-bypass" for x in self.scan()["findings"]))

    def test_internet_clock_comments_literals_and_private_diagnostic(self):
        self.put("src/ui/network/RuntimeInternetMultiplayerMenu.cpp", 'auto internetMenuNowMilliseconds(const State& s){/*GetTickCount64()*/const char* x="timeGetTime()";return s.clock->nowMs();}')
        self.put("src/ui/network/diagnostics/RuntimeInternetMenuClockSmoke.inl", 'void pollInternetMenu(State& s){GetTickCount64();}')
        self.assertFalse(any(x["rule"]=="internet-menu-clock-bypass" for x in self.scan()["findings"]))

    def test_main_music_clock_rejects_direct_native_and_steady(self):
        self.put("src/ui/main_menu/RuntimeMainMenu.cpp", "struct MenuAudio {void startMusic(const Path& p){if(ok){GetTickCount64();}} void ensureMusicPlaying(){std::chrono::steady_clock::now();}};")
        r=self.scan()
        self.assertEqual(2,len([x for x in r["findings"] if x["rule"]=="main-menu-music-clock-bypass"]))

    def test_main_music_clock_allows_borrow_and_keeps_other_scope(self):
        self.put("src/ui/main_menu/RuntimeMainMenu.cpp", 'struct MenuAudio {auto* clock=&runtimeWin32UiClock();void startMusic(const Path& p){clock->nowMs();}void ensureMusicPlaying(){const char* s="GetTickCount64()";/* timeGetTime() */clock->nowMs();}}; void phase(){GetTickCount64();}')
        r=self.scan()
        self.assertFalse(any(x["rule"]=="main-menu-music-clock-bypass" for x in r["findings"]))

    def test_main_music_clock_frozen_class_is_private(self):
        self.put("src/audio/menu/diagnostics/main_menu_music_original/MenuAudio.original.inl", "struct MenuAudio {void ensureMusicPlaying(){GetTickCount64();}};")
        r=self.scan()
        self.assertFalse(any(x["rule"]=="main-menu-music-clock-bypass" for x in r["findings"]))

    def test_menu_sfx_collect_rejects_nested_native_and_wrong_port(self):
        self.put("src/audio/events/RuntimeMenuSoundMci.inl", "void collect(){if(ready){GetTickCount64();} runtimeUiClockNow(s);}")
        r=self.scan()
        self.assertEqual(2,len([x for x in r["findings"] if x["rule"]=="menu-sfx-clock-bypass"]))

    def test_menu_sfx_default_provider_and_comments_are_allowed(self):
        self.put("src/audio/events/RuntimeMenuSoundMci.inl", 'auto* clock=&runtimeWin32UiClock(); void collect(){ const char* text="GetTickCount64()"; /* timeGetTime() */ clock->nowMs();}')
        r=self.scan()
        self.assertFalse(any(x["rule"]=="menu-sfx-clock-bypass" for x in r["findings"]))

    def test_menu_sfx_frozen_original_is_diagnostic_only(self):
        self.put("tests/audio/menu_sound_clock_original/RuntimeMenuSoundMci.original.inl", "void collect(){GetTickCount64();}")
        r=self.scan()
        self.assertFalse(any(x["rule"]=="menu-sfx-clock-bypass" for x in r["findings"]))

    def test_world_audio_clock_rejects_direct_native_and_wrong_ui_port(self):
        self.put("src/audio/events/RuntimeGattlingAudio.inl", "void f(){ GetTickCount64(); runtimeUiClockNow(s); }\n")
        r=self.scan()
        self.assertEqual(2,len([x for x in r["findings"] if x["rule"]=="world-audio-clock-bypass"]))

    def test_world_audio_clock_comments_and_literals_cannot_invent_bypasses(self):
        self.put("src/audio/events/RuntimeWorldAudioClock.inl", 'const char* s="GetTickCount64()"; // runtimeUiClockNow(s)\nvoid f(){ runtimeWorldAudioClockNow(s); }\n')
        r=self.scan()
        self.assertFalse(any(x["rule"]=="world-audio-clock-bypass" for x in r["findings"]))

    def test_world_audio_clock_rejects_nested_native_argument_in_shell(self):
        self.put("src/runtime/host/RuntimeShell.cpp", "void f(){advanceRuntimeDiskMoveAudio(s, static_cast<unsigned long long>(GetTickCount64()));}\n")
        r=self.scan()
        self.assertIn("world-audio-clock-bypass",{x["rule"] for x in r["findings"]})

    def test_world_audio_clock_provider_and_frozen_oracle_are_private(self):
        self.put("src/runtime/host/RuntimeUiClockWin32.cpp", "auto f(){return GetTickCount64();}\n")
        self.put("src/audio/events/diagnostics/frozen.inl", "auto f(){return GetTickCount64();}\n")
        r=self.scan()
        self.assertFalse(any(x["rule"]=="world-audio-clock-bypass" for x in r["findings"]))

    def test_presentation_clock_standard_interface_is_allowed(self):
        self.put("src/ui/time/RuntimeUiClock.h", "#include <cstdint>\nclass Clock { public: virtual std::uint64_t nowMs() const noexcept=0; };\n")
        r=self.scan({"presentation-clock":["src/ui/time/RuntimeUiClock.h"]})
        self.assertFalse(any(x["rule"].startswith(("protected-","presentation-clock-")) for x in r["findings"]))

    def test_presentation_clock_transitive_native_and_owner_are_rejected(self):
        self.put("src/ui/time/RuntimeUiClock.h", '#include "core/Hidden.h"\n')
        self.put("src/core/Hidden.h", "#include <Windows.h>\nHANDLE native;\n")
        r=self.scan({"presentation-clock":["src/ui/time/RuntimeUiClock.h"]})
        self.assertIn("protected-platform",{x["rule"] for x in r["findings"]})
        self.assertIn("presentation-clock-dependency",{x["rule"] for x in r["findings"]})

    def test_atomic_public_interface_rejects_direct_native_header(self):
        self.put("src/core/io/RuntimeAtomicFile.h", "#include <Windows.h>\nHANDLE handle;\n")
        r = self.scan({"atomic-file-interface": ["src/core/io/RuntimeAtomicFile.h"]})
        self.assertIn("protected-platform", {f["rule"] for f in r["findings"]})

    def test_atomic_public_interface_rejects_transitive_native_and_owner(self):
        self.put("src/core/io/RuntimeAtomicFile.h", '#include "core/Hidden.h"\n')
        self.put("src/core/Hidden.h", "#include <Windows.h>\nHANDLE handle;\n")
        r = self.scan({"atomic-file-interface": ["src/core/io/RuntimeAtomicFile.h"]})
        self.assertIn("atomic-file-interface-dependency", {f["rule"] for f in r["findings"]})
        self.assertTrue(any(f["rule"] == "protected-platform" and len(f["witness"]) == 2 for f in r["findings"]))

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

    def scan(self, modules=None):
        refs = []
        for name, target in self.targets.items():
            self.put("build/reply/" + name + ".json", json.dumps(dict(name=name, **target)))
            refs.append(dict(name=name, jsonFile=name + ".json"))
        self.put("build/reply/model.json", json.dumps(dict(configurations=[
            dict(name="Release", targets=refs)])))
        return guard.scan(self.root, self.root / "build", self.inventory,
                          self.root / "build/trace.jsonl",
                          modules or {"pure": ["src/core/Pure.h", "src/core/Pure.cpp"]})

    def software_frame_fixture(self):
        self.put("src/runtime/presentation/SoftwareFrame.h", '#include "ui/surface/RuntimeMenuPixelSurface.h"\n')
        self.put("src/ui/surface/RuntimeMenuPixelSurface.h", '#include "ui/input/RuntimeInputPixelValues.h"\n#include "ui/values/RuntimeColorValues.h"\n')
        self.put("src/ui/input/RuntimeInputPixelValues.h", "struct Pixel { int x, y; };\n")
        self.put("src/ui/values/RuntimeColorValues.h", "struct Color { unsigned value; };\n")
        return {"software-frame": ["src/runtime/presentation/SoftwareFrame.h"]}

    def test_software_frame_only_reuses_exact_pure_pixel_values(self):
        report = self.scan(self.software_frame_fixture())
        self.assertFalse(any(f["rule"].startswith("protected-") or f["rule"].startswith("software-frame-")
                             for f in report["findings"]))
        self.put("src/ui/NewWidget.h", "struct Widget {};\n")
        self.put("src/runtime/presentation/SoftwareFrame.h", '#include "ui/NewWidget.h"\n')
        report = self.scan({"software-frame": ["src/runtime/presentation/SoftwareFrame.h"]})
        self.assertIn("software-frame-dependency", {f["rule"] for f in report["findings"]})

    def test_software_frame_pixel_value_cannot_hide_native_dependency(self):
        modules = self.software_frame_fixture()
        self.put("src/ui/values/RuntimeColorValues.h", "#include <windows.h>\nstruct Color { HWND window; };\n")
        report = self.scan(modules)
        self.assertTrue(any(f["rule"] == "protected-platform" and len(f["witness"]) == 3
                            for f in report["findings"]))

    def test_software_frame_cannot_borrow_mutable_world(self):
        modules = self.software_frame_fixture()
        self.put("src/runtime/presentation/SoftwareFrame.h", "struct GameWorld; struct Frame { GameWorld* live; };\n")
        report = self.scan(modules)
        self.assertIn("software-frame-live-state", {f["rule"] for f in report["findings"]})

    def test_path_candidate_rejects_queue_dependency(self):
        self.put("src/simulation/navigation/pathfinding/PathRequestQueue.h", "class PathRequestQueue {};\n")
        self.put("src/simulation/navigation/pathfinding/PathGoalCandidateTask.h",
                 '#include "simulation/navigation/pathfinding/PathRequestQueue.h"\n')
        report = self.scan({"path-candidate-task": ["src/simulation/navigation/pathfinding/PathGoalCandidateTask.h"]})
        self.assertIn("path-candidate-dependency", {f["rule"] for f in report["findings"]})

    def test_backend_options_have_no_native_or_live_dependencies(self):
        self.put("src/runtime/presentation/FrameBackendOptions.h", "enum class Backend { Software, D3D11 };\n")
        report = self.scan({"software-frame": ["src/runtime/presentation/FrameBackendOptions.h"]})
        self.assertFalse(any(f["rule"].startswith("protected-") or f["rule"].startswith("software-frame-") for f in report["findings"]))

    def test_backend_options_transitive_native_rejected(self):
        self.put("src/runtime/presentation/FrameBackendOptions.h", '#include "runtime/presentation/private.h"\n')
        self.put("src/runtime/presentation/private.h", "#include <windows.h>\nstruct Settings { HWND owner; };\n")
        report = self.scan({"software-frame": ["src/runtime/presentation/FrameBackendOptions.h"]})
        self.assertTrue({"software-frame-dependency", "protected-platform"}.issubset({f["rule"] for f in report["findings"]}))

    def test_backend_options_transitive_live_world_rejected(self):
        self.put("src/runtime/presentation/FrameBackendOptions.h", '#include "runtime/presentation/private.h"\n')
        self.put("src/runtime/presentation/private.h", "struct GameWorld; struct Settings { GameWorld* world; };\n")
        report = self.scan({"software-frame": ["src/runtime/presentation/FrameBackendOptions.h"]})
        self.assertIn("software-frame-live-state", {f["rule"] for f in report["findings"]})

    def test_path_candidate_rejects_forward_live_state(self):
        self.put("src/simulation/navigation/pathfinding/PathGoalCandidateTask.h",
                 "struct WorldObjectInstance; struct Input { WorldObjectInstance* live; };\n")
        report = self.scan({"path-candidate-task": ["src/simulation/navigation/pathfinding/PathGoalCandidateTask.h"]})
        self.assertIn("path-candidate-live-state", {f["rule"] for f in report["findings"]})

    def test_task_executor_rejects_application_dependency(self):
        self.put("src/world/model/GameWorld.h", "struct World {};\n")
        self.put("src/core/tasks/BoundedTaskExecutor.h", '#include "world/model/GameWorld.h"\n')
        report = self.scan({"bounded-tasks": ["src/core/tasks/BoundedTaskExecutor.h"]})
        self.assertTrue(any(f["rule"] == "task-executor-dependency" and len(f["witness"]) == 2
                            for f in report["findings"]))

    def test_task_executor_rejects_forward_declared_live_state(self):
        self.put("src/core/tasks/BoundedTaskExecutor.h",
                 "struct WorldObjectInstance; struct X { WorldObjectInstance* live; };\n")
        report = self.scan({"bounded-tasks": ["src/core/tasks/BoundedTaskExecutor.h"]})
        self.assertIn("task-executor-live-state", {f["rule"] for f in report["findings"]})

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

    def test_software_window_reuses_only_frame_and_pixel_values(self):
        self.software_frame_fixture()
        self.put("src/runtime/window/RuntimeSoftwareWindow.h", '#include "runtime/presentation/SoftwareFrame.h"\n')
        report = self.scan({"software-window": ["src/runtime/window/RuntimeSoftwareWindow.h"]})
        self.assertFalse(any(f["rule"].startswith("protected-") or f["rule"].startswith("software-window-") for f in report["findings"]))

    def test_software_window_cannot_expose_sdl_device(self):
        self.put("src/runtime/window/RuntimeSoftwareWindow.h", "struct SDL_Window; SDL_Window* device;\n")
        report = self.scan({"software-window": ["src/runtime/window/RuntimeSoftwareWindow.h"]})
        self.assertIn("software-window-device-or-live-state", {f["rule"] for f in report["findings"]})

    def test_software_window_transitive_native_or_host_rejected(self):
        self.put("src/runtime/window/RuntimeSoftwareWindow.h", '#include "runtime/host/Hidden.h"\n')
        self.put("src/runtime/host/Hidden.h", "HDC device; GameWorld* world;\n")
        report = self.scan({"software-window": ["src/runtime/window/RuntimeSoftwareWindow.h"]})
        rules = {f["rule"] for f in report["findings"]}
        self.assertTrue({"software-window-dependency", "software-window-device-or-live-state", "protected-platform", "protected-dependency"}.issubset(rules))

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

    def test_menu_draw_allows_only_exact_pure_values(self):
        self.put("src/ui/drawing/MenuDrawCommands.h", '#include "ui/surface/RuntimeMenuPixelSurface.h"\n')
        self.put("src/ui/surface/RuntimeMenuPixelSurface.h", '#include "ui/input/RuntimeInputPixelValues.h"\n')
        self.put("src/ui/input/RuntimeInputPixelValues.h", "struct RuntimeInputPixelRect {};\n")
        facts=self.scan({"menu-draw":["src/ui/drawing/MenuDrawCommands.h"]})["findings"]
        self.assertFalse(any(f["rule"].startswith("protected-") or f["rule"].startswith("menu-draw-") for f in facts))

    def test_menu_draw_rejects_native_transitively(self):
        self.put("src/ui/drawing/MenuDrawCommands.h", '#include "ui/surface/RuntimeMenuPixelSurface.h"\n')
        self.put("src/ui/surface/RuntimeMenuPixelSurface.h", '#include <d3d11.h>\nID3D11Device* device;\n')
        facts=self.scan({"menu-draw":["src/ui/drawing/MenuDrawCommands.h"]})["findings"]
        self.assertTrue(any(f["rule"]=="protected-platform" for f in facts))

    def test_menu_draw_rejects_live_world_and_other_ui(self):
        self.put("src/ui/drawing/MenuDrawCommands.h", '#include "ui/Other.h"\n')
        self.put("src/ui/Other.h", "struct GameWorld {};\n")
        facts=self.scan({"menu-draw":["src/ui/drawing/MenuDrawCommands.h"]})["findings"]
        self.assertTrue(any(f["rule"]=="menu-draw-dependency" for f in facts))
        self.assertTrue(any(f["rule"]=="menu-draw-live-or-device" for f in facts))

    def map_draw_fixture(self):
        self.put("src/render/core/MapDrawCommands.h", '#include "ui/input/RuntimeInputPixelValues.h"\n')
        self.put("src/render/core/RuntimeMapPixelRules.h", "inline unsigned color(unsigned x) {return x;}\n")
        self.put("src/ui/input/RuntimeInputPixelValues.h", "struct RuntimeInputPixelRect {};\n")
        return {"map-draw":["src/render/core/MapDrawCommands.h","src/render/core/RuntimeMapPixelRules.h"]}

    def test_map_draw_exact_pure_values(self):
        facts=self.scan(self.map_draw_fixture())["findings"]
        self.assertFalse(any(f["rule"].startswith("protected-") or f["rule"].startswith("map-draw-") for f in facts))

    def test_map_draw_native_transitive_rejected(self):
        modules=self.map_draw_fixture()
        self.put("src/ui/input/RuntimeInputPixelValues.h", '#include <d3d11.h>\nID3D11Device* device;\n')
        facts=self.scan(modules)["findings"]
        self.assertTrue(any(f["rule"]=="protected-platform" for f in facts))

    def test_map_draw_live_world_rejected(self):
        modules=self.map_draw_fixture()
        self.put("src/render/core/RuntimeMapPixelRules.h", '#include "core/Bad.h"\n')
        self.put("src/core/Bad.h", "struct GameWorld {};\n")
        facts=self.scan(modules)["findings"]
        self.assertTrue(any(f["rule"]=="map-draw-dependency" for f in facts))
        self.assertTrue(any(f["rule"]=="map-draw-live-or-device" for f in facts))

    def test_map_sprite_material_pure_values(self):
        modules=self.map_draw_fixture()
        self.put("src/render/core/MapDrawCommands.h", '#include "render/core/MapSpriteMaterial.h"\n')
        self.put("src/render/core/MapSpriteMaterial.h", "struct MapSpriteMaterial { unsigned width, indices, palette; };\n")
        facts=self.scan(modules)["findings"]
        self.assertFalse(any(f["rule"].startswith("protected-") or f["rule"].startswith("map-draw-") for f in facts))

    def test_map_sprite_material_native_transitive_rejected(self):
        modules=self.map_draw_fixture()
        self.put("src/render/core/MapDrawCommands.h", '#include "render/core/MapSpriteMaterial.h"\n')
        self.put("src/render/core/MapSpriteMaterial.h", '#include <d3d11.h>\nID3D11Device* device;\n')
        facts=self.scan(modules)["findings"]
        self.assertTrue(any(f["rule"]=="protected-platform" for f in facts))

    def test_map_sprite_material_world_transitive_rejected(self):
        modules=self.map_draw_fixture()
        self.put("src/render/core/MapDrawCommands.h", '#include "render/core/MapSpriteMaterial.h"\n')
        self.put("src/render/core/MapSpriteMaterial.h", '#include "simulation/Bad.h"\n')
        self.put("src/simulation/Bad.h", "struct GameWorld {};\n")
        facts=self.scan(modules)["findings"]
        self.assertTrue(any(f["rule"]=="map-draw-dependency" for f in facts))
        self.assertTrue(any(f["rule"]=="map-draw-live-or-device" for f in facts))

    def test_map_tile_pure_pixel_value_dependency_allowed(self):
        self.put("src/render/preparation/MapTilePreparation.h", '#include "render/core/MapDrawCommands.h"\n')
        self.put("src/render/core/MapDrawCommands.h", '#include "ui/input/RuntimeInputPixelValues.h"\n')
        self.put("src/ui/input/RuntimeInputPixelValues.h", "struct Point { int x, y; };\n")
        report=self.scan({"map-tile-preparation":["src/render/preparation/MapTilePreparation.h"]})
        self.assertFalse(any(f["rule"].startswith("protected-") or f["rule"].startswith("map-tile-") for f in report["findings"]))

    def test_map_tile_forward_live_state_rejected(self):
        self.put("src/render/preparation/MapTilePreparation.h", "struct GameWorld; struct Input { GameWorld* world; };\n")
        report=self.scan({"map-tile-preparation":["src/render/preparation/MapTilePreparation.h"]})
        self.assertIn("map-tile-live-or-device", {f["rule"] for f in report["findings"]})

    def test_map_tile_transitive_native_rejected(self):
        self.put("src/render/preparation/MapTilePreparation.h", '#include "render/core/MapDrawCommands.h"\n')
        self.put("src/render/core/MapDrawCommands.h", '#include <d3d11.h>\n')
        report=self.scan({"map-tile-preparation":["src/render/preparation/MapTilePreparation.h"]})
        self.assertIn("protected-platform", {f["rule"] for f in report["findings"]})

    def test_map_tile_scratch_cache_rejected(self):
        self.put("src/render/preparation/MapTilePreparation.cpp", '#include "render/BadCache.h"\n')
        self.put("src/render/BadCache.h", "thread_local RuntimeMapRenderCache renderCache;\n")
        report=self.scan({"map-tile-preparation":["src/render/preparation/MapTilePreparation.cpp"]})
        self.assertIn("map-tile-dependency", {f["rule"] for f in report["findings"]})
        self.assertIn("map-tile-live-or-device", {f["rule"] for f in report["findings"]})

    def test_exception_requires_owner_reason_followup(self):
        self.put("src/core/Pure.h", "HWND x;\n")
        report = self.scan()
        baseline = self.baseline(report)
        baseline["exceptions"][0]["follow_up"] = ""
        with self.assertRaises(ValueError):
            guard.evaluate(report, baseline)

    def test_resource_path_rejects_native_header(self):
        self.put("src/assets/archive/MixArchive.h", "#include <windows.h>\n")
        report = self.scan({"resource-path": ["src/assets/archive/MixArchive.h"]})
        self.assertIn("protected-platform", {f["rule"] for f in report["findings"]})

    def test_menu_resource_watch_accepts_its_pure_public_header(self):
        self.put("src/ui/resources/RuntimeMenuResourceWatch.h", "#pragma once\n#include <filesystem>\nstruct RuntimeConfig;\n")
        report = self.scan({"menu-resource-watch": ["src/ui/resources/RuntimeMenuResourceWatch.h"]})
        self.assertFalse(any(f["rule"].startswith(("protected-", "menu-resource-watch-")) for f in report["findings"]))

    def test_menu_resource_watch_rejects_native_header(self):
        self.put("src/ui/resources/RuntimeMenuResourceWatch.h", "#include <windows.h>\n")
        report = self.scan({"menu-resource-watch": ["src/ui/resources/RuntimeMenuResourceWatch.h"]})
        self.assertIn("protected-platform", {f["rule"] for f in report["findings"]})

    def test_menu_resource_watch_rejects_config_or_menu_dependency(self):
        self.put("src/ui/resources/RuntimeMenuResourceWatch.h", '#include "runtime/config/RuntimeConfig.h"\n')
        self.put("src/runtime/config/RuntimeConfig.h", "#pragma once\n")
        report = self.scan({"menu-resource-watch": ["src/ui/resources/RuntimeMenuResourceWatch.h"]})
        self.assertIn("menu-resource-watch-dependency", {f["rule"] for f in report["findings"]})

    def test_movie_path_rejects_native_header(self):
        self.put("src/assets/video/BinkPlaybackPaths.h", "#include <windows.h>\n")
        report = self.scan({"movie-path": ["src/assets/video/BinkPlaybackPaths.h"]})
        self.assertIn("protected-platform", {f["rule"] for f in report["findings"]})

    def test_movie_path_rejects_playback_owner(self):
        self.put("src/assets/video/BinkPlaybackPaths.cpp", '#include "assets/video/BinkPlaybackService.h"\n')
        self.put("src/assets/video/BinkPlaybackService.h", "#pragma once\n")
        report = self.scan({"movie-path": ["src/assets/video/BinkPlaybackPaths.cpp"]})
        self.assertIn("movie-path-dependency", {f["rule"] for f in report["findings"]})

    def test_audio_path_rejects_native_header(self):
        self.put("src/assets/audio/AudioSampleResolver.h", "#include <xaudio2.h>\n")
        report = self.scan({"audio-path": ["src/assets/audio/AudioSampleResolver.h"]})
        self.assertIn("protected-platform", {f["rule"] for f in report["findings"]})

    def test_audio_path_rejects_host_or_device_dependency(self):
        self.put("src/assets/audio/AudioSampleResolver.cpp", '#include "audio/backend/Device.h"\n')
        self.put("src/audio/backend/Device.h", "#pragma once\n")
        report = self.scan({"audio-path": ["src/assets/audio/AudioSampleResolver.cpp"]})
        self.assertIn("audio-path-dependency", {f["rule"] for f in report["findings"]})

    def test_resource_path_rejects_host_dependency(self):
        self.put("src/runtime/campaign/RuntimeCampaignDataSource.h", '#include "runtime/host/Paths.h"\n')
        self.put("src/runtime/host/Paths.h", "#pragma once\n")
        report = self.scan({"resource-path": ["src/runtime/campaign/RuntimeCampaignDataSource.h"]})
        self.assertIn("resource-path-dependency", {f["rule"] for f in report["findings"]})

    def test_file_path_rejects_native_header(self):
        self.put("src/core/io/IniFile.h", '#include <windows.h>\n')
        report = self.scan({"file-path": ["src/core/io/IniFile.h"]})
        self.assertIn("protected-platform", {f["rule"] for f in report["findings"]})

    def test_file_path_rejects_provider_dependency(self):
        self.put("src/runtime/config/RuntimeConfigPaths.h", '#include "core/io/RuntimeExecutableDirectory.h"\n')
        self.put("src/core/io/RuntimeExecutableDirectory.h", '#include <filesystem>\n')
        report = self.scan({"file-path": ["src/runtime/config/RuntimeConfigPaths.h"]})
        self.assertIn("file-path-dependency", {f["rule"] for f in report["findings"]})

    def test_service_console_public_sdk_rejected(self):
        self.put("src/network/internet/RuntimeInternetServiceProcess.h", '#include <windows.h>\n')
        facts = self.scan({"service-console": ["src/network/internet/RuntimeInternetServiceProcess.h"]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_service_console_private_outsider_rejected(self):
        self.put("src/network/internet/private/RuntimeInternetServiceConsoleControl.h", "#pragma once\n")
        self.put("src/Other.cpp", '#include "network/internet/private/RuntimeInternetServiceConsoleControl.h"\n')
        facts = self.scan({"service-console": ["src/network/internet/private/RuntimeInternetServiceConsoleControl.h"]})["findings"]
        self.assertIn("service-console-private-owner", {f["rule"] for f in facts})

    def test_service_console_opaque_native_rejected(self):
        self.put("src/network/internet/RuntimeInternetServiceProcess.h", "void* console;\n")
        facts = self.scan({"service-console": ["src/network/internet/RuntimeInternetServiceProcess.h"]})["findings"]
        self.assertIn("service-console-opaque", {f["rule"] for f in facts})

    def test_service_console_main_bypass_rejected(self):
        self.put("src/network/internet/RuntimeInternetServiceMain.cpp", "void f() { SetConsoleCtrlHandler(h, true); }\n")
        self.put("src/network/internet/RuntimeInternetServiceProcess.h", "#pragma once\n")
        facts = self.scan({"service-console": ["src/network/internet/RuntimeInternetServiceProcess.h"]})["findings"]
        self.assertIn("service-console-main-bypass", {f["rule"] for f in facts})

    def test_tactics_codec_direct_sdk_rejected(self):
        self.put("src/simulation/ai/RuntimeComputerTacticsCodec.h", '#include <windows.h>\n')
        facts = self.scan({"computer-tactics-codec": ["src/simulation/ai/RuntimeComputerTacticsCodec.h"]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_tactics_codec_transitive_sdk_rejected(self):
        self.put("src/simulation/ai/RuntimeComputerTacticsCodec.h", '#include "core/io/RuntimeSaveTextFields.h"\n')
        self.put("src/core/io/RuntimeSaveTextFields.h", '#include <windows.h>\n')
        facts = self.scan({"computer-tactics-codec": ["src/simulation/ai/RuntimeComputerTacticsCodec.h"]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_tactics_codec_opaque_native_rejected(self):
        self.put("src/simulation/ai/RuntimeComputerTacticsCodec.h", 'struct Port { void* window; };\n')
        facts = self.scan({"computer-tactics-codec": ["src/simulation/ai/RuntimeComputerTacticsCodec.h"]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_tactics_codec_host_dependency_rejected(self):
        self.put("src/simulation/ai/RuntimeComputerTacticsCodec.h", '#include "runtime/host/Session.h"\n')
        self.put("src/runtime/host/Session.h", '#pragma once\n')
        facts = self.scan({"computer-tactics-codec": ["src/simulation/ai/RuntimeComputerTacticsCodec.h"]})["findings"]
        self.assertIn("reverse-dependency", {f["rule"] for f in facts})

    def test_superweapon_core_direct_diagnostic_include_rejected(self):
        core = "src/simulation/superweapons/SuperWeaponSystem.cpp"
        self.put(core, '#include "simulation/superweapons/diagnostics/Old.inl"\n')
        self.put("src/simulation/superweapons/diagnostics/Old.inl", "void test();\n")
        facts = self.scan({"superweapon-production": [core]})["findings"]
        self.assertIn("superweapon-production-diagnostic-include", {f["rule"] for f in facts})

    def test_superweapon_core_transitive_diagnostic_include_rejected(self):
        core = "src/simulation/superweapons/SuperWeaponSystem.cpp"
        self.put(core, '#include "simulation/superweapons/Helper.h"\n')
        self.put("src/simulation/superweapons/Helper.h", '#include "simulation/superweapons/diagnostics/Old.inl"\n')
        self.put("src/simulation/superweapons/diagnostics/Old.inl", "void test();\n")
        facts = self.scan({"superweapon-production": [core]})["findings"]
        self.assertIn("superweapon-production-diagnostic-include", {f["rule"] for f in facts})

    def test_superweapon_core_legacy_definition_rejected_ignores_text(self):
        core = "src/simulation/superweapons/SuperWeaponSystem.cpp"
        self.put(core, '// SuperWeaponSystem::runSmokeTest()\nconst char* note="SuperWeaponSystem::runOneShotSmokeTest()";\n')
        facts = self.scan({"superweapon-production": [core]})["findings"]
        self.assertNotIn("superweapon-production-diagnostic-definition", {f["rule"] for f in facts})
        self.put(core, 'int SuperWeaponSystem::runSmokeTest() { return 0; }\n')
        facts = self.scan({"superweapon-production": [core]})["findings"]
        self.assertIn("superweapon-production-diagnostic-definition", {f["rule"] for f in facts})

    def test_superweapon_core_target_cannot_compile_diagnostic_source(self):
        path = "src/simulation/superweapons/diagnostics/SuperWeaponSystemSmoke.cpp"
        self.put(path, "int test();\n")
        self.targets["concord_superweapon_core_runtime_leaf"] = dict(sources=[dict(path=path, compileGroupIndex=0)])
        facts = self.scan({"superweapon-production": []})["findings"]
        self.assertIn("superweapon-core-diagnostic-source", {f["rule"] for f in facts})

    def test_superweapon_core_public_sdk_or_opaque_rejected(self):
        header = "src/simulation/superweapons/SuperWeaponSystem.h"
        for content in ('#include <windows.h>\n', 'struct State { void* native; };\n'):
            self.put(header, content)
            facts = self.scan({"superweapon-production": [header]})["findings"]
            self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_load_transaction_host_context_is_rejected(self):
        header = "src/runtime/persistence/RuntimeLoadTransaction.h"
        self.put(header, "struct RuntimeWindowState; void load(RuntimeWindowState&);\n")
        facts = self.scan({"load-transaction": [header]})["findings"]
        self.assertIn("protected-host-state", {f["rule"] for f in facts})

    def test_load_transaction_transitive_sdk_is_rejected(self):
        header = "src/runtime/persistence/RuntimeLoadTransaction.h"
        self.put(header, '#include "runtime/persistence/Helper.h"\n')
        self.put("src/runtime/persistence/Helper.h", "#include <windows.h>\n")
        facts = self.scan({"load-transaction": [header]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_load_transaction_opaque_escape_is_rejected(self):
        header = "src/runtime/persistence/RuntimeLoadTransaction.h"
        self.put(header, "struct Load { void* nativeContext; };\n")
        facts = self.scan({"load-transaction": [header]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_ai_trigger_hash_legacy_shim_has_no_algorithm(self):
        path = "src/simulation/ai/RuntimeComputerTacticsHash.inl"
        self.put(path, "// preserve legacy include site\nmixRuntimeComputerAiTriggerDefinitions(hash, triggers.computerAiTriggers);\n")
        facts = self.scan({"ai-trigger-definition-hash": [path]})["findings"]
        self.assertNotIn("ai-trigger-hash-compatibility-algorithm", {f["rule"] for f in facts})
        self.put(path, "runtimeStableHashMix(hash, triggers.computerAiTriggers.size());\n")
        facts = self.scan({"ai-trigger-definition-hash": [path]})["findings"]
        self.assertIn("ai-trigger-hash-compatibility-algorithm", {f["rule"] for f in facts})

    def test_ai_trigger_hash_public_host_context_is_rejected(self):
        path = "src/simulation/ai/RuntimeComputerAiTriggerHash.h"
        self.put(path, "struct RuntimeWindowState; void hash(RuntimeWindowState&);\n")
        facts = self.scan({"ai-trigger-definition-hash": [path]})["findings"]
        self.assertIn("protected-host-state", {f["rule"] for f in facts})

    def test_ai_trigger_hash_transitive_primitive_sdk_is_rejected(self):
        header = "src/simulation/ai/RuntimeComputerAiTriggerHash.h"
        self.put(header, '#include "core/determinism/RuntimeAuthorityHashPrimitives.h"\n')
        self.put("src/core/determinism/RuntimeAuthorityHashPrimitives.h", "#include <windows.h>\n")
        facts = self.scan({"ai-trigger-definition-hash": [header]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_ai_trigger_hash_opaque_escape_is_rejected(self):
        header = "src/simulation/ai/RuntimeComputerAiTriggerHash.h"
        self.put(header, "struct Hash { void* nativeContext; };\n")
        facts = self.scan({"ai-trigger-definition-hash": [header]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_trigger_scheduler_public_host_root_is_rejected(self):
        path = "src/simulation/triggers/RuntimeTriggerScheduler.h"
        self.put(path, "struct RuntimeWindowState; void advance(RuntimeWindowState&);\n")
        facts = self.scan({"trigger-domain-scheduler": [path]})["findings"]
        self.assertIn("protected-host-state", {f["rule"] for f in facts})

    def test_trigger_scheduler_transitive_native_include_is_rejected(self):
        path = "src/simulation/triggers/RuntimeTriggerState.h"
        self.put(path, '#include "world/values/RuntimeCellPoint.h"\n')
        self.put("src/world/values/RuntimeCellPoint.h", "#include <windows.h>\n")
        facts = self.scan({"trigger-domain-scheduler": [path]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_trigger_scheduler_opaque_session_is_rejected(self):
        path = "src/simulation/triggers/RuntimeTriggerScheduler.h"
        self.put(path, "struct Operations { void* nativeSession; };\n")
        facts = self.scan({"trigger-domain-scheduler": [path]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_trigger_scheduler_host_implementation_edge_is_rejected(self):
        path = "src/simulation/triggers/RuntimeTriggerScheduler.cpp"
        self.put(path, '#include "runtime/host/RuntimeShell.h"\n')
        self.put("src/runtime/host/RuntimeShell.h", "struct HostOnly {};\n")
        facts = self.scan({"trigger-domain-scheduler": [path]})["findings"]
        self.assertIn("protected-dependency", {f["rule"] for f in facts})

    def test_vision_phase_public_host_root_is_rejected(self):
        path = "src/simulation/vision/RuntimeVisionPhase.h"
        self.put(path, "struct RuntimeWindowState; void update(RuntimeWindowState&);\n")
        facts = self.scan({"vision-authority-phase": [path]})["findings"]
        self.assertIn("protected-host-state", {f["rule"] for f in facts})

    def test_vision_phase_transitive_sdk_is_rejected(self):
        path = "src/simulation/vision/RuntimeVisionPhase.h"
        self.put(path, '#include "simulation/vision/PhaseHelper.h"\n')
        self.put("src/simulation/vision/PhaseHelper.h", "#include <windows.h>\n")
        facts = self.scan({"vision-authority-phase": [path]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_vision_phase_erased_session_is_rejected(self):
        path = "src/simulation/vision/RuntimeVisionPhase.h"
        self.put(path, "struct Operations { void* nativeSession; };\n")
        facts = self.scan({"vision-authority-phase": [path]})["findings"]
        self.assertIn("protected-platform", {f["rule"] for f in facts})

    def test_vision_phase_host_implementation_edge_is_rejected(self):
        path = "src/simulation/vision/RuntimeVisionPhase.cpp"
        self.put(path, '#include "runtime/host/RuntimeShell.h"\n')
        self.put("src/runtime/host/RuntimeShell.h", "struct HostOnly {};\n")
        facts = self.scan({"vision-authority-phase": [path]})["findings"]
        self.assertIn("protected-dependency", {f["rule"] for f in facts})

    def test_move_shroud_fixture_keeps_terrain_index_domain(self):
        path = "src/simulation/movement/ground/diagnostics/RuntimeMoveToShroudSmoke.inl"
        self.put(path, "cell.cellIndex512 = (y * 512) + x;\n")
        self.assertNotIn("move-shroud-fixture-terrain-index-domain", {f["rule"] for f in self.scan()["findings"]})
        self.put(path, "cell.cellIndex512 = runtimeCellKey(x,y);\n")
        self.assertIn("move-shroud-fixture-terrain-index-domain", {f["rule"] for f in self.scan()["findings"]})

    def test_save_identity_public_root_is_rejected(self):
        path = "src/runtime/persistence/RuntimeSaveIdentity.h"
        self.put(path, "struct RuntimeWindowState; void validate(RuntimeWindowState&);\n")
        self.assertIn("protected-host-state", {f["rule"] for f in self.scan({"save-identity": [path]})["findings"]})

    def test_save_identity_transitive_sdk_is_rejected(self):
        path = "src/runtime/persistence/RuntimeSaveIdentity.h"
        self.put(path, '#include "runtime/persistence/IdentityHelper.h"\n')
        self.put("src/runtime/persistence/IdentityHelper.h", "#include <windows.h>\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"save-identity": [path]})["findings"]})

    def test_save_identity_erased_session_is_rejected(self):
        path = "src/runtime/persistence/RuntimeSaveIdentity.h"
        self.put(path, "struct Normalizer { void* session; };\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"save-identity": [path]})["findings"]})

    def test_save_identity_host_dependency_is_rejected(self):
        path = "src/runtime/persistence/RuntimeSaveIdentity.cpp"
        self.put(path, '#include "runtime/host/RuntimeShell.h"\n')
        self.put("src/runtime/host/RuntimeShell.h", "struct HostOnly {};\n")
        self.assertIn("protected-dependency", {f["rule"] for f in self.scan({"save-identity": [path]})["findings"]})

    def test_trigger_persistence_public_root_is_rejected(self):
        path = "src/simulation/triggers/RuntimeTriggerPersistence.h"
        self.put(path, "struct RuntimeWindowState; void restore(RuntimeWindowState&);\n")
        self.assertIn("protected-host-state", {f["rule"] for f in self.scan({"trigger-persistence": [path]})["findings"]})

    def test_trigger_persistence_transitive_sdk_is_rejected(self):
        path = "src/simulation/triggers/RuntimeTriggerPersistence.h"
        self.put(path, '#include "simulation/triggers/PersistenceHelper.h"\n')
        self.put("src/simulation/triggers/PersistenceHelper.h", "#include <windows.h>\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"trigger-persistence": [path]})["findings"]})

    def test_trigger_persistence_erased_root_is_rejected(self):
        path = "src/simulation/triggers/RuntimeTriggerPersistence.h"
        self.put(path, "struct Operations { void* session; };\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"trigger-persistence": [path]})["findings"]})

    def test_trigger_persistence_host_edge_is_rejected(self):
        path = "src/simulation/triggers/RuntimeTriggerPersistence.cpp"
        self.put(path, '#include "runtime/host/RuntimeShell.h"\n')
        self.put("src/runtime/host/RuntimeShell.h", "struct HostOnly {};\n")
        self.assertIn("protected-dependency", {f["rule"] for f in self.scan({"trigger-persistence": [path]})["findings"]})

    def test_save_document_public_root_is_rejected(self):
        path = "src/runtime/persistence/RuntimeSaveDocument.h"
        self.put(path, "struct RuntimeWindowState; void parse(RuntimeWindowState&);\n")
        self.assertIn("protected-host-state", {f["rule"] for f in self.scan({"save-document": [path]})["findings"]})

    def test_save_document_transitive_sdk_is_rejected(self):
        path = "src/runtime/persistence/RuntimeSaveDocument.h"
        self.put(path, '#include "runtime/persistence/SaveDocumentHelper.h"\n')
        self.put("src/runtime/persistence/SaveDocumentHelper.h", "#include <windows.h>\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"save-document": [path]})["findings"]})

    def test_save_document_erased_owner_is_rejected(self):
        path = "src/runtime/persistence/RuntimeSaveDocument.h"
        self.put(path, "struct Parser { void* session; };\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"save-document": [path]})["findings"]})

    def test_save_document_host_edge_is_rejected(self):
        path = "src/runtime/persistence/RuntimeSaveDocument.cpp"
        self.put(path, '#include "runtime/host/RuntimeShell.h"\n')
        self.put("src/runtime/host/RuntimeShell.h", "struct HostOnly {};\n")
        self.assertIn("protected-dependency", {f["rule"] for f in self.scan({"save-document": [path]})["findings"]})

    def test_committed_ground_public_root_is_rejected(self):
        path = "src/simulation/movement/ground/RuntimeCommittedGroundMotion.h"
        self.put(path, "struct RuntimeWindowState; void advance(RuntimeWindowState&);\n")
        self.assertIn("protected-host-state", {f["rule"] for f in self.scan({"committed-ground-motion": [path]})["findings"]})

    def test_committed_ground_transitive_sdk_is_rejected(self):
        path = "src/simulation/movement/ground/RuntimeCommittedGroundMotion.h"
        self.put(path, '#include "simulation/movement/Helper.h"\n')
        self.put("src/simulation/movement/Helper.h", "#include <windows.h>\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"committed-ground-motion": [path]})["findings"]})

    def test_committed_ground_erased_owner_is_rejected(self):
        path = "src/simulation/movement/ground/RuntimeCommittedGroundMotion.h"
        self.put(path, "struct Query { void* state; };\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"committed-ground-motion": [path]})["findings"]})

    def test_committed_ground_host_edge_is_rejected(self):
        path = "src/simulation/movement/ground/RuntimeCommittedGroundMotion.cpp"
        self.put(path, '#include "runtime/host/RuntimeShell.h"\n')
        self.put("src/runtime/host/RuntimeShell.h", "struct HostOnly {};\n")
        self.assertIn("protected-dependency", {f["rule"] for f in self.scan({"committed-ground-motion": [path]})["findings"]})

    def test_open_transport_public_root_is_rejected(self):
        path = "src/simulation/combat/targeting/RuntimeOpenTransportMuzzle.h"
        self.put(path, "struct RuntimeWindowState; void query(RuntimeWindowState&);\n")
        self.assertIn("protected-host-state", {f["rule"] for f in self.scan({"open-transport-source": [path]})["findings"]})

    def test_open_transport_transitive_sdk_is_rejected(self):
        path = "src/simulation/combat/targeting/RuntimeOpenTransportMuzzle.h"
        self.put(path, '#include "simulation/combat/targeting/HostHelper.h"\n')
        self.put("src/simulation/combat/targeting/HostHelper.h", "#include <windows.h>\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"open-transport-source": [path]})["findings"]})

    def test_open_transport_erased_owner_is_rejected(self):
        path = "src/simulation/combat/targeting/RuntimeOpenTransportMuzzle.h"
        self.put(path, "struct Query { void* state; };\n")
        self.assertIn("protected-platform", {f["rule"] for f in self.scan({"open-transport-source": [path]})["findings"]})

    def test_open_transport_host_edge_is_rejected(self):
        path = "src/simulation/combat/targeting/RuntimeOpenTransportMuzzle.cpp"
        self.put(path, '#include "runtime/host/RuntimeShell.h"\n')
        self.put("src/runtime/host/RuntimeShell.h", "struct HostOnly {};\n")
        self.assertIn("protected-dependency", {f["rule"] for f in self.scan({"open-transport-source": [path]})["findings"]})

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
