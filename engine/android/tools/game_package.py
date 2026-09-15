"""Package game data without modifying the owner's installation."""

import configparser
import hashlib
import io
import os
from pathlib import Path
import re
import struct
import zipfile

INDEX = "opengothic-private-v1.tsv"
BLOCKED_SUFFIXES = {".exe", ".dll", ".bat", ".cmd", ".ps1", ".lnk", ".sav", ".log"}
SAFE_INI = {
    "GAME": {
        "usegothic1controls": ("useGothic1Controls", 0, 1),
        "usequicksavekeys": ("useQuickSaveKeys", 0, 1),
        "usepotionkeys": ("usePotionKeys", 0, 1),
        "subtitles": ("subTitles", 0, 1),
        "subtitlesplayer": ("subTitlesPlayer", 0, 1),
        "mousesensitivity": ("mouseSensitivity", 0, 1),
    },
    "SOUND": {
        "musicenabled": ("musicEnabled", 0, 1),
        "musicvolume": ("musicVolume", 0, 1),
        "soundvolume": ("soundVolume", 0, 1),
    },
}


def child_ci(path, name):
    return next((p for p in Path(path).iterdir() if p.name.casefold() == name.casefold()), None)


def validate_game(root):
    root = Path(root).expanduser().resolve()
    if not root.is_dir():
        raise ValueError(f"Game directory does not exist: {root}")
    data, work = child_ci(root, "Data"), child_ci(root, "_work")
    if not data or not data.is_dir() or not work or not work.is_dir():
        raise ValueError("Select the installation root containing Data and _work, not its System folder.")
    game_edition(root)
    return root


def world_names(path):
    """Read only the VDF catalog, using the header/entry layout in ZenKit's Vfs.cc."""
    with path.open("rb") as source:
        header = source.read(296)
        signatures = (b"PSVDSC_V2.00\r\n\r\n", b"PSVDSC_V2.00\n\r\n\r", b"PSVDSC_V2.00\x1a\x1a\x1a\x1a")
        if len(header) != 296 or header[256:272] not in signatures:
            raise ValueError(f"Invalid world archive: {path}")
        count, _, _, _, offset, _ = struct.unpack_from("<6I", header, 272)
        offset = offset or 296
        if offset < 296 or count > 1000000 or offset + count * 80 > path.stat().st_size:
            raise ValueError(f"Invalid world catalog: {path}")
        source.seek(offset)
        names = set()
        for _ in range(count):
            entry = source.read(80)
            if len(entry) != 80:
                raise ValueError(f"Truncated world catalog: {path}")
            if struct.unpack_from("<I", entry, 72)[0] & 0x80000000:
                continue
            name = entry[:64].split(b"\0", 1)[0].rstrip().upper()
            if name.endswith(b".ZEN"):
                names.add(name)
        return names


def game_edition(root):
    data = child_ci(root, "Data")
    worlds = child_ci(data, "Worlds.vdf") if data and data.is_dir() else None
    if not worlds or not worlds.is_file():
        raise ValueError("Expected Data/Worlds.vdf from Gothic 1, Gothic II Classic or Night of the Raven.")
    names = world_names(worlds)
    addon = child_ci(data, "Worlds_Addon.vdf")
    if addon and addon.is_file():
        names |= world_names(addon)
    gothic1 = b"WORLD.ZEN" in names
    gothic2 = b"NEWWORLD.ZEN" in names or b"ADDONWORLD.ZEN" in names
    if gothic1 and gothic2:
        raise ValueError("Gothic 1 and Gothic II worlds are mixed. Select a separate, clean installation.")
    if gothic1:
        return "Gothic 1"
    if gothic2 and b"ADDONWORLD.ZEN" in names:
        return "Gothic II: Night of the Raven"
    if gothic2:
        if any("_addon" in p.name.casefold() for p in data.iterdir() if p.is_file()):
            raise ValueError("Addon archives remain but the addon world is missing. Use a complete Classic or NotR installation; removing addon files does not convert the game.")
        return "Gothic II Classic"
    raise ValueError("Expected Gothic 1, Gothic II Classic or Night of the Raven worlds.")


def unsupported_plugins(root):
    """Recognize common Windows plugin locations without rejecting Steam's bundled SystemPack."""
    system = child_ci(root, "System")
    if not system or not system.is_dir():
        return []
    found = [p for p in system.iterdir() if "union" in p.name.casefold()]
    autorun = child_ci(system, "Autorun")
    if autorun and autorun.is_dir():
        found += [p for p in autorun.iterdir() if p.suffix.casefold() == ".dll"]
    return sorted(found, key=str)


