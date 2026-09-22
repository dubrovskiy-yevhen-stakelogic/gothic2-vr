"""Prepare pinned Android dependencies without changing an installed SDK."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import ssl
import stat
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            sha.update(chunk)
    return sha.hexdigest()


def checked_file(path, expected):
    if not path.is_file() or path.is_symlink() or digest(path).lower() != expected.lower():
        raise ValueError(f'Missing or modified dependency file: {path}')


def child(root, relative):
    path = PurePosixPath(relative)
    if path.is_absolute() or '..' in path.parts or any(c in relative for c in '\\:\r\n\x00'):
        raise ValueError(f'Unsafe archive path: {relative!r}')
    target = root.joinpath(*path.parts)
    if not target.resolve().is_relative_to(root.resolve()):
        raise ValueError(f'Archive path escapes its directory: {relative!r}')
    return target


class HttpsRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not newurl.startswith('https://'):
            raise ValueError('Refusing a non-HTTPS redirect')
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def download(url, target, expected, verify_only):
    if target.exists():
        checked_file(target, expected)
        return
    if verify_only:
        raise ValueError(f'Missing dependency archive: {target}. Run SETUP-DEPENDENCIES.bat first.')
    if not url.startswith('https://') or not re.fullmatch('[0-9a-fA-F]{64}', expected):
        raise ValueError('Invalid dependency URL or SHA256')
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + '.partial')
    if partial.exists():
        raise ValueError(f'Previous partial download preserved: {partial}. Inspect and remove it before retrying.')
    opener = urllib.request.build_opener(HttpsRedirectHandler(), urllib.request.HTTPSHandler(context=ssl.create_default_context()))
    request = urllib.request.Request(url, headers={'User-Agent': 'Gothic2VR-source-kit/1'})
    print(f'Downloading {target.name}...', flush=True)
    with opener.open(request, timeout=60) as response, partial.open('xb') as output:
        shutil.copyfileobj(response, output)
    checked_file(partial, expected)
    partial.rename(target)


def extract(archive, destination, verify_only):
    if destination.exists():
        return
    if verify_only:
        raise ValueError(f'Missing extracted dependency: {destination}')
    staging = destination.with_name(destination.name + '.extracting')
    if staging.exists():
        raise ValueError(f'Previous extraction preserved: {staging}. Inspect and remove it before retrying.')
    with zipfile.ZipFile(archive) as source:
        entries = source.infolist()
        names = set()
        for entry in entries:
            child(staging, entry.filename)
            folded = entry.filename.rstrip('/').casefold()
            if folded in names or stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError(f'Duplicate path or symlink in archive: {entry.filename}')
            names.add(folded)
        staging.mkdir(parents=True)
        for entry in entries:
            target = child(staging, entry.filename)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.open(entry) as stream, target.open('xb') as output:
                    shutil.copyfileobj(stream, output)
    staging.rename(destination)


def verify_extraction(archive, destination):
    with zipfile.ZipFile(archive) as source:
        expected = {entry.filename for entry in source.infolist() if not entry.is_dir()}
        actual = {path.relative_to(destination).as_posix() for path in destination.rglob('*') if path.is_file()}
        if expected != actual:
            raise ValueError(f'Extracted dependency file list changed: {destination}')
        for entry in source.infolist():
            if entry.is_dir():
                continue
            path = child(destination, entry.filename)
            sha = hashlib.sha256()
            with source.open(entry) as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                    sha.update(chunk)
            checked_file(path, sha.hexdigest())


def prepare_vulkan(verify_only):
    lock = json.loads((ROOT / 'config/source-lock.json').read_text(encoding='utf-8-sig'))
    entry = next(item for item in lock['archives'] if item['id'] == 'tempestvulkanheaders')
    cache = ROOT / 'toolchain/dependencies'
    archive = child(cache, entry['fileName'])
    destination = cache / 'tempestvulkanheaders'
    download(entry['url'], archive, entry['sha256'], verify_only)
    extract(archive, destination, verify_only)
    verify_extraction(archive, destination)
    source = child(destination, entry['rootDirectory'])
    if not child(source, entry['requiredFile']).is_file():
        raise ValueError('Vulkan Headers required file is missing')
    cmake_path = source.as_posix()
    if any(c in cmake_path for c in '";\n\r$'):
        raise ValueError('Source-kit path cannot contain quotes, semicolons, dollar signs or line breaks')
    contents = '# Generated by tools/bootstrap-dependencies.py.\n'
    contents += f'set(FETCHCONTENT_SOURCE_DIR_TEMPESTVULKANHEADERS "{cmake_path}" CACHE PATH "Pinned Vulkan headers" FORCE)\n'
    init = cache / 'source-dependencies.cmake'
    if verify_only:
        if not init.is_file() or init.read_text(encoding='utf-8') != contents:
            raise ValueError('Dependency paths changed. Run SETUP-DEPENDENCIES.bat again after moving the kit.')
    else:
        init.write_text(contents, encoding='utf-8')
    print('Verified pinned Vulkan Headers.')


def prepare_openxr(verify_only):
    lock = json.loads((ROOT / 'config/openxr-sdk.lock.json').read_text(encoding='utf-8-sig'))
    version = lock['version']
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        raise ValueError('Invalid OpenXR version')
    archive = ROOT / f'toolchain/dependencies/openxr-{version}.aar'
    destination = ROOT / f'toolchain/openxr-{version}'
    download(lock['source_url'], archive, lock['archive_sha256'], verify_only)
    extract(archive, destination, verify_only)
    verify_extraction(archive, destination)
    for entry in lock['files']:
        path = child(ROOT, entry['path'])
        if not path.is_relative_to(destination):
            raise ValueError('OpenXR lock file path escapes its dependency directory')
        checked_file(path, entry['sha256'])
    print('Verified pinned OpenXR loader and headers.')


def install_openxr_windows(extracted, destination, verify_only):
    """Normalise the release archive into the layout engine/CMakeLists.txt expects."""
    if destination.exists():
        return
    if verify_only:
        raise ValueError(f'Missing extracted dependency: {destination}')

    def x64(path):
        return any(part.casefold() == 'x64' for part in path.parts)

    headers = next((path.parent for path in sorted(extracted.rglob('openxr/openxr.h'))), None)
    library = next((path for path in sorted(extracted.rglob('openxr_loader.lib')) if x64(path)), None)
    runtime = next((path for path in sorted(extracted.rglob('openxr_loader.dll')) if x64(path)), None)
    if headers is None or library is None or runtime is None:
        raise ValueError(f'Unexpected OpenXR Windows archive layout under {extracted}')
    staging = destination.with_name(destination.name + '.installing')
    if staging.exists():
        raise ValueError(f'Previous installation preserved: {staging}. Inspect and remove it before retrying.')
    shutil.copytree(headers, staging / 'include/openxr')
    (staging / 'x64/lib').mkdir(parents=True, exist_ok=True)
    shutil.copy2(library, staging / 'x64/lib/openxr_loader.lib')
    (staging / 'x64/bin').mkdir(parents=True, exist_ok=True)
    shutil.copy2(runtime, staging / 'x64/bin/openxr_loader.dll')
    staging.rename(destination)


def record_openxr_windows(lock, lock_path, archive, extracted, destination):
    """Trust-on-first-use: fetch once unverified, then pin what was actually received."""
    url = lock['source_url']
    if not url.startswith('https://'):
        raise ValueError('Invalid dependency URL')
    print('Recording the Windows OpenXR hashes from one unverified fetch of')
    print(f'  {url}')
    print('Compare the recorded digests with the Khronos release page before committing the lock file.')
    if not archive.exists():
        archive.parent.mkdir(parents=True, exist_ok=True)
        partial = archive.with_name(archive.name + '.partial')
        if partial.exists():
            raise ValueError(f'Previous partial download preserved: {partial}. Inspect and remove it before retrying.')
        opener = urllib.request.build_opener(HttpsRedirectHandler(), urllib.request.HTTPSHandler(context=ssl.create_default_context()))
        request = urllib.request.Request(url, headers={'User-Agent': 'Gothic2VR-source-kit/1'})
        print(f'Downloading {archive.name}...', flush=True)
        with opener.open(request, timeout=60) as response, partial.open('xb') as output:
            shutil.copyfileobj(response, output)
        partial.rename(archive)
    extract(archive, extracted, False)
    install_openxr_windows(extracted, destination, False)
    lock['archive_sha256'] = digest(archive)
    for entry in lock['files']:
        path = child(ROOT, entry['path'])
        if not path.is_file() or path.is_symlink():
            raise ValueError(f'Missing dependency file: {path}')
        entry['sha256'] = digest(path)
    lock_path.write_text(json.dumps(lock, indent=2) + '\n', encoding='utf-8')
    print(f'Recorded sha256 values into config/{lock_path.name}. Review and commit it.')


def prepare_openxr_windows(verify_only, record):
    lock_path = ROOT / 'config/openxr-sdk-windows.lock.json'
    lock = json.loads(lock_path.read_text(encoding='utf-8-sig'))
    version = lock['version']
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        raise ValueError('Invalid OpenXR version')
    archive = ROOT / f'toolchain/dependencies/openxr-{version}-win.zip'
    extracted = ROOT / f'toolchain/dependencies/openxr-{version}-win'
    destination = ROOT / f'toolchain/openxr-{version}-win'
    if lock['install_directory'] != f'toolchain/openxr-{version}-win':
        raise ValueError('OpenXR Windows lock install_directory does not match its version')
    if record:
        if verify_only:
            raise ValueError('--record-hashes cannot be combined with --verify-only')
        record_openxr_windows(lock, lock_path, archive, extracted, destination)
        lock = json.loads(lock_path.read_text(encoding='utf-8-sig'))
    blank = [] if re.fullmatch('[0-9a-fA-F]{64}', lock['archive_sha256']) else ['archive_sha256']
    blank += [entry['path'] for entry in lock['files'] if not re.fullmatch('[0-9a-fA-F]{64}', entry['sha256'])]
    if blank:
        raise ValueError(
            'config/openxr-sdk-windows.lock.json has no sha256 for: ' + ', '.join(blank) + '. '
            'It was authored offline, so no digest could be computed. Run this script once with '
            '--record-hashes (tools/prepare-openxr.ps1 -Windows -RecordHashes) on a machine with '
            'network access to pin what the Khronos release actually serves, verify the digests '
            'against the release page, and commit the lock file. Verification is never skipped.')
    download(lock['source_url'], archive, lock['archive_sha256'], verify_only)
    extract(archive, extracted, verify_only)
    verify_extraction(archive, extracted)
    install_openxr_windows(extracted, destination, verify_only)
    for entry in lock['files']:
        path = child(ROOT, entry['path'])
        if not path.is_relative_to(destination):
            raise ValueError('OpenXR lock file path escapes its dependency directory')
        checked_file(path, entry['sha256'])
    print('Verified pinned Windows OpenXR loader and headers.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    # 'all' stays Android-only: the Windows loader is opt-in so the APK build is unaffected.
    parser.add_argument('--component', choices=('vulkan', 'openxr', 'openxr-windows', 'all'), default='all')
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--record-hashes', action='store_true',
                        help='openxr-windows only: pin the digests of one unverified first fetch')
    args = parser.parse_args()
    if args.component in ('vulkan', 'all'):
        prepare_vulkan(args.verify_only)
    if args.component in ('openxr', 'all'):
        prepare_openxr(args.verify_only)
    if args.component == 'openxr-windows':
        prepare_openxr_windows(args.verify_only, args.record_hashes)


if __name__ == '__main__':
    main()
