#!/usr/bin/env python3
"""Record local build provenance and prove that each VPK embeds its matching SELF."""
import argparse
import hashlib
import json
import subprocess
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from vita_sfo import verify_package_sfo


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def git(root, *arguments):
    return subprocess.check_output(['git', '-C', str(root), *arguments], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_dir', type=Path)
    parser.add_argument('--iso', type=Path)
    parser.add_argument('--psarc', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    folder = args.build_dir.resolve()
    artifacts = []
    for product, self_name in [('wiicompiled-vita-iso-check', 'eboot.bin'),
                               ('wiicompiled-vita-game', 'game-eboot.bin')]:
        package = folder / (product + '.vpk')
        if not package.exists():
            continue
        self_file = folder / self_name
        self_hash = digest(self_file)
        with zipfile.ZipFile(package) as archive:
            packaged_hash = hashlib.sha256(archive.read('eboot.bin')).hexdigest()
            title_id = 'WICMKW001' if product.endswith('-game') else 'WICISO001'
            sfo = verify_package_sfo(archive.read('sce_sys/param.sfo'), title_id)
        if packaged_hash != self_hash:
            raise RuntimeError(f'{package}: packaged eboot differs from build SELF')
        files = []
        for path in [package, self_file, folder / product, folder / (product + '.map'),
                     folder / (product + '.velf')]:
            files.append({'path': str(path), 'bytes': path.stat().st_size, 'sha256': digest(path)})
        artifacts.append({'product': product, 'packaged_eboot_matches': True,
                          'packaged_sfo': sfo, 'files': files})
    if not artifacts:
        raise RuntimeError('No native Vita artifacts found')

    # Covers local changes and generated inputs as well as the upstream pins.
    source_hash = hashlib.sha256()
    extensions = {'.cpp', '.c', '.h', '.hpp', '.cc', '.cs', '.S', '.s', '.cmake', '.yml', '.bin', '.py'}
    paths = [root / 'CMakeLists.txt', root / 'build-vita.sh']
    for name in ['core', 'vita', 'runtime', 'translator/src', 'aurora-main', 'projects/mkwii', 'generated', 'scripts']:
        for path in (root / name).rglob('*'):
            if any(part in {'bin', 'obj', 'build', 'TestResults', '.git'} for part in path.relative_to(root).parts):
                continue
            if path.is_file() and (path.suffix in extensions or path.name == 'CMakeLists.txt'):
                paths.append(path)
    for path in sorted(set(paths)):
        source_hash.update(str(path.relative_to(root)).encode() + b'\0')
        source_hash.update(bytes.fromhex(digest(path)))

    cache = {}
    for line in (folder / 'CMakeCache.txt').read_text().splitlines():
        if line.startswith(('WIC_', 'AURORA_VITA_ASYNC_GX:', 'AURORA_VITA_RENDERER:')) and '=' in line:
            key, value = line.split('=', 1)
            cache[key] = value
    result = {
        'created_utc': datetime.now(timezone.utc).isoformat(),
        'upstream_wiicompiled': '279ce8328f17059a4b46172d4ce71d3f403a1aec',
        'aurora_pin': git(root / 'aurora-main', 'rev-parse', 'HEAD'),
        'aurora_worktree_dirty': bool(git(root / 'aurora-main', 'status', '--porcelain')),
        'aurora_branch': git(root / 'aurora-main', 'branch', '--show-current'),
        'aurora_upstream_base': git(root / 'aurora-main', 'merge-base', 'HEAD', 'origin/vita-experiment'),
        'head': git(root, 'rev-parse', 'HEAD'),
        'branch': git(root, 'branch', '--show-current'),
        'worktree_dirty': bool(git(root, 'status', '--porcelain')),
        'source_and_generated_sha256': source_hash.hexdigest(),
        'source_digest_scope': 'source, generated inputs and host data/build tooling',
        'source_files_hashed': len(set(paths)),
        'cmake_options': cache,
        'validation_scope': 'host tests and native ELF/package checks; device behavior unverified',
        'artifacts': artifacts,
    }
    if args.iso:
        path = args.iso.resolve()
        result['iso'] = {'path': str(path), 'bytes': path.stat().st_size, 'sha256': digest(path)}
    if args.psarc:
        path=args.psarc.resolve()
        result['psarc']={'path':str(path),'bytes':path.stat().st_size,'sha256':digest(path)}
        report=next((p for p in (path.with_suffix(path.suffix+'.json'),path.with_suffix('.json')) if p.exists()),None)
        if report is not None:
            result['psarc']['verification_report']={'path':str(report),'sha256':digest(report)}
    destination = folder / 'DELIVERY.json'
    destination.write_text(json.dumps(result, indent=2) + '\n')
    print(f'{destination}: {len(artifacts)} VPK(s), packaged SELF hashes match')


if __name__ == '__main__':
    main()