def game_files(root):
    root = validate_game(root)
    result = []
    for name in ("Data", "_work"):
        folder = child_ci(root, name)
        for path in sorted(folder.rglob("*")):
            if path.is_symlink():
                raise ValueError(f"Resolve symlinks before packaging: {path}")
            if not path.is_file() or path.suffix.lower() in BLOCKED_SUFFIXES:
                continue
            if path.name.endswith(".og-extract-part"):
                raise ValueError(f"Reserved extraction temporary filename: {path}")
            if not path.resolve().is_relative_to(root):
                raise ValueError(f"File leaves game directory: {path}")
            relative = path.relative_to(folder).as_posix()
            if any(c in relative for c in "\t\r\n\\:"):
                raise ValueError(f"Unsupported asset filename: {path}")
            # Keep the existing import path for both games so older packages and installations remain valid.
            result.append((f"Gothic2/{name}/{relative}", path))
    system = child_ci(root, "System")
    config = child_ci(system, "GothicGame.ini") if system else None
    if config and config.is_file():
        if config.is_symlink() or not config.resolve().is_relative_to(root):
            raise ValueError(f"System configuration must be inside the installation: {config}")
        result.append(("Gothic2/System/GothicGame.ini", config))
    # No executables, Saves, SystemPack.ini or desktop Gothic.ini are copied.
    return result


def safe_preferences(path):
    source = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";",))
    source.read_string(Path(path).read_text(encoding="utf-8-sig", errors="replace"))
    result = configparser.ConfigParser(interpolation=None)
    result.optionxform = str
    for section in source.sections():
        allowed = SAFE_INI.get(section.upper(), {})
        for key, value in source.items(section):
            if key not in allowed:
                continue
            name, minimum, maximum = allowed[key]
            try:
                number = float(value)
            except ValueError:
                continue
            if not minimum <= number <= maximum:
                continue
            if name not in ("mouseSensitivity", "musicVolume", "soundVolume") and number not in (0, 1):
                continue
            if not result.has_section(section.upper()):
                result.add_section(section.upper())
            result[section.upper()][name] = f"{number:g}"
    output = io.StringIO()
    result.write(output, space_around_delimiters=False)
    return output.getvalue().encode("utf-8")


def validate_save(path):
    if not re.fullmatch(r"save_slot_[0-9]+\.sav", Path(path).name):
        raise ValueError(f"Not an OpenGothic slot filename: {path}")
    try:
        with zipfile.ZipFile(path) as archive:
            with archive.open("header") as header:
                if header.read(16) != b"OpenGothic/Save\x00":
                    raise ValueError("Not an OpenGothic save header")
    except (zipfile.BadZipFile, KeyError) as error:
        raise ValueError(f"Not a supported OpenGothic save: {path}") from error


def sha256(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def package(root, output, preferences=None, saves=(), progress=print):
    """Manifest first, regular ZIP entries after; streamable by the phone and ZArchiver."""
    files = game_files(root)
    for save in saves:
        validate_save(save)
        files.append((Path(save).name, Path(save)))
    if len({name.casefold() for name, _ in files}) != len(files):
        raise ValueError("Case-insensitive duplicate filenames in the installation")
    inline = {"Gothic.ini": preferences} if preferences else {}
    manifest = []
    for name, path in files:
        progress(f"Hashing {name}")
        manifest.append(f"{path.stat().st_size}\t{sha256(path)}\t{name}\n")
    for name, contents in inline.items():
        manifest.append(f"{len(contents)}\t{hashlib.sha256(contents).hexdigest()}\t{name}\n")
    index = "".join(manifest).encode("utf-8")
    if len(index) > 4 * 1024 * 1024:
        raise ValueError("Too many files for the private asset index")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".zip.part")
    try:
        # ZIP64 supports a separate archive exceeding 4 GiB; the APK itself must stay below that limit.
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=1) as archive:
            archive.writestr(INDEX, index)
            for name, path in files:
                progress(f"Compressing {name}")
                archive.write(path, name)
            for name, contents in inline.items():
                archive.writestr(name, contents)
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    return {"bytes": sum(p.stat().st_size for _, p in files) + sum(map(len, inline.values())),
            "sha256": sha256(output), "entries": len(files) + len(inline)}
