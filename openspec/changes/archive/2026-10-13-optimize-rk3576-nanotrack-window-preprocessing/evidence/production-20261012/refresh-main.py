import json
import pathlib
import shlex
import subprocess

root = pathlib.Path('/tmp/nanotrack-window-20261012')
build = root / 'build'
subprocess.run(['cmake', '--build', str(build), '--target', 'nanotrack_window_recovery_identity'], check=True)
commands = json.loads((build / 'compile_commands.json').read_text())
for suffix in ('/window_preprocess/recovery.cpp', '/window_preprocess/tests/recovery_test.cpp',
               '/window_preprocess/main.cpp'):
    command = next(row for row in commands if row['file'].endswith(suffix))
    subprocess.run(shlex.split(command['command']), cwd=command['directory'], check=True)
for target in ('nanotrack_window_support', 'nanotrack_window_tests', 'nanotrack_window_recovery'):
    if target == 'nanotrack_window_support':
        (build / 'libnanotrack_window_support.a').unlink(missing_ok=True)
    subprocess.run(['cmake', '-E', 'cmake_link_script',
                    f'CMakeFiles/{target}.dir/link.txt', '--verbose=1'], cwd=build, check=True)
print('诊断源码身份刷新，变更诊断源按 CMake 原编译命令原生重编译及链接。')
