"""Verify packaging and ELF properties without installing or launching an APK."""
import argparse
import os
import re
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parent.parent
lock = json.loads((ROOT / "config/android-toolchain.lock.json").read_text(encoding="utf-8-sig"))
manifest = json.loads((ROOT / "build/android/build-manifest.json").read_text(encoding="utf-8-sig"))
apk = Path(manifest["apk"]["path"])
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk-root', type=Path)
args = parser.parse_args()
sdk_path = args.sdk_root or os.environ.get('ANDROID_SDK_ROOT') or os.environ.get('ANDROID_HOME') or manifest.get('sdkRoot')
if not sdk_path:
    parser.error('Pass --sdk-root or set ANDROID_SDK_ROOT / ANDROID_HOME.')
sdk = Path(sdk_path)
packaging = (ROOT / 'android/CMakeLists.txt').read_text(encoding='utf-8-sig')
version_code = re.search(r'^\s*VERSION_CODE\s+(\d+)\s*$', packaging, re.M).group(1)
version_name = re.search(r'^\s*VERSION_NAME\s+(\S+)\s*$', packaging, re.M).group(1)

report = {"schema_version": 1, "apk": str(apk), "sha256": hashlib.sha256(apk.read_bytes()).hexdigest(), "checks": {}}
checks = report["checks"]
checks["matches_build_manifest"] = report["sha256"].lower() == manifest["apk"]["sha256"].lower()
with zipfile.ZipFile(apk) as archive:
    names = archive.namelist()
    native = [n for n in names if n.startswith("lib/") and n.endswith(".so")]
    checks["only_arm64_native_library"] = sorted(native) == ["lib/arm64-v8a/libopengothic.so", "lib/arm64-v8a/libopenxr_loader.so"]
    data = archive.read("lib/arm64-v8a/libopengothic.so")
    checks["pickup_shaders_packaged"] = all(name in data for name in (b"vr_pickup.vert.sprv", b"vr_pickup.frag.sprv"))
    checks["ranged_family_menu_packaged"] = all(value in data for value in (b"Use as bow default", b"Use as crossbow default"))
    checks["horizontal_string_controls_packaged"] = all(value in data for value in (b"String horizontal X", b"String horizontal Z"))
    checks["independent_string_anchor_packaged"] = b"Support XYZ moves the palm; String controls move the string" in data
    checks["global_trigger_help_packaged"] = b"Stick: scroll | LT/RT: -/+ | A: select | B: back" in data
    checks["two_hand_damage_notice_packaged"] = b"Two-handed weapons deal damage only with a two-handed grip." in data
    checks["run_speed_setting_packaged"] = b"Running speed: < %.2fx >" in data and b"RunSpeed" in data
    checks["first_run_welcome_packaged"] = all(t in data for t in (b"Welcome to Gothic II VR 0.1.0 Alpha", b"WelcomeSeen", b"https://discord.com/channels/747967102895390741/1543691482861408276"))
    checks["release_defaults_packaged"] = b"WelcomeSeen=0" in data and b"ItemCal_DEFAULT_BOW_R=" in data and b"HolsterX2=-0.143947" in data
    checks["entry_contact_diagnostics_packaged"] = b"VR melee entry rejected weapon=" in data
    checks["holster_family_profiles_packaged"] = b"Bow held and holstered defaults saved" in data and b"Crossbow held and holstered defaults saved" in data
    checks["melee_result_diagnostics_packaged"] = b"VR melee result target=" in data and b"VR melee blocked target=" in data
    checks["private_bow_mesh_filter_packaged"] = b"VR_BOW_NO_STRING:" in data and b"VR bow string triangles removed:" in data
    checks["elf64_little_endian_aarch64"] = data[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", data, 18)[0] == 183
    offset = struct.unpack_from("<Q", data, 32)[0]
    size, count = struct.unpack_from("<HH", data, 54)
    loads = []
    for i in range(count):
        header = struct.unpack_from("<IIQQQQQQ", data, offset + i * size)
        if header[0] == 1:
            loads.append({"offset": header[2], "vaddr": header[3], "alignment": header[7]})
    checks["elf_load_segments_16k_compatible"] = bool(loads) and all(x["alignment"] >= 16384 and x["offset"] % 16384 == x["vaddr"] % 16384 for x in loads)
    report["elf_load_segments"] = loads
    prohibited = [n for n in names if n.lower().endswith((".vdf", ".mod", ".zen", ".dat", ".sav", ".ogpart")) or "game-data.zip" in n.lower()]
    checks["no_retail_archives_or_saves"] = not prohibited
    report["asset_entries"] = [n for n in names if n.startswith("assets/")]
    hand_assets = ["BigHandLeft.uxrh", "BigHandRight.uxrh", "BigHandsAlbedo.png", "ULTIMATEXR_LICENSE.txt", "SOURCE.md"]
    checks["licensed_vice_city_hands_packaged"] = all(
        "assets/vrhands/" + name in names and
        archive.read("assets/vrhands/" + name) == (ROOT / "android/assets/vrhands" / name).read_bytes()
        for name in hand_assets)
    inspection = ROOT / "build/android/inspection"
    inspection.mkdir(exist_ok=True)
    library = inspection / "libopengothic.so"
    library.write_bytes(data)
    loader = archive.read("lib/arm64-v8a/libopenxr_loader.so")
    xr_lock = json.loads((ROOT / "config/openxr-sdk.lock.json").read_text())
    loader_lock = next(f for f in xr_lock["files"] if f["path"].endswith("android.arm64-v8a/libopenxr_loader.so"))
    checks["pinned_openxr_loader"] = hashlib.sha256(loader).hexdigest() == loader_lock["sha256"]
    checks["loader_elf64_aarch64"] = loader[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", loader, 18)[0] == 183
    offset = struct.unpack_from("<Q", loader, 32)[0]
    size, count = struct.unpack_from("<HH", loader, 54)
    report["loader_load_segment_alignment"] = [struct.unpack_from("<IIQQQQQQ", loader, offset + i * size)[7] for i in range(count) if struct.unpack_from("<I", loader, offset+i*size)[0] == 1]
    checks["zip_crc"] = archive.testzip() is None
aapt = sdk / "build-tools" / lock["buildToolsVersion"] / "aapt2.exe"
badging = subprocess.check_output([str(aapt), "dump", "badging", str(apk)], text=True, encoding="utf-8")
checks["isolated_application_id"] = f"package: name='{lock['applicationId']}'" in badging
checks["declared_sdk_versions"] = f"minSdkVersion:'{lock['minSdk']}'" in badging and f"targetSdkVersion:'{lock['compileSdk']}'" in badging
checks["vr_launcher"] = "launchable-activity: name='org.tempest.TempestNativeActivity'" in badging
checks["headtracking_required"] = "android.hardware.vr.headtracking" in badging
checks["vr_version"] = f"versionCode='{version_code}'" in badging and f"versionName='{version_name}'" in badging
checks["no_debuggable_manifest"] = "application-debuggable" not in badging
(inspection / "badging.txt").write_text(badging, encoding="utf-8")
manifest_tree = subprocess.check_output([str(aapt), "dump", "xmltree", str(apk), "--file", "AndroidManifest.xml"], text=True, encoding="utf-8")
checks["immersive_vr_categories"] = "org.khronos.openxr.intent.category.IMMERSIVE_HMD" in manifest_tree and "com.oculus.intent.category.VR" in manifest_tree
checks["focus_aware"] = "com.oculus.vr.focusaware" in manifest_tree
checks["gpu_level_trade"] = "com.oculus.trade_cpu_for_gpu_amount" in manifest_tree
(inspection / "manifest-tree.txt").write_text(manifest_tree, encoding="utf-8")
readelf = sdk / "ndk" / lock["ndkVersion"] / "toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe"
symbols = subprocess.check_output([str(readelf), "--dyn-syms", "--wide", str(library)], text=True, encoding="utf-8")
checks["native_activity_entry"] = any("ANativeActivity_onCreate" in line and " UND " not in line for line in symbols.splitlines())
lint = ET.parse(ROOT / "build/android/Gothic2VR/app/build/reports/lint-results-release.xml")
report["lint_warnings"] = [{"id": issue.get("id"), "message": issue.get("message")} for issue in lint.findall("issue") if issue.get("severity") == "Warning"]
checks["lint_no_errors"] = not any(issue.get("severity") in ("Fatal", "Error") for issue in lint.findall("issue"))
report["status"] = "passed" if all(checks.values()) else "failed"
report["scope"] = "Host artifact verification only; no installation, headset rendering or performance acceptance"
(ROOT / "build/android/artifact-verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps({k: v for k, v in report.items() if k != "lint_warnings"}, indent=2))
raise SystemExit(0 if report["status"] == "passed" else 1)
