"""Create the private, indexed import ZIP from a purchased Gothic II installation."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import subprocess

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("game_package", ROOT / "engine/android/tools/game_package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=Path, required=True)
    parser.add_argument("--plan", action="store_true", help="List the import plan without copying game data")
    args = parser.parse_args()
    source = package.validate_game(args.game_root)
    edition = package.game_edition(source)
    if edition != "Gothic II: Night of the Raven":
        raise RuntimeError(f"This project requires NotR data; detected {edition}")
    files = package.game_files(source)
    saves = []
    target = (ROOT / "build/private/game-data.zip").resolve()
    if not target.is_relative_to(ROOT / "build/private") or target.is_relative_to(source):
        raise RuntimeError("Private archive must stay within the project build/private folder")
    report = {
        "schema_version": 1, "edition": edition, "source": str(source), "output": str(target),
        "game_file_count": len(files), "source_bytes": sum(p.stat().st_size for _, p in files),
        "included_saves": [str(p) for p in saves], "index": package.INDEX,
        "contains_purchased_game_data": True, "device_transfer_performed": False,
    }
    if args.plan:
        print(json.dumps(report, indent=2))
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() or target.with_suffix(".zip.part").exists():
        raise RuntimeError("Archive or partial output already exists; preserved without replacement")
    before = [(str(p), p.stat().st_size, p.stat().st_mtime_ns) for _, p in files]
    def progress(message):
        if message.startswith("Compressing Gothic2/Data/"):
            print(message, flush=True)
    report.update(package.package(source, target, saves=saves, progress=progress))
    after = [(str(p), p.stat().st_size, p.stat().st_mtime_ns) for _, p in files]
    if before != after:
        raise RuntimeError("Source changed during packaging; archive retained but not accepted")
    report["archive_bytes"] = target.stat().st_size
    report["status"] = "packaged"
    report["packager_sha256"] = hashlib.sha256(Path(spec.origin).read_bytes()).hexdigest()
    target.with_suffix(".manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    subprocess.run([sys.executable, "-B", str(ROOT / "tools/verify-game-data.py")], check=True)

if __name__ == "__main__":
    main()
