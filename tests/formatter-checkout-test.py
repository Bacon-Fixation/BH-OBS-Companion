#!/usr/bin/env python3
"""Regression: malformed Git symlink blobs, safe checkout, and permanent repair."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1]
def git(root, *args, ok=True, env=None):
    result = subprocess.run(['git', '-C', str(root), *args], text=True, capture_output=True, env=env)
    if ok and result.returncode:
        raise RuntimeError(result.stderr)
    return result

with tempfile.TemporaryDirectory(prefix='bh-formatter-test-') as directory:
    root = Path(directory)
    repo = root / 'source'
    repo.mkdir()
    git(repo, 'init', '-q')
    git(repo, 'config', 'user.email', 'test@example.invalid')
    git(repo, 'config', 'user.name', 'Formatter regression test')
    shutil.copytree(source / 'build-aux', repo / 'build-aux')
    shutil.copytree(source / 'scripts', repo / 'scripts')
    shutil.copyfile(source / '.gitattributes', repo / '.gitattributes')
    git(repo, 'add', '.')
    blob = git(repo, 'hash-object', '-w', '--', 'build-aux/.run-format.zsh').stdout.strip()
    names = ('run-clang-format', 'run-gersemi', 'run-swift-format')
    for name in names:
        git(repo, 'update-index', '--add', '--cacheinfo', f'120000,{blob},build-aux/{name}')
    git(repo, 'commit', '-qm', 'Reproduce malformed symlink entries')
    checkout = root / 'checkout'
    subprocess.run(['git', 'clone', '-q', '--no-checkout', str(repo), str(checkout)], check=True)
    failed = git(checkout, '-c', 'core.symlinks=true', 'checkout', '-f', 'HEAD', ok=False)
    assert failed.returncode and 'File name too long' in failed.stderr, failed.stderr
    env = dict(os.environ, GIT_CONFIG_COUNT='1', GIT_CONFIG_KEY_0='core.symlinks', GIT_CONFIG_VALUE_0='false')
    git(checkout, 'checkout', '-f', 'HEAD', env=env)
    assert all(not (checkout / 'build-aux' / name).is_symlink() for name in names)
    subprocess.run(['bash', str(checkout / 'scripts/repair-formatters.sh')], check=True, capture_output=True)
    for line in git(checkout, 'ls-files', '--stage', '--', *(f'build-aux/{n}' for n in names)).stdout.splitlines():
        assert line.startswith('100755 '), line
    git(checkout, 'config', 'user.email', 'test@example.invalid')
    git(checkout, 'config', 'user.name', 'Formatter regression test')
    git(checkout, 'commit', '-qm', 'Repair formatter file types')
    fixed = root / 'fixed'
    subprocess.run(['git', 'clone', '-q', '--no-checkout', str(checkout), str(fixed)], check=True)
    git(fixed, '-c', 'core.symlinks=true', 'checkout', '-f', 'HEAD')
    for name in names:
        path = fixed / 'build-aux' / name
        assert path.is_file() and not path.is_symlink() and path.stat().st_size < 512
        subprocess.run(['zsh', str(path), '--version'], check=True, capture_output=True)
    # Also exercise a project located below the repository root.
    nested = root / 'nested'
    nested.mkdir()
    git(nested, 'init', '-q')
    project = nested / 'plugin'
    shutil.copytree(source, project)
    subprocess.run(['bash', str(project / 'scripts/repair-formatters.sh')], check=True, capture_output=True)
    entries = git(nested, 'ls-files', '--stage').stdout.splitlines()
    assert len(entries) == 3 and all(line.startswith('100755 ') and '\tplugin/build-aux/' in line for line in entries)
print('PASS: reproduced symlink checkout failure; safe checkout, regular-file repair, fresh checkout, wrappers, and nested repository paths')
