"""在仓库 ARM64 镜像内检查输入并生成目标板部署包。"""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from datetime import datetime, timezone

import yaml


TARGET_MODELS = Path('/opt/drone_tracker/models')
PROGRAMS = ('drone_tracker_node', 'rk3576_recording_worker', 'rk3576_recording_inspect')
MODEL_KEYS = ('yolo_engine', 'nano_template_backbone', 'nano_search_backbone', 'nano_head')
PACKAGER = Path('/packager')
INPUTS = Path('/inputs')
ROS_LIB = Path('/opt/ros/humble/lib')


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def run(*args, env=None):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env)
    if result.returncode:
        raise RuntimeError(f"命令失败: {' '.join(map(str, args))}\n{result.stdout}")
    return result.stdout


def aarch64(path):
    header = run('readelf', '-h', str(path))
    if not re.search(r'Machine:\s*AArch64', header):
        raise RuntimeError(f'不是 AArch64 ELF 文件: {path}')


def parse_camera(path):
    try:
        params = yaml.safe_load(path.read_bytes())['drone_tracker_node']['ros__parameters']
        camera = params['mv_camera']
        if camera['sensor_model'] not in ('ar0234', 'sc233hgs'):
            raise ValueError('不支持的 sensor_model')
        if camera['capture_mode'] not in ('continuous', 'software_trigger'):
            raise ValueError('不支持的 capture_mode')
        if camera['sensor_model'] == 'sc233hgs' and camera['capture_mode'] != 'continuous':
            raise ValueError('SC233HGS 只支持 continuous')
        if camera['capture_mode'] == 'continuous' and camera['trigger_rate_hz'] != 0:
            raise ValueError('连续模式的 trigger_rate_hz 必须为 0')
        if camera['sensor_model'] == 'ar0234' and params.get('recording', {}).get('enabled'):
            raise ValueError('AR0234 不支持独立录像')
        tracker = params['tracker']
        if tracker.get('nano_backbone') not in ('', None):
            raise ValueError('RK3576 nano_backbone 必须为空')
        for key in MODEL_KEYS:
            if not isinstance(tracker.get(key), str) or not tracker[key]:
                raise ValueError(f'必需模型路径缺失: tracker.{key}')
        return params
    except (KeyError, TypeError, ValueError, yaml.YAMLError) as error:
        raise RuntimeError(f'相机配置无效: {error}') from error


def model_sources(params, model_root):
    """把目标板模型路径映射到本地根目录，不接受逃逸或含空白的清单名。"""
    root = model_root.resolve(strict=True)
    mapped = {}
    for key in MODEL_KEYS:
        value = params['tracker'][key]
        path = Path(value)
        try:
            relative = path.relative_to(TARGET_MODELS)
        except ValueError as error:
            raise RuntimeError(f'模型必须位于 {TARGET_MODELS} 下: {key}={value}') from error
        if not relative.parts or '..' in relative.parts or any(c.isspace() for c in str(relative)):
            raise RuntimeError(f'不安全的模型路径: {key}={value}')
        source = root / relative
        try:
            resolved = source.resolve(strict=True)
        except FileNotFoundError as error:
            raise RuntimeError(f'缺少模型: {key}={source}') from error
        if not resolved.is_relative_to(root) or not resolved.is_file():
            raise RuntimeError(f'模型路径越界或不是文件: {key}={source}')
        previous = mapped.get(relative)
        if previous is not None and previous != resolved:
            raise RuntimeError(f'重复模型目标名冲突: {relative}')
        mapped[relative] = resolved
    return mapped


