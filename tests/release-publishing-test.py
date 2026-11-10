#!/usr/bin/env python3
"""Exercise publishing commands without contacting GitHub or publishing a release."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
helper = project / 'scripts/publish-release-assets.sh'
names = ['bacons-helper-windows-x64.zip', 'bacons-helper-windows-x64-obs31.1-32.zip', 'bacons-helper-linux-x86_64.tar.gz', 'bacons-helper-obs-flatpak-x86_64.flatpak']
with tempfile.TemporaryDirectory(prefix='bh-release-') as directory:
    root = Path(directory)
    binary = root / 'bin'
    binary.mkdir()
    fake = binary / 'gh'
    fake.write_text('''#!/usr/bin/env python3
import json, os, sys
with open(os.environ['CALLS'], 'a') as file: file.write(json.dumps(sys.argv[1:])+'\\n')
operation = sys.argv[2]
if operation == 'view': sys.exit(0 if os.environ['EXISTS'] == '1' else 1)
sys.exit(int(os.environ.get('WRITE_STATUS', '0')))
''')
    fake.chmod(0o755)
    assets = root / 'assets with spaces'
    assets.mkdir()
    log = root / 'calls.jsonl'
    env = dict(os.environ, PATH=str(binary)+os.pathsep+os.environ['PATH'], GH_REPO='example/plugin', RELEASE_TAG='Beta_V0.7.2', CALLS=str(log), EXISTS='1')
    def run(exists=True, status='0', tag='Beta_V0.7.2'):
        log.write_text('')
        settings = dict(env, EXISTS='1' if exists else '0', WRITE_STATUS=status, RELEASE_TAG=tag)
        result = subprocess.run(['bash', str(helper), str(assets)], env=settings, capture_output=True, text=True)
        return result, [json.loads(line) for line in log.read_text().splitlines()]
    for name in names: (assets / name).write_bytes(b'package')
    result, calls = run()
    assert result.returncode == 0 and len(calls) == 2
    assert calls[1][:3] == ['release', 'upload', 'Beta_V0.7.2'] and '--clobber' in calls[1]
    assert all(str(assets / name) in calls[1] for name in names)
    assert '--generate-notes' not in calls[1]  # Existing metadata is never rewritten.
    result, calls = run(exists=False)
    assert result.returncode == 0 and calls[1][:3] == ['release', 'create', 'Beta_V0.7.2']
    assert '--verify-tag' in calls[1] and '--generate-notes' in calls[1]
    for exists in (True, False):
        result, calls = run(exists=exists, status='7')
        assert result.returncode == 7
    for name in names:
        (assets / name).unlink()
        result, calls = run()
        assert result.returncode != 0 and calls == []
        (assets / name).write_bytes(b'')
        result, calls = run()
        assert result.returncode != 0 and calls == []
        (assets / name).write_bytes(b'package')
    result, calls = run(tag='')
    assert result.returncode != 0 and calls == []
    result, calls = run(tag='Beta_$(touch INJECTION_MARKER);echo')
    assert result.returncode == 0 and calls[1][2] == 'Beta_$(touch INJECTION_MARKER);echo'
    assert not Path('INJECTION_MARKER').exists()
print('PASS: existing/new releases, complete asset set, metadata preservation, reruns, write failures, missing/empty assets, empty tag, and literal argument handling')
