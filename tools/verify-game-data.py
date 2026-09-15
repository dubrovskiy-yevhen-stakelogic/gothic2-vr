"""Stream-check every private import entry against its indexed SHA256 and size."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import zipfile

ROOT = Path(__file__).resolve().parent.parent
manifest = json.loads((ROOT / "build/private/game-data.manifest.json").read_text(encoding="utf-8-sig"))
archive_path = Path(manifest["output"])
report = {"schema_version": 1, "archive": str(archive_path), "status": "failed"}
output = ROOT / "build/private/game-data.verification.json"

try:
    digest = hashlib.sha256()
    with archive_path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    report["sha256"] = digest.hexdigest()
    if report["sha256"] != manifest["sha256"].lower():
        raise ValueError("Archive hash differs from packaging manifest")
    with zipfile.ZipFile(archive_path) as archive:
        entries = archive.infolist()
        index_name = manifest["index"]
        if not entries or entries[0].filename != index_name or entries[0].file_size > 4 * 1024 * 1024:
            raise ValueError("Missing, oversized or misplaced import index")
        if len({entry.filename.casefold() for entry in entries}) != len(entries):
            raise ValueError("Duplicate archive path")
        indexed = {}
        for line in archive.read(index_name).decode("utf-8").splitlines():
            size_text, expected, name = line.split("\t", 2)
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts or any(c in name for c in "\\:\r\n\t"):
                raise ValueError("Unsafe indexed path")
            if name in indexed or not re.fullmatch(r"[a-f0-9]{64}", expected) or int(size_text) < 0:
                raise ValueError("Invalid index record")
            indexed[name] = (int(size_text), expected)
        if set(indexed) != {entry.filename for entry in entries[1:]} or len(indexed) != manifest["entries"]:
            raise ValueError("Index and ZIP entries differ")
        total = 0
        for name, (size, expected) in indexed.items():
            actual = hashlib.sha256()
            count = 0
            with archive.open(name) as source:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    actual.update(chunk)
                    count += len(chunk)
            if count != size or actual.hexdigest() != expected:
                raise ValueError(f"Size or hash mismatch: {name}")
            total += count
        if total != manifest["bytes"]:
            raise ValueError("Uncompressed total differs from packaging manifest")
        report.update(status="passed", verified_entries=len(indexed), verified_bytes=total,
                      zip_entries_including_index=len(entries), crc_checked=True)
except Exception as error:
    report["error"] = str(error)
finally:
    report["scope"] = "Host archive and every extracted entry verified without device transfer"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
raise SystemExit(0 if report["status"] == "passed" else 1)