def make_bundle(bundle, source_info):
    input_hashes = {}

    def copy(source, target, provenance=None):
        source = Path(source)
        before = digest(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if digest(target) != before or digest(source) != before:
            raise RuntimeError(f'复制期间输入文件变化: {source}')
        input_hashes[source] = before
        return {'source': provenance or str(source), 'sha256': before}

    camera = INPUTS / 'camera.yaml'
    policy = INPUTS / 'cpu-affinity.yaml'
    install = INPUTS / 'install'
    model_root = INPUTS / 'models'
    params = parse_camera(camera)
    run('python3', str(PACKAGER / 'cpu-policy.py'), str(policy))
    camera_record = copy(camera, bundle / 'config/camera.yaml', source_info['camera_source'])
    policy_record = copy(policy, bundle / 'config/cpu-affinity.yaml', source_info['policy_source'])
    for name in ('deploy.sh', 'run-camera', 'prepare-display.py', 'cpu-policy.py',
                 'drone-tracker.service', 'README.md'):
        copy(PACKAGER / name, bundle / name)
    (bundle / 'deploy.sh').chmod(0o755)
    (bundle / 'run-camera').chmod(0o755)

    program_records = {}
    program_dir = bundle / 'payload/lib/drone_tracker'
    for name in PROGRAMS:
        source = install / 'lib/drone_tracker' / name
        if not source.is_file():
            raise RuntimeError(f'缺少安装程序: {source}')
        aarch64(source)
        program_records[name] = copy(source, program_dir / name,
                                     f"{source_info['install_source']}/lib/drone_tracker/{name}")
        (program_dir / name).chmod(0o755)

    for subdir in ('share/ament_index', 'share/drone_tracker'):
        root = install / subdir
        if not root.is_dir():
            raise RuntimeError(f'缺少 ROS 包发现目录: {root}')
        for source in sorted(root.rglob('*')):
            relative = source.relative_to(install)
            if any(part in ('cmake', 'parameters', 'models', '__pycache__')
                   for part in relative.parts):
                continue
            if source.is_file():
                copy(source, bundle / 'payload' / relative)
    for relative in ('share/ament_index/resource_index/packages/drone_tracker',
                     'share/drone_tracker/local_setup.bash'):
        if not (bundle / 'payload' / relative).is_file():
            raise RuntimeError(f'安装目录缺少 ROS 包发现文件: {relative}')

    private = bundle / 'payload/lib/bundled-ros'
    private.mkdir(parents=True)
    seeds = [ROS_LIB / 'libcv_bridge.so']
    seeds += [path for path in sorted(ROS_LIB.glob('libvision_msgs*.so'))
              if 'generator_py' not in path.name]
    if len(seeds) < 2 or any(not path.is_file() for path in seeds):
        raise RuntimeError('ARM64 镜像缺少 cv_bridge 或 vision_msgs 类型支持库')
    pending = list(program_dir.iterdir())
    seen = set()
    library_records = {}
    dependency_env = os.environ.copy()
    dependency_env['LD_LIBRARY_PATH'] = f"/opt/rknn-runtime/lib:{dependency_env.get('LD_LIBRARY_PATH', '')}"

    def include_library(name, source):
        if name == 'librknnrt.so' or source.name == 'librknnrt.so':
            return
        target = private / name
        if target.exists():
            if digest(target) != digest(source):
                raise RuntimeError(f'ROS 私有库同名冲突: {name}')
            return
        aarch64(source)
        library_records[name] = copy(source, target)
        pending.append(source)

    for seed in seeds:
        include_library(seed.name, seed)
    while pending:
        source = pending.pop()
        if source in seen:
            continue
        seen.add(source)
        output = run('ldd', str(source), env=dependency_env)
        if 'not found' in output:
            raise RuntimeError(f'ARM64 依赖缺失: {source}\n{output}')
        for line in output.splitlines():
            match = re.match(r'\s*(\S+) => (/\S+) \(', line)
            if match:
                name, resolved = match.groups()
                dependency = Path(resolved)
                if dependency.is_relative_to(ROS_LIB):
                    include_library(name, dependency)
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = f"{private}:/opt/rknn-runtime/lib:{env.get('LD_LIBRARY_PATH', '')}"
    for file in (*program_dir.iterdir(), *private.iterdir()):
        aarch64(file)
        output = run('ldd', '-r', str(file), env=env)
        if re.search(r'not found|undefined symbol|version .* not found', output):
            raise RuntimeError(f'包内动态依赖无法解析: {file}\n{output}')
    if any(path.name.startswith('librknnrt') for path in private.iterdir()):
        raise RuntimeError('部署包不能包含系统 RKNN Runtime')

    mapped = model_sources(params, model_root)
    model_records = {}
    model_hashes = []
    for relative, source in sorted(mapped.items()):
        checksum = digest(source)
        input_hashes[source] = checksum
        model_records[str(relative)] = {
            'source': f"{source_info['model_source']}/{relative}", 'sha256': checksum}
        model_hashes.append(f'{checksum}  models/{relative}\n')
        if not source_info['without_models']:
            copy(source, bundle / 'models' / relative)
    (bundle / 'MODELS.sha256').write_text(''.join(model_hashes))

    devices = [params['video_source'], params['mv_camera']['media_device']]
    if params.get('osd', {}).get('enabled'):
        devices.append(params['osd']['drm_device'])
    for device in devices:
        if not isinstance(device, str) or not device.startswith('/dev/') or '\n' in device:
            raise RuntimeError(f'不安全的设备路径: {device}')
    (bundle / 'DEVICES').write_text(''.join(f'{device}\n' for device in devices))
    if params.get('recording', {}).get('directory', '/var/lib/drone_tracker/recordings') != '/var/lib/drone_tracker/recordings':
        raise RuntimeError('录像目录与目标板安装器约定不一致')

    provenance = {
        'camera_config': camera_record,
        'cpu_policy': policy_record,
        'install_root': source_info['install_source'],
        'programs': program_records,
        'ros_libraries': library_records,
        'models': model_records,
        'models_included': not source_info['without_models'],
        'git': {'commit': source_info['git_commit'], 'dirty': source_info['git_dirty']},
        'build_image': {'name': source_info['image'], 'id': source_info['image_id']},
        'files': {str(path): checksum for path, checksum in input_hashes.items()},
    }
    (bundle / 'SOURCE.json').write_text(json.dumps(provenance, ensure_ascii=False, indent=2) + '\n')
    return input_hashes


def verify_inputs(hashes):
    for path, expected in hashes.items():
        if digest(path) != expected:
            raise RuntimeError(f'打包期间输入文件变化: {path}')


def package(output_dir=Path('/output')):
    if os.uname().machine != 'aarch64':
        raise RuntimeError('打包核心必须在 ARM64 Linux 镜像中运行')
    source_info = {
        'camera_source': os.environ['PACKAGE_CAMERA_SOURCE'],
        'policy_source': os.environ['PACKAGE_POLICY_SOURCE'],
        'install_source': os.environ['PACKAGE_INSTALL_SOURCE'],
        'model_source': os.environ['PACKAGE_MODEL_SOURCE'],
        'output_source': os.environ['PACKAGE_OUTPUT_SOURCE'],
        'git_commit': os.environ['PACKAGE_GIT_COMMIT'],
        'git_dirty': os.environ['PACKAGE_GIT_DIRTY'] == 'true',
        'image': os.environ['PACKAGE_IMAGE'],
        'image_id': os.environ['PACKAGE_IMAGE_ID'],
        'without_models': os.environ['PACKAGE_WITHOUT_MODELS'] == '1',
    }
    output = Path(output_dir)
    with tempfile.TemporaryDirectory(prefix='.package-', dir=output) as temporary:
        stage = Path(temporary)
        name = f"drone-tracker-{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}-{stage.name[-8:]}"
        bundle = stage / name
        bundle.mkdir()
        hashes = make_bundle(bundle, source_info)
        verify_inputs(hashes)
        lines = [f'{digest(path)}  {path.relative_to(bundle)}\n'
                 for path in sorted(bundle.rglob('*')) if path.is_file() and path.name != 'SHA256SUMS']
        (bundle / 'SHA256SUMS').write_text(''.join(lines))
        check = subprocess.run(['sha256sum', '--check', '--quiet', 'SHA256SUMS'],
                               cwd=bundle, text=True, capture_output=True)
        if check.returncode:
            raise RuntimeError(f'包内 SHA256SUMS 校验失败: {check.stdout}{check.stderr}')
        temporary_archive = stage / f'{name}.tar.gz'
        with tarfile.open(temporary_archive, 'w:gz') as archive:
            archive.add(bundle, arcname=name)
        verify_inputs(hashes)
        archive_hash = digest(temporary_archive)
        final_archive = output / temporary_archive.name
        final_checksum = output / f'{temporary_archive.name}.sha256'
        if final_archive.exists() or final_checksum.exists():
            raise RuntimeError(f'输出包已存在: {final_archive}')
        checksum_temp = stage / final_checksum.name
        checksum_temp.write_text(f'{archive_hash}  {final_archive.name}\n')
        try:
            os.replace(temporary_archive, final_archive)
            os.replace(checksum_temp, final_checksum)
        except Exception:
            final_archive.unlink(missing_ok=True)
            final_checksum.unlink(missing_ok=True)
            raise
        print(f"部署包: {source_info['output_source']}/{final_archive.name}")


if __name__ == '__main__':
    try:
        package()
    except (OSError, RuntimeError, KeyError) as error:
        print(f'打包失败: {error}', file=sys.stderr)
        sys.exit(1)
