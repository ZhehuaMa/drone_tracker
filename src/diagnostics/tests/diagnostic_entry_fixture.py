"""复制真实安装入口，仅重定向固定根路径；保留副本与替换清单供审核。"""
import hashlib
import json
from pathlib import Path
import shutil
import yaml


def prepare(directory, deployment, installation, policy, runtime=None, state=None):
    directory = Path(directory)
    root = directory / 'installation'
    (root / 'bin').mkdir(parents=True)
    (root / 'config').mkdir()
    (root / 'current').symlink_to(Path(installation).resolve())
    runtime = Path(runtime) if runtime else root / 'run/drone_tracker'
    state = Path(state) if state else root / 'var/lib/drone_tracker/diagnostics'
    runtime.parent.mkdir(parents=True, exist_ok=True)
    replacements = {'/opt/drone_tracker': str(root), '/run/drone_tracker': str(runtime),
                    '/var/lib/drone_tracker/diagnostics': str(state)}
    records = {}
    for name in ('run-diagnostics', 'diagnostic-launch.py', 'cpu-policy.py'):
        source = Path(deployment) / name
        original = source.read_text()
        transformed = original
        for before, after in replacements.items():
            transformed = transformed.replace(before, after)
        target = root / 'bin' / name
        target.write_text(transformed)
        target.chmod(0o755)
        records[name] = {'source_sha256': hashlib.sha256(original.encode()).hexdigest(),
                         'fixture_sha256': hashlib.sha256(transformed.encode()).hexdigest()}
    shutil.copy2(policy, root / 'config/cpu-affinity.yaml')
    config = yaml.safe_load((Path(deployment) / 'diagnostics.yaml').read_text())
    config.update(socket=str(runtime / 'diagnostics.sock'), directory=str(state))
    (root / 'config/diagnostics.yaml').write_text(yaml.safe_dump(config, sort_keys=False))
    (directory / 'entry-fixture.json').write_text(json.dumps({'replacements': replacements, 'scripts': records}, indent=2))
    return root, runtime, state
